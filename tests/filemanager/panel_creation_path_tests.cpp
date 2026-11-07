#include "archive/support/source_extract_fixture.h"
#include "filemanager/qtfilemanagerstartup.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "support/panel_keys.h"
#include "support/signal_wait_support.h"
#include "ui/support/layout_inspection.h"

class PanelCreationPathTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    QTemporaryDir seeds_;

  private slots:
    void initTestCase() {
        QVERIFY(seeds_.isValid());
        // These folders are read-only inputs to every startup scenario. Reuse
        // them while settings, windows and archive requests remain per-case.
        for (int count : {2, 301})
            for (const QString &name :
                 {QStringLiteral("requested directory 空"), QStringLiteral("peer directory δ")}) {
                const QString root = seeds_.filePath(QString::number(count) + '/' + name);
                for (int i = 0; i < count; ++i)
                    QVERIFY(SourceExtractFixture::writeFile(
                        QDir(root).filePath(
                            QString("item-%1 文件.txt").arg(i, 3, 10, QLatin1Char('0'))),
                        root.toUtf8()));
            }
        QVERIFY(SourceExtractFixture::writeFile(seeds_.filePath("saved main directory/decoy.txt"),
                                                "saved"));
    }

    void ordinaryStartupKeepsRequestedMainPath_data() {
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<bool>("large");
        QTest::addColumn<bool>("russian");
        QTest::addColumn<bool>("icons");
        QTest::addColumn<QString>("focusRoute");
        for (unsigned pane : {0u, 1u})
            for (bool large : {false, true})
                for (bool russian : {false, true})
                    for (bool icons : {false, true})
                        for (const QString &focusRoute :
                             {QString("none"), QString("input"), QString("api"), QString("late")})
                            QTest::newRow(qPrintable(QString("%1-%2-%3-%4-%5")
                                                         .arg(pane)
                                                         .arg(large)
                                                         .arg(russian)
                                                         .arg(icons)
                                                         .arg(focusRoute)))
                                << pane << large << russian << icons << focusRoute;
    }
    void ordinaryStartupKeepsRequestedMainPath() {
        QFETCH(unsigned, pane);
        QFETCH(bool, large);
        QFETCH(bool, russian);
        QFETCH(bool, icons);
        QFETCH(QString, focusRoute);
        const bool overrideFocus = focusRoute == "input" || focusRoute == "api";
        const bool archiveRequest = focusRoute == "api";
        const bool immediatePeer = focusRoute == "late";
        const int count = large ? 301 : 2;
        const QString settingsPath = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settingsPath);
        SaveRegLang(UString(russian ? L"ru" : L"-"), settingsPath);
        ReloadLang(settingsPath);
        QSettings settings(settingsPath, QSettings::IniFormat);
        settings.setValue("FM/ShowDots", false);
        settings.sync();
        CWindowInfo info{};
        info.rect.right = 980;
        info.rect.bottom = 620;
        info.numPanels = immediatePeer ? 1 : 2;
        info.currentPanel = pane;
        info.splitterPos = 490;
        info.Save(settingsPath);
        CListMode modes;
        modes.Panels[0] = modes.Panels[1] = icons ? 0 : 3;
        modes.Save(settingsPath);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString requested =
            seeds_.filePath(QString::number(immediatePeer ? 2 : count) + "/requested directory 空");
        const QString peer = seeds_.filePath(QString::number(count) + "/peer directory δ");
        const QString saved = seeds_.filePath("saved main directory");
        SavePanelPath(pane, ToUString(saved), settingsPath);
        SavePanelPath(1 - pane, ToUString(peer), settingsPath);
        QtFileManagerStartup startup;
        startup.FullPath = ToUString(requested);
        ControlledBackend backend;
        const QString newArchive = fixture.filePath("new request 空.zip");
        if (archiveRequest) {
            QVERIFY(backend.prepareArchiveFixture(newArchive));
            QList<ArchiveEntry> entries;
            for (int i = 0; i < count; ++i)
                entries << ArchiveEntry{QString("item-%1 文件.txt").arg(i, 3, 10, QLatin1Char('0')),
                                        1, 1};
            backend.queueList(entries);
        }
        MainWindow window(&backend, {}, nullptr, &startup);
        auto *mainUi = window.findChild<QWidget *>(QString("filePanel%1").arg(pane));
        auto *peerUi = window.findChild<QWidget *>(QString("filePanel%1").arg(1 - pane));
        QCOMPARE(
            QFileInfo(mainUi->findChild<QLineEdit *>("archivePath")->text()).canonicalFilePath(),
            QFileInfo(requested).canonicalFilePath());
        if (overrideFocus && large)
            QCOMPARE(mainUi->findChild<FileTable *>("archiveTable")->rowCount(), 0);
        window.show();
        if (immediatePeer) {
            QVERIFY(!peerUi);
            action(window, "twoPanelsAction")->trigger();
            peerUi = window.findChild<QWidget *>(QString("filePanel%1").arg(1 - pane));
            QVERIFY(peerUi);
        }
        if (overrideFocus) {
            auto *address = peerUi->findChild<QLineEdit *>("archivePath");
            PanelKeys::focusAddress(address);
            if (archiveRequest) {
                QTest::mouseClick(address, Qt::LeftButton);
                QVERIFY2(window.windowTitle().contains(QStringLiteral("peer directory δ")),
                         qPrintable(window.windowTitle()));
                QVERIFY(peerUi->isAncestorOf(window.focusWidget()));
                window.openArchive(newArchive);
            } else
                QTest::keyClick(address, Qt::Key_Shift);
        }
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        for (unsigned index : {0u, 1u}) {
            auto *ui = window.findChild<QWidget *>(QString("filePanel%1").arg(index));
            QVERIFY(ui);
            const bool archivePanel = archiveRequest && index != pane;
            const QString expected = index == pane ? requested : (archivePanel ? newArchive : peer);
            QCOMPARE(QFileInfo(QDir::cleanPath(ui->findChild<QLineEdit *>("archivePath")->text()))
                         .canonicalFilePath(),
                     QFileInfo(expected).canonicalFilePath());
            auto *list = static_cast<FileTable *>(ui->findChild<FileTable *>("archiveTable"));
            const int expectedRows = immediatePeer && index == pane ? 2 : count;
            QVERIFY(SignalWaitSupport::waitForCount(*list->model(), expectedRows));
            QCOMPARE(list->rowCount(), expectedRows);
            QTRY_VERIFY(list->isEnabled() && list->updatesEnabled());
            QVERIFY(list->sourcePanel->PanelCreated);
            QCOMPARE(list->sourcePanel->IsFSFolder(), !archivePanel);
            QVERIFY(list->sourcePanel->ShouldProcessNotifications());
            if (!archivePanel)
                QCOMPARE(QFileInfo(ToQString(GetFolderPath(list->sourcePanel->GetFolder())))
                             .canonicalFilePath(),
                         QFileInfo(expected).canonicalFilePath());
            list->sourcePanel->Set_AutoRefresh_Mode(false);
        }
        QTRY_VERIFY2((overrideFocus ? peerUi : mainUi)->isAncestorOf(window.focusWidget()),
                     qPrintable(QString("focus=%1 title=%2")
                                    .arg(window.focusWidget() ? window.focusWidget()->objectName()
                                                              : QString("null"))
                                    .arg(window.windowTitle())));
        QVERIFY(action(window, icons ? "viewMode0Action" : "viewMode3Action")->isChecked());
        QCOMPARE(backend.listCalls, archiveRequest ? 1 : 0);
        if (archiveRequest)
            QCOMPARE(SourceExtractFixture::readFile(newArchive), QByteArray());
        for (const QString &root : {requested, peer}) {
            const int fileCount = immediatePeer && root == requested ? 2 : count;
            for (int i = 0; i < fileCount; ++i) {
                const QString name = QString("item-%1 文件.txt").arg(i, 3, 10, QLatin1Char('0'));
                QCOMPARE(SourceExtractFixture::readFile(QDir(root).filePath(name)), root.toUtf8());
            }
        }
        QCOMPARE(SourceExtractFixture::readFile(QDir(saved).filePath("decoy.txt")),
                 QByteArray("saved"));
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(size);
            QApplication::processEvents();
            QVERIFY(UiInspection::bounds(*window.findChild<QWidget *>("filePanel0"), window).x() <
                    UiInspection::bounds(*window.findChild<QWidget *>("filePanel1"), window).x());
            for (unsigned index : {0u, 1u}) {
                auto *ui = window.findChild<QWidget *>(QString("filePanel%1").arg(index));
                auto *view =
                    icons ? static_cast<QAbstractItemView *>(ui->findChild<QListView *>("iconList"))
                          : ui->findChild<FileTable *>("archiveTable");
                QVERIFY(view->isEnabled());
                QVERIFY(UiInspection::fullyVisible(*view, window));
            }
            QVERIFY(UiInspection::writeReport(
                window,
                QString("panel-main-path-%1-%2").arg(QTest::currentDataTag()).arg(size.width())));
        }
    }
};
QTEST_MAIN(PanelCreationPathTests)
#include "panel_creation_path_tests.moc"
