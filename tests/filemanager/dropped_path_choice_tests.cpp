#include "archive/codecs/archivecodeccatalog.h"
#include "archive/support/native_archive_fixture.h"
#include "archive/support/source_extract_fixture.h"
#include "filemanager/views/filetable.h"
#include "filemanager/window/mainwindow.h"
#include "localization/LangUtils.h"
#include "settings/RegistryUtils.h"
#include "support/drag_drop_backend.h"
#include "support/drag_event_position.h"
#include "support/operation_temp_cleanup.h"
#include "support/window_activation.h"
#include "ui/support/layout_inspection.h"
#include <QMimeData>
#include <QPushButton>
#include <QScopeGuard>
#include <QtTest>

class DroppedPathChoiceTests final : public QObject {
    Q_OBJECT

  private slots:
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }
    void cleanup() { QVERIFY(CleanupOperationTestTemps()); }
    void choose_data() {
        QTest::addColumn<bool>("directory");
        QTest::addColumn<QString>("choice");
        for (const bool directory : {false, true})
            for (const QString choice : {"open", "compress", "cancel"})
                QTest::newRow(
                    qPrintable(QString(directory ? "folder-%1" : "archive-%1").arg(choice)))
                    << directory << choice;
    }
    void choose() {
        QFETCH(bool, directory);
        QFETCH(QString, choice);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString input = fixture.filePath(QStringLiteral("исходная папка 空"));
        const QString output = fixture.filePath("output");
        QVERIFY(QDir().mkpath(input));
        QVERIFY(QDir().mkpath(output));
        const QString payload = QDir(input).filePath(QStringLiteral("данные with spaces.txt"));
        const QByteArray bytes("dropped folder bytes\0\xff", 22);
        QVERIFY(SourceExtractFixture::writeFile(payload, bytes));
        const bool openLargeFolder = directory && choice == "open";
        constexpr int extraFiles = 300;
        if (openLargeFolder)
            for (int i = 0; i < extraFiles; ++i)
                QVERIFY(SourceExtractFixture::writeFile(
                    QDir(input).filePath(QString("extra-%1.txt").arg(i)), "extra"));
        const QString archive = fixture.filePath(QStringLiteral("исходный архив 空.ZIP"));
        QVERIFY(NativeArchiveFixture::WriteFiles(archive, {payload}));
        const QString dropped = directory ? input : archive;
        const QByteArray original = SourceExtractFixture::readFile(archive);
        DropBackend backend;
        ArchiveCodecCatalog probe(QString::fromUtf8(SEVENZIP_CODEC_MODULE), 0);
        QCOMPARE(probe.result(), S_OK);
        QCOMPARE(
            backend.loadCodecCatalog(QString::fromUtf8(SEVENZIP_CODEC_MODULE), probe.version()),
            S_OK);
        const QString language = directory ? "ru" : "-";
        SaveRegLang(ToUString(language), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        MainWindow window(&backend);
        window.resize(directory ? 640 : 980, directory ? 400 : 620);
        window.show();
        auto *address = window.findChild<QLineEdit *>("archivePath");
        address->setText(output);
        QTest::keyClick(address, Qt::Key_Return);
        auto *table = window.findChild<FileTable *>("archiveTable");
        QTRY_VERIFY(table->isEnabled());
        FileTable *peerTable = nullptr;
        int peerRows = 0;
        if (openLargeFolder) {
            auto *two = window.findChild<QAction *>("twoPanelsAction");
            QVERIFY(two);
            if (!two->isChecked())
                two->trigger();
            peerTable = table;
            auto *target = window.findChild<QWidget *>("filePanel1");
            QVERIFY(target && target->isVisible());
            address = target->findChild<QLineEdit *>("archivePath");
            table = target->findChild<FileTable *>("archiveTable");
            address->setFocus();
            address->setText(output);
            QTest::keyClick(address, Qt::Key_Return);
            QTRY_VERIFY(table->isEnabled() && table->rowCount() == peerTable->rowCount());
            QVERIFY(WindowActivation::waitForActive(window));
            peerTable->setFocus();
            QTRY_VERIFY(peerTable->hasFocus());
            peerRows = peerTable->rowCount();
        }

        bool asked = false;
        QTimer responder;
        connect(&responder, &QTimer::timeout, &window, [&] {
            auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!dialog || dialog->objectName() != "droppedPathAction")
                return;
            const auto cancelOnFailure = qScopeGuard([&] {
                if (!asked)
                    dialog->reject();
            });
            auto *open = dialog->button(QMessageBox::Open);
            auto *compress = dialog->findChild<QPushButton *>("compressDroppedPath");
            auto *cancel = dialog->button(QMessageBox::Cancel);
            QVERIFY(open && compress && cancel);
            QVERIFY(UiInspection::writeReport(*dialog,
                                              QString("dropped-path-%1-%2").arg(language, choice)));
            QCOMPARE(dialog->defaultButton(), cancel);
            for (auto *button : {open, static_cast<QAbstractButton *>(compress), cancel}) {
                QVERIFY(button->isEnabled());
                QVERIFY(UiInspection::fullyVisible(*button, *dialog));
                QVERIFY(button->width() >= button->sizeHint().width());
                QVERIFY(button->height() >= button->sizeHint().height());
            }
            QVERIFY(UiInspection::separate(*open, *compress, *dialog));
            QVERIFY(UiInspection::separate(*compress, *cancel, *dialog));
            asked = true;
            responder.stop();
            if (choice == "cancel")
                QTest::keyClick(dialog, Qt::Key_Escape);
            else
                (choice == "open" ? open : compress)->click();
        });
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(dropped)});
        if (directory && choice == "open") {
            QDragEnterEvent moveOnly(DragEventPosition(QPoint(5, 5)), Qt::MoveAction, &mime,
                                     Qt::LeftButton, Qt::ShiftModifier);
            QCoreApplication::sendEvent(table->viewport(), &moveOnly);
            QVERIFY(!moveOnly.isAccepted());
        }
        responder.start(5);
        QDragEnterEvent enter(DragEventPosition(QPoint(5, 5)), Qt::CopyAction | Qt::MoveAction,
                              &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(table->viewport(), &enter);
        QVERIFY(enter.isAccepted());
        QCOMPARE(enter.dropAction(), Qt::CopyAction);
        QDropEvent drop(QPointF(5, 5), Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton,
                        Qt::NoModifier);
        QCoreApplication::sendEvent(table->viewport(), &drop);
        responder.stop();
        QVERIFY(asked);
        QCOMPARE(drop.isAccepted(), choice != "cancel");
        if (choice != "cancel")
            QCOMPARE(drop.dropAction(), Qt::CopyAction);
        if (choice == "open") {
            const QPoint windowPoint = table->viewport()->mapTo(&window, QPoint(5, 5));
            if (openLargeFolder) {
                QVERIFY(!table->isEnabled());
                QDragEnterEvent blocked(DragEventPosition(windowPoint), Qt::CopyAction, &mime,
                                        Qt::LeftButton, Qt::NoModifier);
                QCoreApplication::sendEvent(&window, &blocked);
                QVERIFY(!blocked.isAccepted());
            }
            const int expectedRows = openLargeFolder ? extraFiles + 1 : 1;
            QTRY_VERIFY(table->isEnabled() && table->rowCount() == expectedRows);
            if (openLargeFolder) {
                QDragEnterEvent ready(DragEventPosition(windowPoint), Qt::CopyAction, &mime,
                                      Qt::LeftButton, Qt::NoModifier);
                QCoreApplication::sendEvent(&window, &ready);
                QVERIFY(ready.isAccepted());
                QDragLeaveEvent leave;
                QCoreApplication::sendEvent(&window, &leave);
            }
            bool foundPayload = false;
            for (int row = 0; row < table->rowCount(); ++row)
                foundPayload |= table->model()->index(row, 0).data().toString() ==
                                QFileInfo(payload).fileName();
            QVERIFY(foundPayload);
            if (peerTable) {
                QVERIFY(WindowActivation::waitForActive(window));
                QTRY_VERIFY(table->hasFocus());
                QCOMPARE(peerTable->rowCount(), peerRows);
            }
            QVERIFY(address->text().contains(QFileInfo(dropped).fileName()));
            QVERIFY(backend.Results.isEmpty());
            QVERIFY(QDir(output).isEmpty());
        } else if (choice == "compress") {
            QTRY_COMPARE_WITH_TIMEOUT(backend.Results.size(), 1, 30000);
            QCOMPARE(backend.Results.first().Status, QtOperationResult::Outcome::Success);
            const QStringList outputs = QDir(output).entryList(QDir::Files);
            QCOMPARE(outputs.size(), 1);
            const QString extracted = fixture.filePath("extracted");
            QProcess engine;
            engine.start(QStandardPaths::findExecutable("7z"),
                         {"x", "-o" + extracted, "--", QDir(output).filePath(outputs.first())});
            QVERIFY(engine.waitForFinished(10000));
            QCOMPARE(engine.exitCode(), 0);
            const QString relative = QFileInfo(dropped).fileName() +
                                     (directory ? '/' + QFileInfo(payload).fileName() : QString());
            QCOMPARE(SourceExtractFixture::readFile(QDir(extracted).filePath(relative)),
                     directory ? bytes : original);
            QCOMPARE(QFileInfo(address->text()).canonicalFilePath(),
                     QFileInfo(output).canonicalFilePath());
        } else {
            QVERIFY(backend.Results.isEmpty());
            QVERIFY(QDir(output).isEmpty());
            QCOMPARE(QFileInfo(address->text()).canonicalFilePath(),
                     QFileInfo(output).canonicalFilePath());
        }
        QVERIFY(window.findChild<QAction *>("toolbarAddAction")->isEnabled());
        QCOMPARE(SourceExtractFixture::readFile(payload), bytes);
        QCOMPARE(SourceExtractFixture::readFile(archive), original);
    }
};

QTEST_MAIN(DroppedPathChoiceTests)
#include "dropped_path_choice_tests.moc"
