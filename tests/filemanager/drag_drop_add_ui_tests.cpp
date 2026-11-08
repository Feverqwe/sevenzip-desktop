#include "archive/codecs/archivecodeccatalog.h"
#include "archive/support/native_archive_fixture.h"
#include "archive/support/source_extract_fixture.h"
#include "filemanager/panels/qtpaneldragtransport.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "support/drag_drop_backend.h"
#include "support/drag_event_position.h"
#include "support/operation_kill.h"
#include "support/operation_temp_cleanup.h"
#include "support/scoped_environment.h"
#include "support/window_activation.h"
#include <QJsonDocument>
#include <QJsonObject>

class DragDropAddUiTests final : public QObject {
    Q_OBJECT
  private slots:
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }
    void cleanup() { QVERIFY(CleanupOperationTestTemps()); }
    void externalArchiveDrop_data() {
        QTest::addColumn<bool>("valid");
        QTest::addColumn<bool>("multiple");
        QTest::addColumn<bool>("peer");
        QTest::newRow("open-archive") << true << false << false;
        QTest::newRow("open-in-other-panel") << true << false << true;
        QTest::newRow("damaged-archive") << false << false << false;
        QTest::newRow("damaged-in-other-panel") << false << false << true;
        QTest::newRow("multiple-files-add") << true << true << false;
    }
    void externalArchiveDrop() {
#ifndef SEVENZIP_CODEC_MODULE
        QSKIP("Requires the official native module");
#endif
        QFETCH(bool, valid);
        QFETCH(bool, multiple);
        QFETCH(bool, peer);
        QTemporaryDir root;
        QFile input(root.filePath(QStringLiteral("содержимое.txt")));
        QVERIFY(input.open(QIODevice::WriteOnly));
        input.write("archive drop content");
        input.close();
        const QString archive = root.filePath(QStringLiteral("мой архив.ZIP"));
        DropBackend backend;
        backend.Cancel = multiple;
#ifdef SEVENZIP_CODEC_MODULE
        ArchiveCodecCatalog probe(QString::fromUtf8(SEVENZIP_CODEC_MODULE), 0);
        QCOMPARE(probe.result(), S_OK);
        QCOMPARE(
            backend.loadCodecCatalog(QString::fromUtf8(SEVENZIP_CODEC_MODULE), probe.version()),
            S_OK);
#endif
        QSignalSpy created(&backend, &ArchiveBackend::finished);
        QVERIFY(NativeArchiveFixture::WriteFiles(archive, {input.fileName()}));
        if (!valid) {
            QFile damaged(archive);
            QVERIFY(damaged.open(QIODevice::WriteOnly | QIODevice::Truncate));
            damaged.write("damaged archive");
        }
        MainWindow window(&backend);
        window.show();
        window.activateWindow();
        auto *address = window.findChild<QLineEdit *>("archivePath");
        address->setText(root.path());
        QTest::keyClick(address, Qt::Key_Return);
        auto *table = window.findChild<FileTable *>("archiveTable");
        QTRY_VERIFY(table->isEnabled());
        QLineEdit *otherAddress = nullptr;
        FileTable *otherTable = nullptr;
        if (peer) {
            auto *two = window.findChild<QAction *>("twoPanelsAction");
            if (!two->isChecked())
                two->trigger();
            auto *right = window.findChild<QWidget *>("filePanel1");
            QVERIFY(right && right->isVisible());
            otherAddress = address;
            otherTable = table;
            address = right->findChild<QLineEdit *>("archivePath");
            table = right->findChild<FileTable *>("archiveTable");
            address->setFocus();
            address->setText(root.path());
            QTest::keyClick(address, Qt::Key_Return);
            QTRY_VERIFY(table->isEnabled());
            QTRY_VERIFY(table->rowCount() >= 2);
            QVERIFY(WindowActivation::waitForActive(window));
            otherTable->setFocus();
            QTRY_VERIFY(otherTable->hasFocus());
        }
        QMimeData mime;
        QList<QUrl> urls{QUrl::fromLocalFile(archive)};
        if (multiple)
            urls.append(QUrl::fromLocalFile(input.fileName()));
        mime.setUrls(urls);
        QDragEnterEvent enter(DragEventPosition(QPoint(5, 5)), Qt::CopyAction | Qt::MoveAction,
                              &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(table->viewport(), &enter);
        QVERIFY(enter.isAccepted());
        bool errorShown = false;
        bool choiceShown = false;
        QTimer respond;
        connect(&respond, &QTimer::timeout, &window, [&] {
            if (auto *error = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                if (error->objectName() == "droppedPathAction") {
                    choiceShown = true;
                    error->button(QMessageBox::Open)->click();
                    return;
                }
                errorShown = true;
                error->accept();
            }
        });
        respond.start(5);
        QDropEvent drop(QPointF(5, 5), Qt::CopyAction | Qt::MoveAction, &mime, Qt::NoButton,
                        Qt::NoModifier);
        QCoreApplication::sendEvent(table->viewport(), &drop);
        respond.stop();
        if (otherTable)
            QVERIFY(WindowActivation::waitForActive(window));
        QCOMPARE(choiceShown, !multiple);
        if (multiple) {
            QTRY_COMPARE(backend.Results.size(), 1);
            QCOMPARE(backend.Results.first().Status, QtOperationResult::Outcome::Cancelled);
        }
        QCOMPARE(errorShown, !valid);
        QVERIFY(QFileInfo::exists(archive));
        if (valid && !multiple) {
            QVERIFY(drop.isAccepted());
            QCOMPARE(drop.dropAction(), Qt::CopyAction);
            QVERIFY(address->text().contains(QFileInfo(archive).fileName()));
            bool found = false;
            for (int row = 0; row < table->rowCount(); ++row)
                found |= table->model()->index(row, 0).isValid() &&
                         table->model()->index(row, 0).data().toString() ==
                             QFileInfo(input.fileName()).fileName();
            QVERIFY(found);
            if (otherAddress) {
                QCOMPARE(QDir::cleanPath(otherAddress->text()), QDir::cleanPath(root.path()));
                QTRY_VERIFY(table->hasFocus());
            }
        } else {
            QCOMPARE(QDir::cleanPath(address->text()), QDir::cleanPath(root.path()));
            if (otherTable)
                QTRY_VERIFY(otherTable->hasFocus());
        }
    }
    void externalFilesystemDropCreatesArchiveWithNativeName() {
#ifndef SEVENZIP_CODEC_MODULE
        QSKIP("Source drop Add requires the official native module");
#endif
        QTemporaryDir root;
        QVERIFY(QDir().mkpath(root.filePath("input")));
        QVERIFY(QDir().mkpath(root.filePath("output")));
        const QString name = QStringLiteral("добавленный файл.bin");
        QFile input(QDir(root.filePath("input")).filePath(name));
        QVERIFY(input.open(QIODevice::WriteOnly));
        const QByteArray bytes("Add\0native bytes", 16);
        QCOMPARE(input.write(bytes), qint64(bytes.size()));
        input.close();
        DropBackend backend;
#ifdef SEVENZIP_CODEC_MODULE
        ArchiveCodecCatalog probe(QString::fromUtf8(SEVENZIP_CODEC_MODULE), 0);
        QCOMPARE(probe.result(), S_OK);
        QCOMPARE(
            backend.loadCodecCatalog(QString::fromUtf8(SEVENZIP_CODEC_MODULE), probe.version()),
            S_OK);
#endif
        MainWindow window(&backend);
        window.show();
        auto *address = window.findChild<QLineEdit *>("archivePath");
        address->setText(root.filePath("output"));
        QTest::keyClick(address, Qt::Key_Return);
        auto *table = window.findChild<FileTable *>("archiveTable");
        QTRY_VERIFY(table->isEnabled());
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(input.fileName())});
        QDragEnterEvent enter(DragEventPosition(QPoint(5, 5)), Qt::CopyAction, &mime,
                              Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(table->viewport(), &enter);
        QVERIFY(enter.isAccepted());
        const QString nameReport = root.filePath("dialog-name.json");
        ScopedTestEnvironment report("SEVENZIP_OPERATION_CREATE_NAME_REPORT", nameReport);
        QDropEvent drop(QPointF(5, 5), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(table->viewport(), &drop);
        QTRY_COMPARE_WITH_TIMEOUT(backend.Results.size(), 1, 30000);
        QCOMPARE(backend.Results.first().Status, QtOperationResult::Outcome::Success);
        const auto dialog =
            QJsonDocument::fromJson(SourceExtractFixture::readFile(nameReport)).object();
        const QString archiveName = dialog.value("name").toString();
        const QString format = dialog.value("format").toString();
        QVERIFY(!format.isEmpty());
        QCOMPARE(archiveName, QFileInfo(name).completeBaseName() + "." + format);
        QVERIFY(drop.isAccepted());
        const QString archive = QDir(root.filePath("output")).filePath(archiveName);
        QVERIFY(QFileInfo::exists(archive));
        QVERIFY(!QFileInfo::exists(QDir(root.filePath("output")).filePath(name)));
        QVERIFY(QFileInfo::exists(input.fileName()));
        QVERIFY(!backend.busy());
        ArchiveBackend verifier;
        QSignalSpy finished(&verifier, &ArchiveBackend::finished);
        verifier.list(archive);
        QVERIFY(finished.wait(30000));
        QVERIFY(finished.takeFirst().first().toBool());
        const QString output = root.filePath("verify");
        verifier.extract(archive, output);
        if (finished.isEmpty())
            QVERIFY(finished.wait(30000));
        QVERIFY(finished.takeFirst().first().toBool());
        QFile result(QDir(output).filePath(name));
        QVERIFY(result.open(QIODevice::ReadOnly));
        QCOMPARE(result.readAll(), bytes);
    }

    void independentCompletion_data() { sourceLifetimeData(); }
    void independentCompletion() { verifySourceLifetime(false); }
    void temporarySourceLifetime_data() { sourceLifetimeData(); }
    void temporarySourceLifetime() { verifySourceLifetime(true); }

  private:
    static void sourceLifetimeData() {
        QTest::addColumn<QString>("outcome");
        for (const QString &outcome :
             {QString("create"), QString("cancel"), QString("failure"), QString("crash")})
            QTest::newRow(qPrintable(outcome)) << outcome;
    }
    void verifySourceLifetime(bool wait) {
#ifndef SEVENZIP_CODEC_MODULE
        QSKIP("Requires the official native module");
#else
        QFETCH(QString, outcome);
        const bool cancel = outcome == "cancel", failure = outcome == "failure",
                   crash = outcome == "crash";
        QTemporaryDir input, output;
        QVERIFY(input.isValid() && output.isValid());
        const QString source = input.filePath(QStringLiteral("drop 空 с пробелом.bin"));
        const QByteArray bytes("temporary drop bytes");
        QVERIFY(SourceExtractFixture::writeFile(source, bytes));
        DropBackend backend;
        backend.Barrier = output.filePath("continue");
        backend.Cancel = cancel;
        backend.Fail = failure;
        ArchiveCodecCatalog catalog(QStringLiteral(SEVENZIP_CODEC_MODULE), 0);
        QCOMPARE(backend.loadCodecCatalog(QStringLiteral(SEVENZIP_CODEC_MODULE), catalog.version()),
                 S_OK);
        MainWindow window(&backend);
        window.show();
        auto *address = window.findChild<QLineEdit *>("archivePath");
        address->setText(output.path());
        QTest::keyClick(address, Qt::Key_Return);
        auto *table = dynamic_cast<FileTable *>(window.findChild<FileTable *>("archiveTable"));
        QVERIFY(table);
        QTRY_VERIFY(table->isEnabled());
        unsigned errors = 0;
        QTimer dismiss;
        connect(&dismiss, &QTimer::timeout, &window, [&] {
            if (auto *error = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                ++errors;
                error->accept();
            }
        });
        dismiss.start(5);
        bool returned = false, checked = false;
        QTimer release;
        connect(&release, &QTimer::timeout, &window, [&] {
            if (!backend.Accepted)
                return;
            release.stop();
            checked = true;
            QCOMPARE(returned, !wait);
            QVERIFY(backend.Results.isEmpty());
            QCOMPARE(SourceExtractFixture::readFile(source), bytes);
            // Waiting protects the source stack from teardown; ordinary drops
            // can finish after the manager closes.
            QCOMPARE(window.close(), !wait);
            if (crash)
                QVERIFY(KillOperationProbe(backend.ProcessId));
            else
                QVERIFY(SourceExtractFixture::writeFile(backend.Barrier, "continue"));
        });
        release.start(10);
        UStringVector paths;
        paths.Add(ToUString(source));
        UInt32 flags = 0;
        const HRESULT result = table->sourcePanel->CompressDropFiles(
            paths, ToUString(output.path() + '/'), true, false,
            wait ? k_SourceFlags_TempFiles : k_SourceFlags_DoNotWaitFinish, flags);
        returned = true;
        if (wait) {
            QVERIFY(checked);
            QCOMPARE(result, cancel ? E_ABORT : (failure || crash) ? E_FAIL : S_OK);
        } else {
            QCOMPARE(result, S_OK);
        }
        QTRY_COMPARE_WITH_TIMEOUT(backend.Results.size(), 1, 30000);
        QVERIFY(checked);
        QCOMPARE(backend.Results.first().Status, crash     ? QtOperationResult::Outcome::Crashed
                                                 : cancel  ? QtOperationResult::Outcome::Cancelled
                                                 : failure ? QtOperationResult::Outcome::Failure
                                                           : QtOperationResult::Outcome::Success);
        QCOMPARE(errors, crash && wait ? 1u : 0u);
        if (wait)
            QVERIFY(window.close());
        QCOMPARE(SourceExtractFixture::readFile(source), bytes);
        if (!cancel && !failure && !crash) {
            QVERIFY(!backend.Results.first().ChangedArchives.isEmpty());
            const QString archive = backend.Results.first().ChangedArchives.first();
            // The drag source may release its temporary bytes immediately
            // after completion. The independent output must retain them.
            QVERIFY(QFile::remove(source));
            ArchiveBackend verifier;
            QSignalSpy finished(&verifier, &ArchiveBackend::finished);
            verifier.list(archive);
            QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 30000);
            QVERIFY(finished.takeFirst().first().toBool());
            verifier.extract(archive, output.filePath("verify"));
            QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 30000);
            QVERIFY(finished.first().first().toBool());
            QCOMPARE(SourceExtractFixture::readFile(
                         QDir(output.filePath("verify")).filePath(QFileInfo(source).fileName())),
                     bytes);
        } else {
            QVERIFY(backend.Results.first().ChangedArchives.isEmpty());
        }
#endif
    }

  private slots:
    void launchFailureReleasesTemporarySource() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString source = root.filePath(QStringLiteral("source 空.txt"));
        QVERIFY(SourceExtractFixture::writeFile(source, "keep source"));
        DropBackend backend;
        backend.Program = root.filePath("missing-7zG");
        MainWindow window(&backend);
        window.show();
        auto *table = dynamic_cast<FileTable *>(window.findChild<FileTable *>("archiveTable"));
        QVERIFY(table);
        QTRY_VERIFY(table->isEnabled());
        unsigned errors = 0;
        QTimer dismiss;
        connect(&dismiss, &QTimer::timeout, &window, [&] {
            if (auto *error = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                ++errors;
                error->accept();
            }
        });
        dismiss.start(5);
        UStringVector paths;
        paths.Add(ToUString(source));
        UInt32 flags = 0;
        QCOMPARE(table->sourcePanel->CompressDropFiles(paths, ToUString(root.path() + '/'), true,
                                                       false, k_SourceFlags_TempFiles, flags),
                 E_FAIL);
        QCOMPARE(errors, 1u);
        QVERIFY(backend.Results.isEmpty());
        QCOMPARE(SourceExtractFixture::readFile(source), QByteArray("keep source"));
        QVERIFY(window.close());
    }
};
QTEST_MAIN(DragDropAddUiTests)
#include "drag_drop_add_ui_tests.moc"
