#include "7zip/UI/FileManager/ProgressDialog2Res.h"
#include "archive/resources/archivenativeopen.h"
#include "archive/support/native_archive_fixture.h"
#include "archive/support/source_extract_fixture.h"
#include "dialogs/copy/alias.h"
#include "dialogs/overwrite/OverwriteDialog.h"
#include "filemanager/support/window_state_support.h"
#include "fileops/links/qtlinkservices.h"
#include "localization/LangUtils.h"
#include "platform/support/native_link_fixture.h"
#include "settings/RegistryUtils.h"
#include "support/widget_dialogs.h"
#include "ui/progress/ProgressDialog2.h"
#include "ui/support/layout_inspection.h"
#include <QScopeGuard>

class InterarchiveStagingTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    WidgetDialogFixture dialogs_;
    static NativeArchiveFixture::Item file(const QString &path, const QByteArray &bytes = "x",
                                           bool stream = false) {
        NativeArchiveFixture::Item item;
        item.Path = path;
        item.Bytes = bytes;
        item.AltStream = stream;
        return item;
    }
    static bool extract(const QString &archive, const QString &output) {
        QProcess process;
        process.start(QStandardPaths::findExecutable("7z"),
                      {"x", "-y", "-sns", "-o" + output, "--", archive});
        return process.waitForFinished(10000) && process.exitStatus() == QProcess::NormalExit &&
               process.exitCode() == 0;
    }

  private slots:
    void initTestCase() {
#ifdef Q_OS_WIN
        // QTEST_MAIN bypasses the application's native link initialization.
        QtInitializeLinkPlatform();
#endif
    }
    void movePreservesStagedObjects_data() {
        QTest::addColumn<QString>("kind");
        QTest::addColumn<QString>("targetFormat");
        QTest::addColumn<bool>("accepted");
        QTest::newRow("file-and-directory-links") << QString("symlinks") << QString("tar") << true;
        QTest::newRow("data-encoded-links") << QString("zip-symlinks") << QString("tar") << true;
        QTest::newRow("hard-links") << QString("hardlinks") << QString("tar") << true;
        QTest::newRow("file-streams") << QString("streams") << QString("wim") << true;
        QTest::newRow("directory-streams") << QString("nested-streams") << QString("wim") << true;
        QTest::newRow("replace-host-and-streams")
            << QString("replace-streams") << QString("wim") << true;
        QTest::newRow("unsupported-streams") << QString("streams") << QString("zip") << false;
        QTest::newRow("unsupported-streams-ru") << QString("streams-ru") << QString("zip") << false;
        QTest::newRow("duplicate-file") << QString("duplicate") << QString("zip") << false;
        QTest::newRow("directory-link-collision")
            << QString("directory-link-collision") << QString("tar") << false;
        QTest::newRow("archive-root-symlink")
            << QString("absolute-link") << QString("tar") << false;
        QTest::newRow("case-alias-directories") << QString("case-alias") << QString("zip") << true;
        QTest::newRow("normalized-directories")
            << QString("normalized-directories") << QString("zip") << true;
    }

    void movePreservesStagedObjects() {
        QFETCH(QString, kind);
        QFETCH(QString, targetFormat);
        QFETCH(bool, accepted);
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settings);
        const bool russian = kind.endsWith("-ru");
        if (russian)
            kind.chop(3);
        SaveRegLang(russian ? L"ru" : L"-", settings);
        ReloadLang(settings);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        if (kind == "case-alias") {
            QVERIFY(QDir().mkpath(fixture.filePath("case probe/A")));
            accepted = !QFileInfo::exists(fixture.filePath("case probe/a"));
        }
#ifdef Q_OS_WIN
        if (kind == "normalized-directories")
            accepted = false;
#endif
        using NativeArchiveFixture::Item;
        QList<Item> items{file("control.txt", "source control")};
        QString sourceFormat = "tar";
        const bool streams = kind.contains("streams");
        const bool hostSelection = streams && kind != "nested-streams";
        const QString selection = hostSelection ? QStringLiteral("host 空.bin") : "tree";
        if (streams) {
            sourceFormat = "wim";
            const QString host = hostSelection ? selection : "tree/host 空.bin";
            items.append(file(host, "main bytes"));
            items.append(file(host + QStringLiteral(":stream δ"), "stream bytes", true));
            items.append(file(host + QStringLiteral(":other 空"), "other bytes", true));
        } else if (kind == "symlinks" || kind == "zip-symlinks") {
            if (kind == "zip-symlinks")
                sourceFormat = "zip";
            items.append(file("tree/a.txt", "original bytes"));
            items.append(file("tree/sub/z.txt", "nested bytes"));
            Item fileLink = file("tree/file-link");
            fileLink.SymLink = "a.txt";
            items.append(fileLink);
            Item directoryLink = file("tree/directory-link");
            directoryLink.SymLink = "sub";
            directoryLink.Directory = true;
            items.append(directoryLink);
        } else if (kind == "hardlinks") {
            items.append(file("tree/a.txt", "original bytes"));
            Item link = file("tree/b.txt");
            link.HardLink = "tree/a.txt";
            items.append(link);
        } else if (kind == "duplicate") {
            sourceFormat = "zip";
            items.append(file("tree/a.txt", "first bytes"));
            items.append(file("tree/a.txt", "second bytes"));
        } else if (kind == "case-alias" || kind == "normalized-directories") {
            sourceFormat = "zip";
            const bool caseAlias = kind == "case-alias";
            items.append(file(caseAlias ? "tree/A/first.txt" : "tree/a?/first.txt", "first bytes"));
            items.append(
                file(caseAlias ? "tree/a/second.txt" : "tree/a*/second.txt", "second bytes"));
        } else if (kind == "absolute-link") {
            items.append(file("tree/sub/a.txt", "nested bytes"));
            Item link = file("tree/link");
            link.SymLink = "/tree/sub";
            link.Directory = true;
            items.append(link);
        } else {
            items.append(file("tree/sub/a.txt", "nested bytes"));
            items.append(file("tree/alias", "original bytes"));
            Item link = file("tree/alias");
            link.SymLink = "sub";
            link.Directory = true;
            items.append(link);
        }
        const QString source = fixture.filePath("source." + sourceFormat);
        const QString target = fixture.filePath("target." + targetFormat);
        if (kind == "zip-symlinks") {
            const QDir input(fixture.filePath("input"));
            QVERIFY(
                SourceExtractFixture::writeFile(input.filePath("control.txt"), "source control"));
            QVERIFY(
                SourceExtractFixture::writeFile(input.filePath("tree/a.txt"), "original bytes"));
            QVERIFY(
                SourceExtractFixture::writeFile(input.filePath("tree/sub/z.txt"), "nested bytes"));
            if (!CreateNativeSymbolicLink(input.filePath("tree/file-link"), "a.txt"))
                QSKIP("The fixture filesystem cannot create symbolic links.");
            QProcess creator;
            creator.setWorkingDirectory(input.path());
            creator.start(QStandardPaths::findExecutable("7z"),
                          {"a", "-tzip", "-snl", "--", source, "tree", "control.txt"});
            QVERIFY(creator.waitForFinished(10000));
            QCOMPARE(creator.exitCode(), 0);
            const QDir original(fixture.filePath("original ZIP output"));
            QVERIFY(extract(source, original.path()));
            QVERIFY(QFileInfo(original.filePath("tree/file-link")).isSymLink());
        } else
            QVERIFY(NativeArchiveFixture::Write(source, items, sourceFormat));
        QList<Item> targetItems{file("target-control.txt", "target control")};
        if (kind == "replace-streams") {
            targetItems.append(file(selection, "old main bytes"));
            targetItems.append(file(selection + ":stream δ", "old stream bytes", true));
            targetItems.append(file(selection + ":stale", "stale bytes", true));
        }
        QVERIFY(NativeArchiveFixture::Write(target, targetItems, targetFormat));
        const QByteArray sourceBefore = SourceExtractFixture::readFile(source);
        const QByteArray targetBefore = SourceExtractFixture::readFile(target);
        ArchiveBackend backend;
        QCOMPARE(NativeArchiveFixture::Attach(backend), S_OK);
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        if (!action(window, "twoPanelsAction")->isChecked())
            action(window, "twoPanelsAction")->trigger();
        std::array<FileTable *, 2> views;
        for (unsigned i : {0u, 1u}) {
            auto *panel = window.findChild<QWidget *>(QString("filePanel%1").arg(i));
            auto *address = panel->findChild<QLineEdit *>("archivePath");
            views[i] = panel->findChild<FileTable *>("archiveTable");
            address->setFocus();
            address->setText(i == 0 ? source : target);
            QTest::keyClick(address, Qt::Key_Return);
            QTRY_VERIFY(views[i]->isEnabled());
            QTRY_COMPARE(views[i]->rowCount(), i == 0 || kind == "replace-streams" ? 2 : 1);
            views[i]->sourcePanel->Set_AutoRefresh_Mode(false);
        }
        int selected = -1;
        for (int row = 0; row < views[0]->rowCount(); ++row)
            if (views[0]->model()->index(row, 0).data().toString() == selection)
                selected = row;
        QVERIFY(selected >= 0);
        views[0]->setCurrentCell(selected, 0);
        views[0]->selectRow(selected);
        QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        views[0]->setFocus();
        QTRY_VERIFY(views[0]->hasFocus());
        bool submitted = false, confirmed = false, overwrite = false;
        QStringList errors;
        QTimer responder;
        dialogs_.connectResponder(responder, &window, [&] {
            auto *modal = QApplication::activeModalWidget();
            if (auto *copy = dynamic_cast<CopyDialog *>(modal); copy && !submitted) {
                submitted = true;
                copy->accept();
            } else if (auto *question = dynamic_cast<COverwriteDialog *>(modal)) {
                overwrite = true;
                question->findChild<QPushButton *>("overwriteYes")->click();
            } else if (auto *box = qobject_cast<QMessageBox *>(modal)) {
                const auto dismiss = qScopeGuard([&] {
                    if (box->isVisible())
                        box->reject();
                });
                if (box->button(QMessageBox::Yes)) {
                    confirmed = true;
                    box->button(QMessageBox::Yes)->click();
                } else {
                    errors << box->text();
                    for (const QSize size : {QSize(440, 220), QSize(340, 220)}) {
                        box->resize(size);
                        QApplication::processEvents();
                        auto *label = box->findChild<QLabel *>("qt_msgbox_label");
                        auto *ok = box->button(QMessageBox::Ok);
                        QVERIFY(label && ok);
                        QVERIFY(UiInspection::fullyVisible(*label, *box));
                        QVERIFY(UiInspection::fullyVisible(*ok, *box));
                    }
                    box->accept();
                }
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
        QVERIFY(!overwrite);
        QCOMPARE(confirmed, accepted);
        if (!accepted) {
            QVERIFY(!errors.isEmpty());
            if (russian)
                QVERIFY(errors.last().contains(QStringLiteral("Формат целевого архива")));
            QCOMPARE(SourceExtractFixture::readFile(source), sourceBefore);
            QCOMPARE(SourceExtractFixture::readFile(target), targetBefore);
            return;
        }
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
        const QString sourceOut = fixture.filePath("source output");
        const QString targetOut = fixture.filePath("target output");
        QVERIFY(extract(source, sourceOut));
        QVERIFY(extract(target, targetOut));
        QCOMPARE(SourceExtractFixture::readFile(QDir(sourceOut).filePath("control.txt")),
                 QByteArray("source control"));
        QVERIFY(!QFileInfo::exists(QDir(sourceOut).filePath(selection)));
        QCOMPARE(SourceExtractFixture::readFile(QDir(targetOut).filePath("target-control.txt")),
                 QByteArray("target control"));
        const QDir output(targetOut);
        if (streams) {
            const QString host = hostSelection ? selection : "tree/host 空.bin";
            QCOMPARE(SourceExtractFixture::readFile(output.filePath(host)),
                     QByteArray("main bytes"));
            QCOMPARE(SourceExtractFixture::readFile(output.filePath(host + ":stream δ")),
                     QByteArray("stream bytes"));
            QCOMPARE(SourceExtractFixture::readFile(output.filePath(host + ":other 空")),
                     QByteArray("other bytes"));
            QVERIFY(!QFileInfo::exists(output.filePath(host + ":stale")));
            std::shared_ptr<ArchiveNativeOpen> reader;
            QCOMPARE(backend.openNativeArchive(target, targetFormat, nullptr, reader), S_OK);
            UInt32 count = 0;
            QCOMPARE(reader->arc().Archive->GetNumberOfItems(&count), S_OK);
            QStringList recordedStreams;
            for (UInt32 i = 0; i < count; ++i) {
                NWindows::NCOM::CPropVariant isStream;
                QCOMPARE(reader->arc().Archive->GetProperty(i, kpidIsAltStream, &isStream), S_OK);
                if (isStream.vt == VT_BOOL && isStream.boolVal) {
                    NWindows::NCOM::CPropVariant path;
                    QCOMPARE(reader->arc().Archive->GetProperty(i, kpidPath, &path), S_OK);
                    QCOMPARE(path.vt, VARTYPE(VT_BSTR));
                    recordedStreams.append(
                        QDir::fromNativeSeparators(QString::fromWCharArray(path.bstrVal)));
                }
            }
            recordedStreams.sort();
            QStringList expected{host + ":other 空", host + ":stream δ"};
            expected.sort();
            QCOMPARE(recordedStreams, expected);
        } else if (kind == "case-alias" || kind == "normalized-directories") {
            const bool caseAlias = kind == "case-alias";
            QCOMPARE(SourceExtractFixture::readFile(
                         output.filePath(caseAlias ? "tree/A/first.txt" : "tree/a?/first.txt")),
                     QByteArray("first bytes"));
            QCOMPARE(SourceExtractFixture::readFile(
                         output.filePath(caseAlias ? "tree/a/second.txt" : "tree/a*/second.txt")),
                     QByteArray("second bytes"));
        } else {
            QCOMPARE(SourceExtractFixture::readFile(output.filePath("tree/a.txt")),
                     QByteArray("original bytes"));
            if (kind == "hardlinks")
                QVERIFY(NativeFilesShareIdentity(output.filePath("tree/a.txt"),
                                                 output.filePath("tree/b.txt")));
            else {
                QVERIFY(QFileInfo(output.filePath("tree/file-link")).isSymLink());
                if (kind == "symlinks") {
                    QVERIFY(QFileInfo(output.filePath("tree/directory-link")).isSymLink());
                    QCOMPARE(SourceExtractFixture::readFile(
                                 output.filePath("tree/directory-link/z.txt")),
                             QByteArray("nested bytes"));
                }
            }
        }
    }
};

QTEST_MAIN(InterarchiveStagingTests)
#include "interarchive_staging_tests.moc"
