#include "archive/agent/Agent.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/resources/qtarchivefileresource.h"
#include "archive/support/source_extract_fixture.h"
#include "filemanager/qtfilemanagerstartup.h"
#include "filemanager/support/selection_fixture.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "platform/support/native_property_failure_runtime.h"
#include "ui/progress/ProgressDialog2.h"
#include "ui/support/layout_inspection.h"

class NativeRendererFailureTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    NativePropertyFailureRuntime runtime_;
    QTemporaryDir archives_;
    QStringList names_;
    QString small_, large_;

    void inspectState(MainWindow &window, const QString &state) {
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(size);
            QApplication::processEvents();
            for (unsigned index : {0u, 1u}) {
                auto *owner = window.findChild<QWidget *>(QString("filePanel%1").arg(index));
                auto *table = owner->findChild<FileTable *>("archiveTable");
                auto *icons = owner->findChild<QListView *>("iconList");
                QWidget *view = table->isVisible() ? static_cast<QWidget *>(table) : icons;
                auto *address = owner->findChild<QLineEdit *>("archivePath");
                QVERIFY(view->isEnabled() && address->isEnabled());
                QVERIFY(UiInspection::fullyVisible(*view, window));
                QVERIFY(UiInspection::fullyVisible(*address, window));
                QVERIFY(UiInspection::bounds(*address, window).bottom() <
                        UiInspection::bounds(*view, window).top());
            }
            QVERIFY(UiInspection::writeReport(window, QString("native-renderer-%1-%2-%3")
                                                          .arg(QTest::currentDataTag(), state)
                                                          .arg(size.width())));
        }
    }

  private slots:
    void initTestCase() {
#ifdef Q_OS_WIN
        const QString preparedEngine = QStringLiteral("7z.exe");
#else
        const QString preparedEngine = QStringLiteral("7z");
#endif
        if (!QFileInfo::exists(
                QDir(QString::fromUtf8(SEVENZIP_EXTERNAL_RUNTIME)).filePath(preparedEngine)))
            QSKIP("Run scripts/build-codec-runtime.sh for the official native module");
        QVERIFY(runtime_.prepare(QString::fromUtf8(SEVENZIP_EXTERNAL_RUNTIME),
                                 QString::fromUtf8(SEVENZIP_EXTERNAL_FORMAT_FIXTURE)));
        QVERIFY(archives_.isValid());
        for (int i = 0; i < 301; ++i) {
            const QString name = QString("item %1 空.bin").arg(i, 3, 10, QLatin1Char('0'));
            names_ << name;
        }
        for (int count : {2, 301}) {
            const QString path = archives_.filePath(QString("archive-%1.7z").arg(count));
            QVERIFY(runtime_.createArchive(path, count));
            (count == 2 ? small_ : large_) = path;
        }
    }
    void nativeFailureRetainsCallerPolicy_data() {
        QTest::addColumn<QString>("route");
        QTest::addColumn<int>("result");
        QTest::addColumn<bool>("large");
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<bool>("russian");
        QTest::addColumn<bool>("icons");
        for (const QString &route : {QString("create"), QString("reload"), QString("open")})
            for (HRESULT result : {S_FALSE, E_FAIL, E_ABORT})
                for (bool large : {false, true})
                    for (unsigned pane : {0u, 1u})
                        for (bool russian : {false, true})
                            for (bool icons : {false, true})
                                QTest::newRow(qPrintable(QString("%1-%2-%3-%4-%5-%6")
                                                             .arg(route)
                                                             .arg(quint32(result), 0, 16)
                                                             .arg(large)
                                                             .arg(pane)
                                                             .arg(russian)
                                                             .arg(icons)))
                                    << route << int(result) << large << pane << russian << icons;
    }
    void stoppedNativePublicationDropsItsResult_data() {
        QTest::addColumn<bool>("close");
        QTest::addColumn<unsigned>("pane");
        for (bool close : {false, true})
            for (unsigned pane : {0u, 1u})
                QTest::newRow(qPrintable(QString("%1-%2").arg(close).arg(pane))) << close << pane;
    }
    void stoppedNativePublicationDropsItsResult() {
        QFETCH(bool, close);
        QFETCH(unsigned, pane);
        QVERIFY(runtime_.clear());
        const QString settingsPath = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settingsPath);
        CWindowInfo info{};
        info.rect.right = 980;
        info.rect.bottom = 620;
        info.numPanels = 2;
        info.currentPanel = pane;
        info.splitterPos = 490;
        info.Save(settingsPath);
        QSettings settings(settingsPath, QSettings::IniFormat);
        settings.setValue("FM/ShowDots", false);
        settings.sync();
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString sentinel = fixture.filePath("sentinel δ.txt");
        QVERIFY(SourceExtractFixture::writeFile(sentinel, "cancel bytes"));
        SavePanelPath(1 - pane, ToUString(fixture.path()), settingsPath);
        const QByteArray before = SourceExtractFixture::readFile(large_);
        ArchiveBackend backend(nullptr, runtime_.engine());
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(runtime_.module()));
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY(ready[0][0].toBool());
        QtFileManagerStartup startup;
        startup.FullPath = ToUString(large_);
        startup.ArcFormat = L"SevenzipFixture7z";
        startup.NeedOpenArc = true;
        MainWindow window(&backend, {}, nullptr, &startup);
        auto *owner = window.findChild<QWidget *>(QString("filePanel%1").arg(pane));
        auto *list = static_cast<FileTable *>(owner->findChild<FileTable *>("archiveTable"));
        QTRY_COMPARE(list->rowCount(), 301);
        QTRY_VERIFY(list->isEnabled() && list->updatesEnabled());
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        owner->findChild<QLineEdit *>("archivePath")->setFocus();
        list->sourcePanel->Set_AutoRefresh_Mode(false);
        unsigned messages = 0;
        QTimer dismiss;
        connect(&dismiss, &QTimer::timeout, &window, [&] {
            for (QWidget *widget : QApplication::topLevelWidgets())
                if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible()) {
                    ++messages;
                    box->accept();
                }
        });
        dismiss.start(1);
        QVERIFY(runtime_.arm(257, E_FAIL));
        list->sourcePanel->OnReload();
        QVERIFY(!list->isEnabled());
        QVERIFY(runtime_.receipts().isEmpty());
        if (close)
            window.close();
        else
            window.openPath(fixture.path());
        QTest::qWait(30);
        QCOMPARE(messages, 0u);
        QVERIFY(runtime_.receipts().isEmpty());
        if (!close) {
            QTRY_COMPARE(list->rowCount(), 1);
            QVERIFY(list->isEnabled() && list->updatesEnabled());
            QVERIFY(list->sourcePanel->ShouldProcessNotifications());
            QVERIFY(list->sourcePanel->Is_IO_FS_Folder());
            inspectState(window, "replaced");
        }
        QCOMPARE(SourceExtractFixture::readFile(large_), before);
        QCOMPARE(SourceExtractFixture::readFile(sentinel), QByteArray("cancel bytes"));
        QVERIFY(runtime_.clear());
    }
    void nativeFailureRetainsCallerPolicy() {
        QFETCH(QString, route);
        QFETCH(int, result);
        QFETCH(bool, large);
        QFETCH(unsigned, pane);
        QFETCH(bool, russian);
        QFETCH(bool, icons);
        QVERIFY(runtime_.clear());
        const int count = large ? 301 : 2;
        const UInt32 failedIndex = large ? 257 : 0;
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
        info.numPanels = 2;
        info.currentPanel = pane;
        info.splitterPos = 490;
        info.Save(settingsPath);
        CListMode modes;
        modes.Panels[0] = modes.Panels[1] = icons ? 0 : 3;
        modes.Save(settingsPath);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString archive = fixture.filePath("native archive δ.7z");
        QVERIFY(QFile::copy(large ? large_ : small_, archive));
        const QByteArray before = SourceExtractFixture::readFile(archive);
        const QString peerPath = fixture.filePath("peer folder 空");
        const QString sentinel = QDir(peerPath).filePath("sentinel δ.bin");
        QVERIFY(SourceExtractFixture::writeFile(sentinel, "peer bytes"));
        SavePanelPath(1 - pane, ToUString(peerPath), settingsPath);
        ArchiveBackend backend(nullptr, runtime_.engine());
        QSignalSpy catalogReady(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(runtime_.module()));
        QTRY_COMPARE(catalogReady.size(), 1);
        QVERIFY2(catalogReady[0][0].toBool(), qPrintable(catalogReady[0][1].toString()));
        QVERIFY(backend.codecCatalog()->formatIndex("SevenzipFixture7z") >= 0);
        QtFileManagerStartup startup;
        startup.FullPath = ToUString(route == "create" ? archive : fixture.path());
        startup.ArcFormat = route == "create" ? UString(L"SevenzipFixture7z") : UString();
        startup.NeedOpenArc = route == "create";
        QStringList messages;
        bool messageIdle = true;
        MainWindow *shown = nullptr;
        QTimer dismiss;
        connect(&dismiss, &QTimer::timeout, &backend, [&] {
            for (QWidget *widget : QApplication::topLevelWidgets()) {
                auto *box = qobject_cast<QMessageBox *>(widget);
                if (!box || !box->isVisible())
                    continue;
                messages << box->text();
                if (shown) {
                    for (unsigned index : {0u, 1u}) {
                        auto *ui = shown->findChild<QWidget *>(QString("filePanel%1").arg(index));
                        auto *list =
                            static_cast<FileTable *>(ui->findChild<FileTable *>("archiveTable"));
                        messageIdle = messageIdle && list->isEnabled() && list->updatesEnabled() &&
                                      list->sourcePanel->ShouldProcessNotifications();
                    }
                }
                box->accept();
            }
        });
        dismiss.start(1);
        if (route == "create")
            QVERIFY(runtime_.arm(failedIndex, HRESULT(result)));
        MainWindow window(&backend, {}, nullptr, &startup);
        shown = &window;
        window.show();
        auto *owner = window.findChild<QWidget *>(QString("filePanel%1").arg(pane));
        auto *list = static_cast<FileTable *>(owner->findChild<FileTable *>("archiveTable"));
        auto &panel = *list->sourcePanel;
        panel.Set_AutoRefresh_Mode(false);
        auto *peer = window.findChild<QWidget *>(QString("filePanel%1").arg(1 - pane));
        auto *peerList = static_cast<FileTable *>(peer->findChild<FileTable *>("archiveTable"));
        peerList->sourcePanel->Set_AutoRefresh_Mode(false);
        CMyComPtr<IFolderFolder> peerProvider = peerList->sourcePanel->GetFolder();
        const int peerRows = peerList->rowCount();
        QAbstractItemView *view =
            icons ? static_cast<QAbstractItemView *>(owner->findChild<QListView *>("iconList"))
                  : list;
        CMyComPtr<IFolderFolder> provider;
        if (route != "create") {
            window.activateWindow();
            QVERIFY(QTest::qWaitForWindowActive(&window));
            owner->findChild<QLineEdit *>("archivePath")->setFocus();
            UInt32 fileCount = 0;
            QCOMPARE(panel.GetFolder()->GetNumberOfItems(&fileCount), S_OK);
            UInt32 index = 0;
            for (; index < fileCount; ++index)
                if (ToQString(panel.GetItemName(index)) == QFileInfo(archive).fileName())
                    break;
            QVERIFY(index < fileCount);
            if (route == "open")
                QVERIFY(runtime_.arm(failedIndex, HRESULT(result)));
            if (route == "open")
                panel.OpenItem(index, true, false, L"SevenzipFixture7z");
            else
                QCOMPARE(panel.OpenAsArc_Index(index, L"SevenzipFixture7z"), S_OK);
            if (route == "reload") {
                QTRY_COMPARE(list->rowCount(), count);
                QTRY_VERIFY(list->isEnabled() && list->updatesEnabled());
                provider = panel.GetFolder();
                panel.SelectAll(false);
                MarkSelectionForTest(panel, ToUString(names_[0]));
                list->setCurrentCell(count - 1, 0, QItemSelectionModel::NoUpdate);
                QVERIFY(runtime_.arm(failedIndex, HRESULT(result)));
                window.activateWindow();
                QVERIFY(QTest::qWaitForWindowActive(&window));
                view->setFocus();
                QTest::keyClick(view, Qt::Key_R, Qt::ControlModifier);
            }
        }
        QTRY_VERIFY(!runtime_.receipts().isEmpty());
        QTRY_VERIFY(list->isEnabled() && list->updatesEnabled());
        QCOMPARE(list->rowCount(), 0);
        QVERIFY(panel.IsArcFolder() && panel.PanelCreated && panel.ShouldProcessNotifications());
        QVERIFY(messageIdle);
        QCOMPARE(messages.size(), route == "reload" ? 1 : 0);
        QVERIFY(runtime_.receipts().contains(QByteArray::number(failedIndex) + ' ' +
                                             QByteArray::number(quint32(result))));
        if (provider)
            QCOMPARE(panel.GetFolder(), provider.Interface());
        provider = panel.GetFolder();
        auto *agent = dynamic_cast<CAgentFolder *>(provider.Interface());
        QVERIFY(agent);
        auto *resource = dynamic_cast<QtArchiveFileResource *>(agent->GetEngineResource());
        QVERIFY(resource && resource->NativeItemProperties());
        if (route == "reload")
            QVERIFY(messages[0].contains(ToQString(HResultToMessage(HRESULT(result)))));
        inspectState(window, "failed");
        QVERIFY(runtime_.clear());
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        view->setFocus();
        QTest::keyClick(view, Qt::Key_R, Qt::ControlModifier);
        QTRY_COMPARE(list->rowCount(), count);
        QTRY_VERIFY(list->isEnabled() && list->updatesEnabled());
        QCOMPARE(panel.GetFolder(), provider.Interface());
        QVERIFY(panel.ShouldProcessNotifications());
        QCOMPARE(peerList->sourcePanel->GetFolder(), peerProvider.Interface());
        QCOMPARE(peerList->rowCount(), peerRows);
        QCOMPARE(SourceExtractFixture::readFile(sentinel), QByteArray("peer bytes"));
        QCOMPARE(SourceExtractFixture::readFile(archive), before);
        inspectState(window, "recovered");
        if (route == "open") {
            window.openPath(fixture.path());
            QTRY_COMPARE(list->rowCount(), 2);
            const QString probe = fixture.filePath("timer probe 空.txt");
            QVERIFY(SourceExtractFixture::writeFile(probe, "timer bytes"));
            panel.Set_AutoRefresh_Mode(true);
            panel.MarkPortableFolderChanged();
            panel.OnTimer();
            QTRY_COMPARE(list->rowCount(), 3);
            QVERIFY(panel.ShouldProcessNotifications());
            QCOMPARE(SourceExtractFixture::readFile(probe), QByteArray("timer bytes"));
            panel.Set_AutoRefresh_Mode(false);
        }
    }
};
QTEST_MAIN(NativeRendererFailureTests)
#include "native_renderer_failure_tests.moc"
