#include "archive/agent/Agent.h"
#include "archive/resources/qtarchivefileresource.h"
#include "archive/resources/qtarchivefoldermanager.h"
#include "archive/support/archive_copy_callback.h"
#include "archive/support/source_extract_fixture.h"
#include "filemanager/providers/FileFolderPluginOpen.h"
#include "filemanager/qtfilemanagerstartup.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "platform/support/native_property_failure_runtime.h"
#include "platform/temp/qtownedtempregistry.h"
#include "support/panel_keys.h"
#include "support/widget_dialogs.h"
#include "ui/progress/ProgressDialog2.h"
#include "ui/support/layout_inspection.h"

class NativeNestedPublicationTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    WidgetDialogFixture dialogs_;
    NativePropertyFailureRuntime runtime_;
    QTemporaryDir archives_;
    QMap<QString, QString> outer_;
    QMap<int, QString> inner_;

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
                QVERIFY(UiInspection::fullyVisible(*PanelKeys::navigationFor(address), window));
                QVERIFY(UiInspection::bounds(*address, window).bottom() <
                        UiInspection::bounds(*view, window).top());
            }
            QVERIFY(UiInspection::writeReport(window, QString("native-nested-%1-%2-%3")
                                                          .arg(QTest::currentDataTag(), state)
                                                          .arg(size.width())));
        }
    }

  private slots:
    void initTestCase() {
#ifdef Q_OS_WIN
        const QString engine = "7z.exe";
#else
        const QString engine = "7z";
#endif
        if (!QFileInfo::exists(QDir(QString::fromUtf8(SEVENZIP_EXTERNAL_RUNTIME)).filePath(engine)))
            QSKIP("Run scripts/build-codec-runtime.sh for the official native module");
        QVERIFY(runtime_.prepare(QString::fromUtf8(SEVENZIP_EXTERNAL_RUNTIME),
                                 QString::fromUtf8(SEVENZIP_EXTERNAL_FORMAT_FIXTURE)));
        QVERIFY(archives_.isValid());
        qputenv("SEVENZIP_OWNED_TEMP_REGISTRY", archives_.filePath("registry").toUtf8());
        for (int count : {2, 301}) {
            const QString folder = archives_.filePath(QString::number(count));
            QVERIFY(QDir().mkpath(folder));
            const QString inner = QDir(folder).filePath("inner archive 空.7z");
            QVERIFY(runtime_.createArchive(inner, count));
            inner_[count] = inner;
            for (const QString &type : {QString("7z"), QString("tar")}) {
                const QString path =
                    archives_.filePath(QString("outer-%1-%2.%1").arg(type).arg(count));
                QProcess create;
                create.setWorkingDirectory(folder);
                QStringList args{"a", "-t" + type};
                if (type == "7z")
                    args << "-m0=Copy";
                create.start(runtime_.engine(),
                             args + QStringList{"--", path, QFileInfo(inner).fileName()});
                QVERIFY(create.waitForFinished(10000));
                QCOMPARE(create.exitCode(), 0);
                outer_[type + QString::number(count)] = path;
            }
        }
    }
    void nativeStreamMapsFolderIndicesAndRetainsItsOwner() {
        if (qEnvironmentVariable("SEVENZIP_NESTED_ROUTE") != "stream")
            return;
        QVERIFY(runtime_.clear());
        QTemporaryDir fixture;
        const QMap<QString, QByteArray> files{
            {"a.txt", "first"}, {"d/inner 空.bin", "nested bytes"}, {"z.txt", "last"}};
        for (auto it = files.cbegin(); it != files.cend(); ++it)
            QVERIFY(SourceExtractFixture::writeFile(fixture.filePath(it.key()), it.value()));
        const QString archive = fixture.filePath("indices.tar");
        QProcess create;
        create.setWorkingDirectory(fixture.path());
        create.start(runtime_.engine(), {"a", "-ttar", "--", archive, "a.txt", "d", "z.txt"});
        QVERIFY(create.waitForFinished(10000));
        QCOMPARE(create.exitCode(), 0);
        CMyComPtr<ISequentialInStream> retained;
        std::weak_ptr<const ArchiveCodecCatalog> catalog;
        {
            ArchiveBackend backend(nullptr, runtime_.engine());
            QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
            QVERIFY(backend.discoverCodecCatalog(runtime_.module()));
            QTRY_COMPARE(ready.size(), 1);
            QVERIFY(ready[0][0].toBool());
            catalog = backend.codecCatalog();
            CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
            CFfpOpen opening(manager);
            QCOMPARE(opening.OpenFileFolderPlugin(nullptr, us2fs(archive.toStdWString().c_str()),
                                                  UString(L"tar"), nullptr),
                     S_OK);
            CMyComPtr<IFolderSetFlatMode> flat;
            QCOMPARE(opening.Folder.QueryInterface(IID_IFolderSetFlatMode, &flat), S_OK);
            auto verifyFolder = [&](IFolderFolder *folder, const QString &prefix) {
                QCOMPARE(folder->LoadItems(), S_OK);
                CMyComPtr<IInArchiveGetStream> streams;
                QCOMPARE(folder->QueryInterface(IID_IInArchiveGetStream, (void **)&streams), S_OK);
                UInt32 count = 0;
                QCOMPARE(folder->GetNumberOfItems(&count), S_OK);
                for (UInt32 index = 0; index < count; ++index) {
                    NWindows::NCOM::CPropVariant name, parent, directory;
                    QCOMPARE(folder->GetProperty(index, kpidName, &name), S_OK);
                    QCOMPARE(folder->GetProperty(index, kpidPrefix, &parent), S_OK);
                    QCOMPARE(folder->GetProperty(index, kpidIsDir, &directory), S_OK);
                    CMyComPtr<ISequentialInStream> input;
                    QCOMPARE(streams->GetStream(index, &input), S_OK);
                    if (directory.vt == VT_BOOL && directory.boolVal) {
                        if (input) {
                            char byte;
                            UInt32 got = 1;
                            QCOMPARE(input->Read(&byte, 1, &got), S_OK);
                            QCOMPARE(got, 0u);
                        }
                        continue;
                    }
                    QVERIFY(input);
                    QString path = prefix;
                    if (parent.vt == VT_BSTR)
                        path += QString::fromWCharArray(parent.bstrVal);
                    path += QString::fromWCharArray(name.bstrVal);
                    path = QDir::fromNativeSeparators(path);
                    QVERIFY2(files.contains(path), qPrintable(path));
                    char bytes[64];
                    UInt32 got = 0;
                    QCOMPARE(input->Read(bytes, sizeof(bytes), &got), S_OK);
                    QCOMPARE(QByteArray(bytes, got), files[path]);
                    if (path == "d/inner 空.bin")
                        retained = input;
                }
            };
            verifyFolder(opening.Folder, {});
            QCOMPARE(flat->SetFlatMode(1), S_OK);
            verifyFolder(opening.Folder, {});
            QCOMPARE(flat->SetFlatMode(0), S_OK);
            QCOMPARE(opening.Folder->LoadItems(), S_OK);
            CMyComPtr<IFolderFolder> child;
            QCOMPARE(opening.Folder->BindToFolder(L"d", &child), S_OK);
            verifyFolder(child, "d/");
        }
        QVERIFY(retained);
        QVERIFY(!catalog.expired());
        CMyComPtr<IInStream> seekable;
        QCOMPARE(retained.QueryInterface(IID_IInStream, &seekable), S_OK);
        QCOMPARE(seekable->Seek(0, STREAM_SEEK_SET, nullptr), S_OK);
        char bytes[64];
        UInt32 got = 0;
        QCOMPARE(retained->Read(bytes, sizeof(bytes), &got), S_OK);
        QCOMPARE(QByteArray(bytes, got), QByteArray("nested bytes"));
        seekable.Release();
        retained.Release();
        QVERIFY(catalog.expired());
    }
    void nativeOpeningHonorsConfirmedCancel_data() {
        QTest::addColumn<QString>("route");
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<int>("answer");
        const QString requested = qEnvironmentVariable("SEVENZIP_NESTED_ROUTE");
        for (const QString &route : {QString("stream"), QString("memory"), QString("disk")}) {
            if (!requested.isEmpty() && requested != route)
                continue;
            for (unsigned pane : {0u, 1u})
                for (auto answer : {QMessageBox::Yes, QMessageBox::No, QMessageBox::Cancel})
                    QTest::newRow(qPrintable(QString("%1-%2-%3").arg(route).arg(pane).arg(answer)))
                        << route << pane << int(answer);
        }
    }
    void nativeOpeningHonorsConfirmedCancel() {
        QFETCH(QString, route);
        QFETCH(unsigned, pane);
        QFETCH(int, answer);
        QVERIFY(runtime_.clear());
        const QString settingsPath = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settingsPath);
        SaveRegLang(UString(L"-"), settingsPath);
        ReloadLang(settingsPath);
        CWindowInfo info{};
        info.rect.right = 980;
        info.rect.bottom = 620;
        info.numPanels = 2;
        info.currentPanel = pane;
        info.splitterPos = 490;
        info.Save(settingsPath);
        QTemporaryDir peer;
        QVERIFY(SourceExtractFixture::writeFile(peer.filePath("sentinel.txt"), "peer bytes"));
        SavePanelPath(1 - pane, ToUString(peer.path()), settingsPath);
        const QString path = outer_[(route == "stream" ? "tar" : "7z") + QString::number(301)];
        const QByteArray original = SourceExtractFixture::readFile(path);
        ArchiveBackend backend(nullptr, runtime_.engine());
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(runtime_.module()));
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY(ready[0][0].toBool());
        QtFileManagerStartup startup;
        startup.FullPath = ToUString(path);
        startup.ArcFormat = route == "stream" ? L"tar" : L"SevenzipFixture7z";
        startup.NeedOpenArc = true;
        MainWindow window(&backend, {}, nullptr, &startup);
        window.show();
        auto *owner = window.findChild<QWidget *>(QString("filePanel%1").arg(pane));
        auto *list = static_cast<FileTable *>(owner->findChild<FileTable *>("archiveTable"));
        auto &panel = *list->sourcePanel;
        panel.Set_AutoRefresh_Mode(false);
        QTRY_COMPARE(list->rowCount(), 1);
        CMyComPtr<IFolderFolder> outer = panel.GetFolder();
        QtOwnedTempRegistry registry;
        QSet<QString> before;
        for (const auto &entry : registry.Entries())
            before.insert(entry.key);
        QStringList created;
        bool clicked = false, replied = false, timedOut = false;
        QStringList errors;
        QElapsedTimer elapsed;
        elapsed.start();
        QTimer driver, responder;
        dialogs_.connectResponder(responder, &window, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(dialogs_.activeDialog())) {
                if (box->objectName() == "progressCancelQuestion") {
                    replied = true;
                    box->button(QMessageBox::StandardButton(answer))->click();
                } else {
                    errors << box->text();
                    box->accept();
                }
            }
        });
        dialogs_.connectResponder(driver, &window, [&] {
            if (elapsed.elapsed() > 15000) {
                timedOut = true;
                runtime_.clear();
                return;
            }
            if (clicked || runtime_.openReceipts().isEmpty())
                return;
            for (QWidget *widget : QApplication::topLevelWidgets())
                if (auto *progress = dynamic_cast<CProgressDialog *>(widget);
                    progress && dialogs_.ready(*progress)) {
                    for (const auto &entry : registry.Entries())
                        if (!before.contains(entry.key))
                            created << entry.path;
                    clicked = true;
                    progress->findChild<QPushButton *>("IDCANCEL")->click();
                    if (answer != QMessageBox::Yes)
                        runtime_.clear();
                    return;
                }
        });
        QVERIFY(runtime_.forceDisk(route == "disk"));
        QVERIFY(runtime_.arm(UInt32(-1), E_ABORT, 301));
        responder.start(5);
        driver.start(5);
        panel.OpenItem(0, true, false, L"SevenzipFixture7z");
        driver.stop();
        responder.stop();
        QVERIFY(clicked && replied && !timedOut);
        QVERIFY(!created.isEmpty());
        QCOMPARE(errors.size(), 0);
        QVERIFY(runtime_.clear());
        if (answer == QMessageBox::Yes) {
            QCOMPARE(panel.GetFolder(), outer.Interface());
            QCOMPARE(panel.GetParentFolders().Size(), 1u);
            QCOMPARE(list->rowCount(), 1);
            for (const QString &directory : created)
                QVERIFY2(!QFileInfo::exists(directory), qPrintable(directory));
            panel.OpenItem(0, true, false, L"SevenzipFixture7z");
        }
        QTRY_COMPARE(list->rowCount(), 301);
        QTRY_VERIFY(list->isEnabled() && list->updatesEnabled());
        QVERIFY(panel.ShouldProcessNotifications());
        QCOMPARE(panel.GetParentFolders().Size(), 2u);
        inspectState(window, "cancel-recovered");
        action(window, "parentFolderAction")->trigger();
        QTRY_COMPARE(list->rowCount(), 1);
        QCOMPARE(panel.GetFolder(), outer.Interface());
        for (const QString &directory : created)
            QTRY_VERIFY(!QFileInfo::exists(directory));
        QCOMPARE(SourceExtractFixture::readFile(path), original);
        QCOMPARE(SourceExtractFixture::readFile(peer.filePath("sentinel.txt")),
                 QByteArray("peer bytes"));
        window.openPath(peer.path());
        QTRY_COMPARE(list->rowCount(), 1);
        QVERIFY(SourceExtractFixture::writeFile(peer.filePath("timer.txt"), "timer bytes"));
        panel.Set_AutoRefresh_Mode(true);
        panel.MarkPortableFolderChanged();
        panel.OnTimer();
        QTRY_COMPARE(list->rowCount(), 2);
        panel.Set_AutoRefresh_Mode(false);
    }
    void nestedPublicationRestoresSourceGuards_data() {
        QTest::addColumn<QString>("route");
        QTest::addColumn<int>("count");
        QTest::addColumn<int>("result");
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<bool>("russian");
        QTest::addColumn<bool>("icons");
        const QString requested = qEnvironmentVariable("SEVENZIP_NESTED_ROUTE");
        for (const QString &route : {QString("stream"), QString("memory"), QString("disk")}) {
            if (!requested.isEmpty() && requested != route)
                continue;
            for (int count : {2, 301})
                for (HRESULT result : {S_OK, S_FALSE, E_FAIL, E_ABORT})
                    for (unsigned pane : {0u, 1u})
                        for (bool russian : {false, true})
                            for (bool icons : {false, true})
                                QTest::newRow(qPrintable(QString("%1-%2-%3-%4-%5-%6")
                                                             .arg(route)
                                                             .arg(count)
                                                             .arg(quint32(result), 0, 16)
                                                             .arg(pane)
                                                             .arg(russian)
                                                             .arg(icons)))
                                    << route << count << int(result) << pane << russian << icons;
        }
    }
    void nestedPublicationRestoresSourceGuards() {
        QFETCH(QString, route);
        QFETCH(int, count);
        QFETCH(int, result);
        QFETCH(unsigned, pane);
        QFETCH(bool, russian);
        QFETCH(bool, icons);
        QVERIFY(runtime_.clear());
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
        const QString peerPath = fixture.filePath("peer folder δ");
        const QString sentinel = QDir(peerPath).filePath("sentinel 空.txt");
        QVERIFY(SourceExtractFixture::writeFile(sentinel, "peer bytes"));
        SavePanelPath(1 - pane, ToUString(peerPath), settingsPath);
        const QString archivePath =
            outer_[(route == "stream" ? "tar" : "7z") + QString::number(count)];
        const QByteArray original = SourceExtractFixture::readFile(archivePath);
        const QByteArray innerBytes = SourceExtractFixture::readFile(inner_[count]);
        ArchiveBackend backend(nullptr, runtime_.engine());
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(runtime_.module()));
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY2(ready[0][0].toBool(), qPrintable(ready[0][1].toString()));
        QtFileManagerStartup startup;
        startup.FullPath = ToUString(archivePath);
        startup.ArcFormat = route == "stream" ? L"tar" : L"SevenzipFixture7z";
        startup.NeedOpenArc = true;
        MainWindow window(&backend, {}, nullptr, &startup);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *owner = window.findChild<QWidget *>(QString("filePanel%1").arg(pane));
        auto *list = static_cast<FileTable *>(owner->findChild<FileTable *>("archiveTable"));
        auto *peer = window.findChild<QWidget *>(QString("filePanel%1").arg(1 - pane));
        auto *peerList = static_cast<FileTable *>(peer->findChild<FileTable *>("archiveTable"));
        auto &panel = *list->sourcePanel;
        panel.Set_AutoRefresh_Mode(false);
        peerList->sourcePanel->Set_AutoRefresh_Mode(false);
        QTRY_COMPARE(list->rowCount(), 1);
        QTRY_VERIFY(list->isEnabled() && list->updatesEnabled());
        CMyComPtr<IFolderFolder> outerProvider = panel.GetFolder();
        CMyComPtr<IFolderFolder> peerProvider = peerList->sourcePanel->GetFolder();
        auto *agent = dynamic_cast<CAgentFolder *>(outerProvider.Interface());
        QVERIFY(agent);
        auto *resource = dynamic_cast<QtArchiveFileResource *>(agent->GetEngineResource());
        QVERIFY(resource && resource->NativeItemProperties());
        CMyComPtr<IInArchiveGetStream> nativeStream;
        resource->NativeItemProperties()->QueryInterface(IID_IInArchiveGetStream,
                                                         (void **)&nativeStream);
        QCOMPARE(bool(nativeStream), route == "stream");
        CMyComPtr<IInArchiveGetStream> sourceStream;
        outerProvider.QueryInterface(IID_IInArchiveGetStream, &sourceStream);
        if (route == "stream") {
            QVERIFY(sourceStream);
            CMyComPtr<ISequentialInStream> input;
            QCOMPARE(sourceStream->GetStream(0, &input), S_OK);
            QVERIFY(input);
            QByteArray read;
            for (;;) {
                char bytes[8192];
                UInt32 got = 0;
                QCOMPARE(input->Read(bytes, sizeof(bytes), &got), S_OK);
                if (!got)
                    break;
                read.append(bytes, got);
                QVERIFY(read.size() <= innerBytes.size());
            }
            QCOMPARE(read, innerBytes);
        }
        QStringList messages;
        QTimer dismiss;
        connect(&dismiss, &QTimer::timeout, &window, [&] {
            for (QWidget *widget : QApplication::topLevelWidgets())
                if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible()) {
                    messages << box->text();
                    box->accept();
                }
        });
        dismiss.start(1);
        PanelKeys::focusAddress(owner->findChild<QLineEdit *>("archivePath"));
        QVERIFY(runtime_.forceDisk(route == "disk"));
        QVERIFY(runtime_.arm(count == 301 ? 257 : 0, HRESULT(result), count));
        panel.OpenItem(0, true, false, L"SevenzipFixture7z");
        QTRY_VERIFY(!runtime_.receipts().isEmpty());
        QTRY_VERIFY(list->isEnabled() && list->updatesEnabled());
        QCOMPARE(messages.size(), 0);
        QCOMPARE(panel.GetParentFolders().Size(), 2u);
        QCOMPARE(list->rowCount(), result == S_OK ? count : 0);
        QVERIFY(panel.ShouldProcessNotifications());
        QCOMPARE(!runtime_.diskReceipts().isEmpty(), route == "disk");
        if (route == "disk") {
            // Forcing the route must not poison extraction/preallocation metadata.
            NWindows::NCOM::CPropVariant size;
            QCOMPARE(resource->NativeItemProperties()->GetProperty(0, kpidSize, &size), S_OK);
            QCOMPARE(size.vt, VARTYPE(VT_UI8));
            QCOMPARE(size.uhVal.QuadPart, UInt64(innerBytes.size()));
        }
        if (route == "stream")
            QVERIFY(runtime_.extractionReceipts().isEmpty());
        else
            QVERIFY(runtime_.extractionReceipts().contains(
                QByteArray::number(QCoreApplication::applicationPid()) + " 1"));
        const QString spool = ToQString(fs2us(panel.GetParentFolders().Back().FilePath));
        const bool hadSpool = QFileInfo::exists(spool);
        CMyComPtr<IFolderFolder> childProvider = panel.GetFolder();
        inspectState(window, result == S_OK ? "opened" : "failed");
        QVERIFY(runtime_.clear());
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QAbstractItemView *view =
            icons ? static_cast<QAbstractItemView *>(owner->findChild<QListView *>("iconList"))
                  : list;
        view->setFocus();
        QTest::keyClick(view, Qt::Key_R, Qt::ControlModifier);
        QTRY_COMPARE(list->rowCount(), count);
        QTRY_VERIFY(list->isEnabled() && list->updatesEnabled());
        QCOMPARE(panel.GetFolder(), childProvider.Interface());
        QVERIFY(panel.ShouldProcessNotifications());
        inspectState(window, "recovered");
        {
            // Reading the opened child must work regardless of whether its
            // source stayed a handler stream or used a temporary file.
            CMyComPtr<IFolderOperations> operations;
            QCOMPARE(childProvider.QueryInterface(IID_IFolderOperations, &operations), S_OK);
            auto *observer = new CArchiveCopyCallback;
            CMyComPtr<IFolderOperationsExtractCallback> callback = observer;
            QTemporaryDir output;
            QVERIFY(output.isValid());
            const auto prefix = (output.path() + '/').toStdWString();
            const UInt32 index = 0;
            QCOMPARE(operations->CopyTo(0, &index, 1, 0, 0, prefix.c_str(), callback), S_OK);
            QCOMPARE(observer->Errors, 0u);
            QCOMPARE(
                SourceExtractFixture::readFile(output.filePath(QStringLiteral("item 000 空.bin"))),
                QStringLiteral("item 000 空.bin").toUtf8());
        }
        // The fixture's identity assertion is complete. Release its extra
        // reader before the source Parent workflow closes/deletes the spool.
        childProvider.Release();
        action(window, "parentFolderAction")->trigger();
        QTRY_COMPARE(list->rowCount(), 1);
        QCOMPARE(panel.GetFolder(), outerProvider.Interface());
        QCOMPARE(panel.GetParentFolders().Size(), 1u);
        if (hadSpool)
            QTRY_VERIFY(!QFileInfo::exists(spool));
        QCOMPARE(peerList->sourcePanel->GetFolder(), peerProvider.Interface());
        QCOMPARE(peerList->rowCount(), 1);
        QCOMPARE(SourceExtractFixture::readFile(sentinel), QByteArray("peer bytes"));
        QCOMPARE(SourceExtractFixture::readFile(archivePath), original);
        window.openPath(fixture.path());
        QTRY_COMPARE(list->rowCount(), 1);
        const QString probe = fixture.filePath("timer probe 空.txt");
        QVERIFY(SourceExtractFixture::writeFile(probe, "timer bytes"));
        panel.Set_AutoRefresh_Mode(true);
        panel.MarkPortableFolderChanged();
        panel.OnTimer();
        QTRY_COMPARE(list->rowCount(), 2);
        QVERIFY(panel.ShouldProcessNotifications());
        panel.Set_AutoRefresh_Mode(false);
        QCOMPARE(messages.size(), 0);
    }
};

QTEST_MAIN(NativeNestedPublicationTests)
#include "native_nested_publication_tests.moc"
