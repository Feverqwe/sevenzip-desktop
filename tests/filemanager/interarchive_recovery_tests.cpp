#include "7zip/UI/FileManager/ProgressDialog2Res.h"
#include "archive/agent/Agent.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/support/source_extract_fixture.h"
#include "dialogs/copy/alias.h"
#include "filemanager/qtfilemanagerstartup.h"
#include "filemanager/support/selection_fixture.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "platform/support/native_property_failure_runtime.h"
#include "support/delete_reply.h"
#include "support/panel_keys.h"
#include "support/progress_dialog.h"
#include "support/signal_wait_support.h"
#include "ui/progress/ProgressDialog2.h"
#include "ui/support/layout_inspection.h"

class InterarchiveRecoveryTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    QString engine_;
    NativePropertyFailureRuntime runtime_;
    QTemporaryDir seeds_;

    void createArchive(const QString &path, const QString &root, const QStringList &names,
                       const QString &format = "zip") {
        QProcess process;
        process.setWorkingDirectory(root);
        process.start(engine_, QStringList{"a", "-t" + format, "--", path} + names);
        QVERIFY(process.waitForFinished(10000));
        QCOMPARE(process.exitCode(), 0);
    }
    void verifyArchive(const QString &path, const QString &output, const QStringList &names) {
        QProcess process;
        process.start(engine_, {"x", "-y", "-o" + output, "--", path});
        QVERIFY(process.waitForFinished(10000));
        QCOMPARE(process.exitCode(), 0);
        QCOMPARE(QDir(output).entryList(QDir::Files).size(), names.size());
        for (const QString &name : names)
            QCOMPARE(SourceExtractFixture::readFile(QDir(output).filePath(name)), name.toUtf8());
    }

  private slots:
    void initTestCase() {
        engine_ = QStandardPaths::findExecutable("7z");
        QVERIFY(!engine_.isEmpty());
        QVERIFY(seeds_.isValid());
        const QString added = QStringLiteral("a copied 文件.txt");
        const QString control = QStringLiteral("zz source control δ.bin");
        const QString marked = QStringLiteral("m marked δ.txt");
        const QString focused = QStringLiteral("z focused 文件.bin");
        for (const QString &name : {added, control, marked, focused})
            QVERIFY(SourceExtractFixture::writeFile(seeds_.filePath(name), name.toUtf8()));
        createArchive(seeds_.filePath("source.zip"), seeds_.path(), {added, control});
        createArchive(seeds_.filePath("target.zip"), seeds_.path(), {marked, focused}, "7z");
        for (int count : {1, 301}) {
            const QString home =
                seeds_.filePath(QStringLiteral("fallback directory %1 文件").arg(count));
            QVERIFY(QDir().mkdir(home));
            for (int i = 0; i < count; ++i)
                QVERIFY(SourceExtractFixture::writeFile(QDir(home).filePath(QString::number(i)),
                                                        QByteArray("fallback")));
        }
        QVERIFY(runtime_.prepare(QString::fromUtf8(SEVENZIP_EXTERNAL_RUNTIME),
                                 QString::fromUtf8(SEVENZIP_EXTERNAL_FORMAT_FIXTURE)));
        QVERIFY(runtime_.enableWriter());
    }
    void rollbackFailureAndRetry_data() {
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<bool>("russian");
        QTest::addColumn<bool>("icons");
        QTest::addColumn<QString>("route");
        QStringList routes{"restore", "restore-large", "reopen-failure", "reopen-cancel"};
#ifdef SEVENZIP_CODEC_MODULE
        routes << "native-restore";
#endif
        for (unsigned pane : {0u, 1u})
            for (bool russian : {false, true})
                for (bool icons : {false, true})
                    for (const QString &route : routes)
                        QTest::newRow(qPrintable(
                            QString("%1-%2-%3-%4").arg(pane).arg(russian).arg(icons).arg(route)))
                            << pane << russian << icons << route;
#ifdef SEVENZIP_CODEC_MODULE
        QTest::newRow("native-reopen-failure")
            << 0u << false << false << QString("native-reopen-failure");
        QTest::newRow("native-reopen-cancel")
            << 1u << true << true << QString("native-reopen-cancel");
        QTest::newRow("native-restore-large")
            << 1u << false << false << QString("native-restore-large");
#endif
    }
    void rollbackFailureAndRetry() {
        QFETCH(unsigned, pane);
        QFETCH(bool, russian);
        QFETCH(bool, icons);
        QFETCH(QString, route);
        const bool restoreFailure = route.contains("restore");
        const int fallbackCount = route.endsWith("restore-large") ? 301 : 1;
        const QString settingsPath = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settingsPath);
        SaveRegLang(UString(russian ? L"ru" : L"-"), settingsPath);
        ReloadLang(settingsPath);
        QSettings settings(settingsPath, QSettings::IniFormat);
        settings.setValue("FM/ShowDots", false);
        settings.sync();
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString added = QStringLiteral("a copied 文件.txt");
        const QString control = QStringLiteral("zz source control δ.bin");
        const QString marked = QStringLiteral("m marked δ.txt");
        const QString focused = QStringLiteral("z focused 文件.bin");
        const QString home =
            seeds_.filePath(QStringLiteral("fallback directory %1 文件").arg(fallbackCount));
        const QString sourceArchive = fixture.filePath("source archive.zip");
        const QString targetArchive = fixture.filePath("target archive.fixture7z");
        QVERIFY(QFile::copy(seeds_.filePath("source.zip"), sourceArchive));
        QVERIFY(QFile::copy(seeds_.filePath("target.zip"), targetArchive));
        const QByteArray sourceBefore = SourceExtractFixture::readFile(sourceArchive);
        const QByteArray targetBefore = SourceExtractFixture::readFile(targetArchive);
        QVERIFY(runtime_.clear());
        const auto clearFault = qScopeGuard([&] { runtime_.clear(); });
        ArchiveBackend backend(nullptr, runtime_.engine());
        if (route.startsWith("native-")) {
            QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
            QVERIFY(backend.discoverCodecCatalog(runtime_.module()));
            QTRY_COMPARE(ready.size(), 1);
            QVERIFY2(ready[0][0].toBool(), qPrintable(ready[0][1].toString()));
        }
        CWindowInfo windowInfo{};
        windowInfo.rect.right = 980;
        windowInfo.rect.bottom = 620;
        windowInfo.numPanels = 2;
        windowInfo.currentPanel = pane;
        windowInfo.splitterPos = 490;
        windowInfo.Save(settingsPath);
        for (unsigned index : {0u, 1u})
            SavePanelPath(index, ToUString(home), settingsPath);
        QtFileManagerStartup startup;
        startup.FullPath = ToUString(sourceArchive);
        startup.NeedOpenArc = true;
        MainWindow window(&backend, {}, nullptr, &startup);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *sourceUi = window.findChild<QWidget *>(QString("filePanel%1").arg(pane));
        auto *targetUi = window.findChild<QWidget *>(QString("filePanel%1").arg(1 - pane));
        for (QWidget *ui : {targetUi, sourceUi}) {
            auto *table = ui->findChild<FileTable *>("archiveTable");
            const int initialCount = ui == targetUi ? fallbackCount : 2;
            QVERIFY(SignalWaitSupport::waitForCount(*table->model(), initialCount));
            QCOMPARE(table->rowCount(), initialCount);
            QTRY_VERIFY(table->isEnabled() && table->updatesEnabled());
        }
        // Finish both initial publications before choosing an operation owner;
        // the source initial-focus request can otherwise replace this focus.
        QVERIFY(SignalWaitSupport::waitForEnabled(*action(window, "refreshAction")));
        QVERIFY(action(window, "refreshAction")->isEnabled());
        // The source is opened by the ordinary startup path. Only the target
        // needs a filesystem-to-archive transition to retain its fallback folder.
        for (QWidget *ui : {targetUi}) {
            auto *address = ui->findChild<QLineEdit *>("archivePath");
            QCOMPARE(QFileInfo(address->text()).canonicalFilePath(),
                     QFileInfo(home).canonicalFilePath());
            QVERIFY(SignalWaitSupport::waitForEnabled(*action(window, "refreshAction")));
            QVERIFY(action(window, "refreshAction")->isEnabled());
            window.activateWindow();
            QVERIFY(QTest::qWaitForWindowActive(&window));
            PanelKeys::focusAddress(address);
            QVERIFY(ui->isAncestorOf(window.focusWidget()));
            auto *table = ui->findChild<FileTable *>("archiveTable");
            window.openArchive(targetArchive);
            QVERIFY2(SignalWaitSupport::waitForCount(*table->model(), 2),
                     qPrintable(QString("owner=%1 rows=%2 source=%3 target=%4 status=%5")
                                    .arg(ui->objectName())
                                    .arg(table->rowCount())
                                    .arg(sourceUi->findChild<QLineEdit *>("archivePath")->text(),
                                         targetUi->findChild<QLineEdit *>("archivePath")->text(),
                                         window.statusBar()->currentMessage())));
            QCOMPARE(table->rowCount(), 2);
            QTRY_VERIFY(table->isEnabled() && table->updatesEnabled() &&
                        static_cast<FileTable *>(table)->sourcePanel->ShouldProcessNotifications());
            QVERIFY(SignalWaitSupport::waitForEnabled(*action(window, "refreshAction")));
            QVERIFY(action(window, "refreshAction")->isEnabled());
            if (icons)
                action(window, "viewMode0Action")->trigger();
        }
        auto *source = static_cast<FileTable *>(sourceUi->findChild<FileTable *>("archiveTable"));
        auto *target = static_cast<FileTable *>(targetUi->findChild<FileTable *>("archiveTable"));
        CPanel &sourcePanel = *source->sourcePanel;
        CPanel &destination = *target->sourcePanel;
        for (CPanel *panel : {&sourcePanel, &destination})
            panel->Set_AutoRefresh_Mode(false);
        CMyComPtr<IFolderFolder> sourceProvider = sourcePanel.GetFolder();
        CMyComPtr<IFolderFolder> targetProvider = destination.GetFolder();
        QVERIFY(dynamic_cast<CAgentFolder *>(sourceProvider.Interface()));
        auto view = [icons](QWidget *ui) -> QAbstractItemView * {
            return icons ? static_cast<QAbstractItemView *>(ui->findChild<QListView *>("iconList"))
                         : ui->findChild<FileTable *>("archiveTable");
        };
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        PanelKeys::focusAddress(sourceUi->findChild<QLineEdit *>("archivePath"));
        if (icons)
            action(window, "viewMode0Action")->trigger();
        sourcePanel.SelectAll(false);
        MarkSelectionForTest(sourcePanel, ToUString(added));
        view(sourceUi)->setFocus();
        QVERIFY(sourceUi->isAncestorOf(window.focusWidget()));
        QStringList errors;
        bool submitted = false, refused = false, committed = false, replaced = false;
        bool reviewed = false;
        const QString sentinel = QDir(targetArchive).filePath("keep sentinel δ.txt");
        QTimer driver;
        connect(&driver, &QTimer::timeout, &window, [&] {
            auto *modal = QApplication::activeModalWidget();
            if (auto *dialog = dynamic_cast<CopyDialog *>(modal); dialog && !submitted) {
                submitted = true;
                dialog->accept();
            } else if (auto *box = dynamic_cast<QtDeleteConfirmation *>(modal)) {
                refused = true;
                committed = SourceExtractFixture::readFile(targetArchive) != targetBefore;
                if (restoreFailure) {
                    // Quiesce this private native fixture before replacing
                    // its read-shared Windows file with a sentinel directory.
                    auto *folder = dynamic_cast<CAgentFolder *>(destination.GetFolder());
                    const bool released = folder && folder->CloseArchive() == S_OK;
                    replaced = released && QFile::remove(targetArchive) &&
                               QDir().mkdir(targetArchive) &&
                               SourceExtractFixture::writeFile(sentinel, QByteArray("keep"));
                } else {
                    QVERIFY(runtime_.arm(0, route.endsWith("reopen-cancel") ? E_ABORT : E_FAIL, 2,
                                         kpidPath));
                }
                ReplyDelete(*box, false);
            } else if (auto *box = qobject_cast<QMessageBox *>(modal)) {
                errors << box->text();
                box->accept();
            } else if (auto *progress = dynamic_cast<CProgressDialog *>(modal)) {
                auto *close = progress->findChild<QPushButton *>("IDCANCEL");
                if (TestProgressDialog::IsCloseButton(close)) {
                    reviewed = true;
                    if (progress->Sync.FinalMessage.Result != E_ABORT)
                        errors << TestProgressDialog::ReportText(*progress);
                    close->click();
                }
            }
        });
        driver.start(1);
        QTest::keyClick(view(sourceUi), Qt::Key_F6);
        QTRY_VERIFY(reviewed);
        QVERIFY(submitted);
        QVERIFY(refused);
        QVERIFY(committed);
        QCOMPARE(replaced, restoreFailure);
        const QByteArray receipts = runtime_.receipts();
        const bool cancelled = receipts.contains(QByteArray::number(UInt32(E_ABORT)));
        QCOMPARE(cancelled, route.endsWith("reopen-cancel"));
        QVERIFY(SignalWaitSupport::waitForCount(*target->model(), fallbackCount));
        QCOMPARE(target->rowCount(), fallbackCount);
        QVERIFY(SignalWaitSupport::waitForEnabled(*action(window, "refreshAction")));
        QVERIFY(action(window, "refreshAction")->isEnabled());
        QCOMPARE(errors.size(), restoreFailure ? 2 : (cancelled ? 0 : 1));
        if (restoreFailure) {
            QVERIFY(errors.last().contains(russian ? QStringLiteral("откатить") : "roll back"));
            QCOMPARE(SourceExtractFixture::readFile(sentinel), QByteArray("keep"));
        } else {
            QVERIFY(!receipts.isEmpty());
            QCOMPARE(SourceExtractFixture::readFile(targetArchive), targetBefore);
        }
        QVERIFY(destination.GetFolder() != targetProvider.Interface());
        QVERIFY(destination.IsFSFolder());
        QCOMPARE(
            QFileInfo(targetUi->findChild<QLineEdit *>("archivePath")->text()).canonicalFilePath(),
            QFileInfo(home).canonicalFilePath());
        QCOMPARE(sourcePanel.GetFolder(), sourceProvider.Interface());
        QCOMPARE(SourceExtractFixture::readFile(sourceArchive), sourceBefore);
        QCOMPARE(source->rowCount(), 2);
        QVERIFY(sourceUi->isAncestorOf(window.focusWidget()));
        for (const QString &phase : {QString("failed"), QString("recovered")}) {
            if (phase == "recovered") {
                QVERIFY(runtime_.clear());
                if (restoreFailure) {
                    QVERIFY(QDir().rename(targetArchive, fixture.filePath("preserved directory")));
                    QVERIFY(SourceExtractFixture::writeFile(targetArchive, targetBefore));
                }
                window.activateWindow();
                QVERIFY(QTest::qWaitForWindowActive(&window));
                PanelKeys::focusAddress(targetUi->findChild<QLineEdit *>("archivePath"));
                QVERIFY(targetUi->isAncestorOf(window.focusWidget()));
                window.openArchive(targetArchive);
                QVERIFY(SignalWaitSupport::waitForCount(*target->model(), 2));
                QCOMPARE(target->rowCount(), 2);
                QVERIFY(SignalWaitSupport::waitForEnabled(*action(window, "refreshAction")));
                QVERIFY(action(window, "refreshAction")->isEnabled());
                window.activateWindow();
                QVERIFY(QTest::qWaitForWindowActive(&window));
                targetProvider = destination.GetFolder();
                destination.SelectAll(false);
                MarkSelectionForTest(destination, ToUString(marked));
                target->setCurrentCell(1, 0, QItemSelectionModel::NoUpdate);
                MarkSelectionForTest(sourcePanel, ToUString(added));
                view(sourceUi)->setFocus();
                submitted = false;
                errors.clear();
                QTest::keyClick(view(sourceUi), Qt::Key_F5);
                QVERIFY(submitted);
                QVERIFY(SignalWaitSupport::waitForCount(*target->model(), 3));
                QCOMPARE(target->rowCount(), 3);
                QVERIFY(SignalWaitSupport::waitForEnabled(*action(window, "refreshAction")));
                QVERIFY(action(window, "refreshAction")->isEnabled());
                QVERIFY(errors.isEmpty());
                QCOMPARE(destination.GetFolder(), targetProvider.Interface());
                CSelectedState selection;
                destination.SaveSelectedState(selection);
                QCOMPARE(selection.SelectedNames.Size(), 1u);
                QCOMPARE(ToQString(selection.SelectedNames[0]), marked);
                QCOMPARE(ToQString(selection.FocusedName), focused);
                verifyArchive(sourceArchive, fixture.filePath("verify source"), {added, control});
                verifyArchive(targetArchive, fixture.filePath("verify target"),
                              {added, marked, focused});
                if (restoreFailure)
                    QCOMPARE(SourceExtractFixture::readFile(
                                 fixture.filePath("preserved directory/keep sentinel δ.txt")),
                             QByteArray("keep"));
            }
            QCOMPARE(sourcePanel.GetFolder(), sourceProvider.Interface());
            QCOMPARE(SourceExtractFixture::readFile(sourceArchive), sourceBefore);
            CSelectedState selection;
            sourcePanel.SaveSelectedState(selection);
            if (phase == "failed") {
                QCOMPARE(selection.SelectedNames.Size(), 1u);
                QCOMPARE(ToQString(selection.SelectedNames[0]), added);
            } else
                QVERIFY(selection.SelectedNames.IsEmpty());
            QVERIFY(sourcePanel.ShouldProcessNotifications() &&
                    destination.ShouldProcessNotifications());
            QVERIFY(source->updatesEnabled() && target->updatesEnabled());
            QVERIFY(sourceUi->isAncestorOf(window.focusWidget()));
            for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
                window.resize(size);
                QApplication::processEvents();
                for (QWidget *ui : {sourceUi, targetUi}) {
                    QVERIFY(view(ui)->isEnabled());
                    QVERIFY(UiInspection::fullyVisible(*view(ui), window));
                }
                QVERIFY(UiInspection::writeReport(window, QString("interarchive-recovery-%1-%2-%3")
                                                              .arg(QTest::currentDataTag(), phase)
                                                              .arg(size.width())));
            }
        }
        driver.stop();
    }
};
QTEST_MAIN(InterarchiveRecoveryTests)
#include "interarchive_recovery_tests.moc"
