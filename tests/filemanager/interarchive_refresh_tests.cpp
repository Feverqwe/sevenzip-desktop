#include "7zip/UI/FileManager/ProgressDialog2Res.h"
#include "archive/agent/Agent.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/support/source_extract_fixture.h"
#include "dialogs/copy/alias.h"
#include "filemanager/support/selection_fixture.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "support/delete_reply.h"
#include "ui/progress/ProgressDialog2.h"
#include "ui/support/layout_inspection.h"

class InterarchiveRefreshTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    QString engine_;
    quint32 version_ = 0;

    void createArchive(const QString &path, const QString &directory, const QStringList &names) {
        QProcess process;
        process.setWorkingDirectory(directory);
        process.start(engine_, QStringList{"a", "--", path} + names);
        QVERIFY(process.waitForFinished(10000));
        QCOMPARE(process.exitCode(), 0);
    }
    void verifyArchive(const QString &path, const QString &output, const QStringList &names,
                       const QString &added, bool containsAdded) {
        QProcess process;
        process.start(engine_, {"x", "-y", "-o" + output, "--", path});
        QVERIFY(process.waitForFinished(10000));
        QCOMPARE(process.exitCode(), 0);
        for (const QString &name : names)
            QCOMPARE(SourceExtractFixture::readFile(QDir(output).filePath(name)), name.toUtf8());
        QCOMPARE(QFileInfo::exists(QDir(output).filePath(added)), containsAdded);
        if (containsAdded)
            QCOMPARE(SourceExtractFixture::readFile(QDir(output).filePath(added)), added.toUtf8());
    }

  private slots:
    void initTestCase() {
        engine_ = QStandardPaths::findExecutable("7z");
        QVERIFY(!engine_.isEmpty());
#ifdef SEVENZIP_CODEC_MODULE
        ArchiveCodecCatalog catalog(QString::fromUtf8(SEVENZIP_CODEC_MODULE), 0);
        QCOMPARE(catalog.result(), S_OK);
        version_ = catalog.version();
#endif
    }
    void transferPublishesDestination_data() {
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<bool>("russian");
        QTest::addColumn<bool>("icons");
        QTest::addColumn<bool>("alternative");
        QTest::addColumn<bool>("move");
        QTest::addColumn<QString>("route");
        QStringList routes{"delete-no", "delete-refuse"};
#ifdef SEVENZIP_CODEC_MODULE
        routes << "zip" << "7z" << "large-zip";
#endif
        for (unsigned pane : {0u, 1u})
            for (bool russian : {false, true})
                for (bool icons : {false, true})
                    for (bool alternative : {false, true})
                        for (bool move : {false, true})
                            for (const QString &route : routes) {
                                if (!move && route.startsWith("delete-"))
                                    continue;
                                QTest::newRow(qPrintable(QString("%1-%2-%3-%4-%5-%6")
                                                             .arg(pane)
                                                             .arg(russian)
                                                             .arg(icons)
                                                             .arg(alternative)
                                                             .arg(move)
                                                             .arg(route)))
                                    << pane << russian << icons << alternative << move << route;
                            }
    }
    void transferPublishesDestination() {
        QFETCH(unsigned, pane);
        QFETCH(bool, russian);
        QFETCH(bool, icons);
        QFETCH(bool, alternative);
        QFETCH(bool, move);
        QFETCH(QString, route);
        const bool rollback = route.startsWith("delete-");
        const bool success = !rollback;
        const int originalCount = route == "large-zip" ? 301 : 2;
        const QString settingsPath = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settingsPath);
        SaveRegLang(UString(russian ? L"ru" : L"-"), settingsPath);
        ReloadLang(settingsPath);
        QSettings settings(settingsPath, QSettings::IniFormat);
        settings.setValue("FM/AlternativeSelection", alternative);
        settings.setValue("FM/ShowDots", false);
        settings.sync();
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString added = QStringLiteral("a copied 文件.txt");
        const QString control = QStringLiteral("zz source control δ.bin");
        const QString marked = QStringLiteral("m marked δ.txt");
        const QString focused = QStringLiteral("z focused 文件.bin");
        QStringList targetNames{marked, focused};
        for (int i = 2; i < originalCount; ++i)
            targetNames << QString("seed-%1.txt").arg(i);
        for (const QString &name : targetNames + QStringList{added, control})
            QVERIFY(SourceExtractFixture::writeFile(fixture.filePath(name), name.toUtf8()));
        const QString format = route == "7z" ? route : "zip";
        const QString sourceArchive = fixture.filePath("source archive." + format);
        const QString targetArchive = fixture.filePath("target archive." + format);
        createArchive(sourceArchive, fixture.path(), {added, control});
        createArchive(targetArchive, fixture.path(), targetNames);
        const QByteArray sourceBefore = SourceExtractFixture::readFile(sourceArchive);
        const QByteArray targetBefore = SourceExtractFixture::readFile(targetArchive);
        ArchiveBackend backend;
#ifdef SEVENZIP_CODEC_MODULE
        QCOMPARE(backend.loadCodecCatalog(QString::fromUtf8(SEVENZIP_CODEC_MODULE), version_),
                 S_OK);
#endif
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        action(window, "twoPanelsAction")->trigger();
        auto *sourceUi = window.findChild<QWidget *>(QString("filePanel%1").arg(pane));
        auto *targetUi = window.findChild<QWidget *>(QString("filePanel%1").arg(1 - pane));
        for (QWidget *ui : {targetUi, sourceUi}) {
            window.activateWindow();
            QVERIFY(QTest::qWaitForWindowActive(&window));
            ui->findChild<QLineEdit *>("archivePath")->setFocus();
            window.openArchive(ui == targetUi ? targetArchive : sourceArchive);
            auto *list = ui->findChild<FileTable *>("archiveTable");
            QTRY_COMPARE(list->rowCount(), ui == targetUi ? originalCount : 2);
            QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
            if (icons)
                action(window, "viewMode0Action")->trigger();
        }
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *source = static_cast<FileTable *>(sourceUi->findChild<FileTable *>("archiveTable"));
        auto *target = static_cast<FileTable *>(targetUi->findChild<FileTable *>("archiveTable"));
        auto &sourcePanel = *source->sourcePanel;
        auto &destination = *target->sourcePanel;
        for (CPanel *panel : {&sourcePanel, &destination})
            panel->Set_AutoRefresh_Mode(false);
        CMyComPtr<IFolderFolder> sourceProvider = sourcePanel.GetFolder();
        CMyComPtr<IFolderFolder> targetProvider = destination.GetFolder();
        QVERIFY(dynamic_cast<CAgentFolder *>(sourceProvider.Interface()));
        QVERIFY(dynamic_cast<CAgentFolder *>(targetProvider.Interface()));
        destination.SelectAll(false);
        MarkSelectionForTest(destination, ToUString(marked));
        target->setCurrentCell(originalCount - 1, 0, QItemSelectionModel::NoUpdate);
        CSelectedState selection;
        destination.SaveSelectedState(selection);
        QCOMPARE(selection.SelectedNames.Size(), 1u);
        sourcePanel.SelectAll(false);
        MarkSelectionForTest(sourcePanel, ToUString(added));
        QAbstractItemView *view =
            icons ? static_cast<QAbstractItemView *>(sourceUi->findChild<QListView *>("iconList"))
                  : source;
        QAbstractItemView *targetView =
            icons ? static_cast<QAbstractItemView *>(targetUi->findChild<QListView *>("iconList"))
                  : target;
        view->setFocus();
        QVERIFY(sourceUi->isAncestorOf(window.focusWidget()));
        QSignalSpy publication(target->model(), &QAbstractItemModel::rowsRemoved);
        QStringList errors;
        bool submitted = false, deleteConfirmed = false, destinationCommitted = false;
        bool reviewed = false;
        QTimer driver;
        connect(&driver, &QTimer::timeout, &window, [&] {
            auto *modal = QApplication::activeModalWidget();
            if (auto *dialog = dynamic_cast<CopyDialog *>(modal); dialog && !submitted) {
                submitted = true;
                dialog->accept();
            } else if (auto *box = dynamic_cast<QtDeleteConfirmation *>(modal)) {
                deleteConfirmed = true;
                destinationCommitted =
                    SourceExtractFixture::readFile(targetArchive) != targetBefore;
                ReplyDelete(*box, route != "delete-no" && route != "delete-refuse");
            } else if (auto *box = qobject_cast<QMessageBox *>(modal)) {
                errors << box->text();
                box->accept();
            } else if (auto *progress = dynamic_cast<CProgressDialog *>(modal)) {
                auto *close = progress->findChild<QPushButton *>("IDCANCEL");
                if (close && close->text() == ToQString(LangString(IDS_CLOSE))) {
                    reviewed = true;
                    close->click();
                }
            }
        });
        driver.start(1);
        QTest::keyClick(view, move ? Qt::Key_F6 : Qt::Key_F5);
        if (rollback)
            QTRY_VERIFY(reviewed);
        QVERIFY(submitted);
        QTRY_COMPARE(target->rowCount(), originalCount + (success ? 1 : 0));
        QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
        driver.stop();
        QCOMPARE(deleteConfirmed, move && (success || rollback));
        QCOMPARE(destinationCommitted, deleteConfirmed);
        QVERIFY(errors.isEmpty());
        QCOMPARE(sourcePanel.GetFolder(), sourceProvider.Interface());
        if (rollback)
            QVERIFY(destination.GetFolder() != targetProvider.Interface());
        else
            QCOMPARE(destination.GetFolder(), targetProvider.Interface());
        QVERIFY(!publication.isEmpty());
        CSelectedState restored;
        destination.SaveSelectedState(restored);
        QCOMPARE(restored.SelectedNames.Size(), 1u);
        QCOMPARE(ToQString(restored.SelectedNames[0]), marked);
        QCOMPARE(ToQString(restored.FocusedName), ToQString(selection.FocusedName));
        CSelectedState sourceSelection;
        sourcePanel.SaveSelectedState(sourceSelection);
        QVERIFY(sourceSelection.SelectedNames.IsEmpty());
        QCOMPARE(source->rowCount(), success && move ? 1 : 2);
        QVERIFY(sourcePanel.ShouldProcessNotifications() &&
                destination.ShouldProcessNotifications());
        QVERIFY(source->updatesEnabled() && target->updatesEnabled());
        QVERIFY(view->isEnabled() && targetView->isEnabled());
        QVERIFY(sourceUi->isAncestorOf(window.focusWidget()));
        if (!(success && move))
            QCOMPARE(SourceExtractFixture::readFile(sourceArchive), sourceBefore);
        if (!success)
            QCOMPARE(SourceExtractFixture::readFile(targetArchive), targetBefore);
        verifyArchive(sourceArchive, fixture.filePath("verify source"), {control}, added,
                      !(success && move));
        verifyArchive(targetArchive, fixture.filePath("verify target"), targetNames, added,
                      success);
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(size);
            QApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(*view, window));
            QVERIFY(UiInspection::fullyVisible(*targetView, window));
            QVERIFY(UiInspection::writeReport(window, QString("interarchive-refresh-%1-%2")
                                                          .arg(QTest::currentDataTag())
                                                          .arg(size.width())));
        }
    }
};
QTEST_MAIN(InterarchiveRefreshTests)
#include "interarchive_refresh_tests.moc"
