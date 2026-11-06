#include "7zip/UI/FileManager/ProgressDialog2Res.h"
#include "archive/support/native_archive_fixture.h"
#include "archive/support/source_extract_fixture.h"
#include "dialogs/copy/alias.h"
#include "dialogs/overwrite/OverwriteDialog.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "platform/support/native_property_failure_runtime.h"
#include "settings/RegistryUtils.h"
#include "support/delete_reply.h"
#include "support/overwrite_reply.h"
#include "support/widget_dialogs.h"
#include "ui/progress/ProgressDialog2.h"
#include <QScopeGuard>
#include <QStandardPaths>

class ArchiveMoveCompletionTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    WidgetDialogFixture dialogs_;
    static bool run(const QString &directory, const QStringList &args) {
        QProcess process;
        process.setWorkingDirectory(directory);
        process.start(QStandardPaths::findExecutable("7z"), args);
        return process.waitForFinished(10000) && process.exitStatus() == QProcess::NormalExit &&
               process.exitCode() == 0;
    }
  private slots:
    void overwriteSkipKeepsArchiveSelection_data() {
        QTest::addColumn<bool>("directory");
        QTest::addColumn<QString>("answer");
        QTest::addColumn<QString>("format");
        QTest::addColumn<bool>("lateFailure");
        QTest::newRow("file-no") << false << QString("overwriteNo") << QString("zip") << false;
        QTest::newRow("directory-no") << true << QString("overwriteNo") << QString("zip") << false;
        QTest::newRow("directory-no-to-all")
            << true << QString("overwriteNoAll") << QString("zip") << false;
        QTest::newRow("file-overwrite-yes")
            << false << QString("overwriteYes") << QString("zip") << false;
        QTest::newRow("solid-neighbor-overwrite-yes")
            << false << QString("overwriteYes") << QString("7z") << false;
        QTest::newRow("published-removal-reopen-failure")
            << false << QString("overwriteYes") << QString("7z") << true;
        QTest::newRow("overwrite-cancel")
            << false << QString("overwriteCancel") << QString("zip") << false;
        QTest::newRow("host-and-stream-overwrite-yes")
            << false << QString("overwriteYes") << QString("wim") << false;
        QTest::newRow("directory-streams-overwrite-yes")
            << true << QString("overwriteYes") << QString("wim") << false;
    }
    void overwriteSkipKeepsArchiveSelection() {
        QFETCH(bool, directory);
        QFETCH(QString, answer);
        const bool noToAll = answer == "overwriteNoAll";
        const bool accept = answer == "overwriteYes";
        QFETCH(QString, format);
        QFETCH(bool, lateFailure);
        const bool streams = format == "wim";
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settings);
        SaveRegLang(L"-", settings);
        ReloadLang(settings);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString input = fixture.filePath("input");
        const QString output = fixture.filePath("output");
        const QString selection = directory ? QStringLiteral("folder 空")
                                  : streams ? QStringLiteral("h")
                                            : QStringLiteral("payload 空.txt");
        const QString collision = directory ? selection + "/a.txt" : selection;
        const QString streamOwner = directory ? selection : collision;
        const QString copied = selection + "/z.txt";
        const QString secondCollision = selection + "/b.txt";
        QVERIFY(SourceExtractFixture::writeFile(QDir(input).filePath(collision), "archive bytes"));
        if (directory) {
            QVERIFY(SourceExtractFixture::writeFile(QDir(input).filePath(copied), "copied child"));
            QVERIFY(SourceExtractFixture::writeFile(QDir(input).filePath(secondCollision),
                                                    "second archive bytes"));
            QVERIFY(SourceExtractFixture::writeFile(QDir(output).filePath(secondCollision),
                                                    "second existing bytes"));
        }
        QVERIFY(SourceExtractFixture::writeFile(QDir(input).filePath("control.txt"), "control"));
        QVERIFY(
            SourceExtractFixture::writeFile(QDir(output).filePath(collision), "existing bytes"));
        if (streams)
            QVERIFY(SourceExtractFixture::writeFile(output + '/' + streamOwner + ":stream δ",
                                                    "existing stream"));
        if (streams && directory)
            QVERIFY(SourceExtractFixture::writeFile(output + '/' + streamOwner + ":other 空",
                                                    "existing other stream"));
        const QString archive = fixture.filePath("source." + (lateFailure ? "fixture7z" : format));
        QStringList args{"a", "-t" + format};
        if (format == "7z")
            args << "-ms=on";
        args << "--" << archive << selection << "control.txt";
        if (streams) {
            const auto item = [](const QString &path, const QByteArray &bytes) {
                NativeArchiveFixture::Item entry;
                entry.Path = path;
                entry.Bytes = bytes;
                return entry;
            };
            QList<NativeArchiveFixture::Item> items{item("control.txt", "control")};
            auto host = item(streamOwner, directory ? "" : "archive bytes");
            host.Directory = directory;
            items.append(host);
            if (directory) {
                items.append(item(collision, "archive bytes"));
                items.append(item(copied, "copied child"));
                items.append(item(secondCollision, "second archive bytes"));
            }
            auto stream = host;
            stream.Path += ":stream δ";
            stream.Bytes = "archive stream";
            stream.AltStream = true;
            stream.Directory = false;
            items.append(stream);
            if (directory) {
                stream.Path = streamOwner + ":other 空";
                stream.Bytes = "other archive stream";
                items.append(stream);
            }
            QVERIFY(NativeArchiveFixture::Write(archive, items, format));
            if (!directory) {
                const QString original = fixture.filePath("original WIM output");
                QVERIFY(run(fixture.path(), {"x", "-y", "-sns", "-o" + original, "--", archive}));
                QCOMPARE(SourceExtractFixture::readFile(original + '/' + streamOwner + ":stream δ"),
                         QByteArray("archive stream"));
            }
        } else
            QVERIFY(run(input, args));
        const QByteArray before = SourceExtractFixture::readFile(archive);
        NativePropertyFailureRuntime runtime;
        if (lateFailure) {
            QVERIFY(runtime.prepare(QString::fromUtf8(SEVENZIP_EXTERNAL_RUNTIME),
                                    QString::fromUtf8(SEVENZIP_EXTERNAL_FORMAT_FIXTURE)));
            QVERIFY(runtime.enableWriter());
        }
        ArchiveBackend backend(nullptr, lateFailure ? runtime.engine() : QString());
        if (streams)
            QCOMPARE(NativeArchiveFixture::Attach(backend), S_OK);
        if (lateFailure) {
            QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
            QVERIFY(backend.discoverCodecCatalog(runtime.module()));
            QTRY_COMPARE(ready.size(), 1);
            QVERIFY2(ready[0][0].toBool(), qPrintable(ready[0][1].toString()));
        }
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        if (!action(window, "twoPanelsAction")->isChecked())
            action(window, "twoPanelsAction")->trigger();
        std::array<FileTable *, 2> views;
        const int targetRows =
            QDir(output).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size();
        for (unsigned i : {0u, 1u}) {
            auto *ui = window.findChild<QWidget *>(QString("filePanel%1").arg(i));
            views[i] = ui->findChild<FileTable *>("archiveTable");
            auto *address = ui->findChild<QLineEdit *>("archivePath");
            address->setFocus();
            address->setText(i == 0 ? archive : output);
            QTest::keyClick(address, Qt::Key_Return);
            QTRY_VERIFY(views[i]->isEnabled());
            QTRY_COMPARE(views[i]->rowCount(), i == 0 ? 2 : targetRows);
            views[i]->sourcePanel->Set_AutoRefresh_Mode(false);
        }
        int row = -1;
        for (int i = 0; i < views[0]->rowCount(); ++i)
            if (views[0]->model()->index(i, 0).data().toString() == selection)
                row = i;
        QVERIFY(row >= 0);
        views[0]->setCurrentCell(row, 0);
        views[0]->selectRow(row);
        QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        views[0]->setFocus();
        QTRY_VERIFY(views[0]->hasFocus());
        bool submitted = false;
        int overwriteQuestions = 0;
        bool deletionAsked = false;
        QStringList errors;
        QTimer responder;
        dialogs_.connectResponder(responder, &window, [&] {
            auto *modal = QApplication::activeModalWidget();
            if (auto *copy = dynamic_cast<CopyDialog *>(modal); copy && !submitted) {
                const auto dismiss = qScopeGuard([&] {
                    if (copy->isVisible())
                        copy->reject();
                });
                if (lateFailure)
                    QVERIFY(runtime.arm(UInt32(-1), E_FAIL, 1));
                submitted = true;
                copy->accept();
            } else if (auto *overwrite = dynamic_cast<COverwriteDialog *>(modal)) {
                ++overwriteQuestions;
                if (noToAll)
                    ReplyOverwrite(*overwrite, IDB_NO_TO_ALL);
                else
                    overwrite->findChild<QPushButton *>(answer)->click();
            } else if (auto *confirmation = dynamic_cast<QtDeleteConfirmation *>(modal)) {
                deletionAsked = true;
                ReplyDelete(*confirmation);
            } else if (auto *box = qobject_cast<QMessageBox *>(modal)) {
                errors << box->text();
                box->accept();
            } else if (auto *progress = dynamic_cast<CProgressDialog *>(modal)) {
                auto *close = progress->findChild<QPushButton *>("IDCANCEL");
                if (close && close->text() == ToQString(LangString(IDS_CLOSE))) {
                    auto *messages =
                        progress->findChild<QAbstractItemView *>("IDL_PROGRESS_MESSAGES");
                    if (messages)
                        for (int row = 0; row < messages->model()->rowCount(); ++row) {
                            const QString message =
                                messages->model()->index(row, 1).data().toString();
                            if (!message.isEmpty())
                                errors.append(message);
                        }
                    close->click();
                }
            }
        });
        responder.start(2);
        action(window, "moveAction")->trigger();
        responder.stop();
        QVERIFY(submitted);
        int expectedQuestions = directory && !noToAll ? 2 : 1;
        if (streams) {
            expectedQuestions = directory ? 4 : 2;
#ifdef Q_OS_WIN
            // Replacing the host also removes its old streams on Windows.
            if (!directory)
                expectedQuestions = 1;
#endif
        }
        if (lateFailure) {
            QVERIFY(!runtime.openReceipts().isEmpty());
            QVERIFY2(window.statusBar()->currentMessage().contains("Check the source archive"),
                     qPrintable(window.statusBar()->currentMessage()));
            QVERIFY(runtime.clear());
        } else
            QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
        QCOMPARE(overwriteQuestions, expectedQuestions);
        QCOMPARE(SourceExtractFixture::readFile(QDir(output).filePath(collision)),
                 QByteArray(accept ? "archive bytes" : "existing bytes"));
        if (streams)
            QCOMPARE(SourceExtractFixture::readFile(output + '/' + streamOwner + ":stream δ"),
                     QByteArray("archive stream"));
        if (streams && directory)
            QCOMPARE(SourceExtractFixture::readFile(output + '/' + streamOwner + ":other 空"),
                     QByteArray("other archive stream"));
        if (directory) {
            QCOMPARE(SourceExtractFixture::readFile(QDir(output).filePath(copied)),
                     QByteArray("copied child"));
            QCOMPARE(SourceExtractFixture::readFile(QDir(output).filePath(secondCollision)),
                     QByteArray(accept ? "second archive bytes" : "second existing bytes"));
        }
        const QString verified = fixture.filePath("verified source");
        QVERIFY(run(fixture.path(), {"x", "-y", "-o" + verified, "--", archive}));
        QCOMPARE(SourceExtractFixture::readFile(QDir(verified).filePath("control.txt")),
                 QByteArray("control"));
        QCOMPARE(QFileInfo::exists(QDir(verified).filePath(collision)), !accept);
        if (!accept) {
            QCOMPARE(SourceExtractFixture::readFile(QDir(verified).filePath(collision)),
                     QByteArray("archive bytes"));
            QCOMPARE(SourceExtractFixture::readFile(archive), before);
        }
        QVERIFY(!deletionAsked);
    }
};

QTEST_MAIN(ArchiveMoveCompletionTests)
#include "archive_move_completion_tests.moc"
