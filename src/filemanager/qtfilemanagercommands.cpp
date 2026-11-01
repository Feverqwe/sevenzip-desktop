#include "filemanager/qtfilemanagercommands.h"
#include "7zip/PropID.h"
#include "7zip/UI/FileManager/resource.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/hashing/HashCalc.h"
#include "archive/resources/qtarchivefileresource.h"
#include "filemanager/MyLoadMenu.h"
#include "filemanager/panels/PanelSplitFile.h"
#include "filemanager/qtfilemenuprojection.h"
#include "filemanager/qtprogramfilemenu.h"
#include "filemanager/window/mainwindow.h"
#include "ui/dialogs/qtdialogscreenservices.h"

QtFileManagerCommands::QtFileManagerCommands(MainWindow &window) : window_(window) {}

void QtFileManagerCommands::Bind(QAction *action, unsigned id) {
    actions_[id].append(action);
    QObject::disconnect(action, &QAction::triggered, &window_, nullptr);
    QObject::connect(action, &QAction::triggered, &window_, [this, id] {
        if (!window_.app_.ExecuteCommand(id))
            window_.refreshActions();
    });
    action->setProperty("nativeCommandID", id);
}

void QtFileManagerCommands::BindToggle(QAction *action, unsigned id, bool CApp::*state) {
    actions_[id].append(action);
    QObject::disconnect(action, &QAction::toggled, &window_, nullptr);
    QObject::connect(action, &QAction::toggled, &window_, [this, action, id, state](bool checked) {
        const bool previous = window_.app_.*state;
        if (checked == previous)
            return;
        // QAction toggles before dispatch; native CApp owns the actual flag.
        // Roll presentation back before the command and mirror it on reload.
        {
            QSignalBlocker block(action);
            action->setChecked(previous);
        }
        window_.app_.ExecuteCommand(id);
    });
    action->setProperty("nativeCommandID", id);
}

void QtFileManagerCommands::AttachActions() {
    AttachFileActions();
    AttachPanelActions();
    AttachViewActions();
    AttachMountActions();
    window_.app_.AttachQtCommandServices(this);
    for (QAction *action : {window_.toolbarAdd_, window_.extract_, window_.test_, window_.copy_,
                            window_.move_, window_.remove_, window_.infoAction_})
        QObject::connect(action, &QAction::changed, &window_, &MainWindow::refreshToolbarTexts);
}

void QtFileManagerCommands::AttachFileActions() {
    auto &w = window_;
    Bind(w.infoAction_, IDM_PROPERTIES);
    Bind(w.back_, Back);
    Bind(w.toolbarAdd_, kMenuCmdID_Toolbar_Add);
    Bind(w.extract_, kMenuCmdID_Toolbar_Extract);
    Bind(w.test_, kMenuCmdID_Toolbar_Test);
    Bind(w.openItem_, IDM_OPEN);
    Bind(w.findChild<QAction *>("openArchiveAction"), OpenArchivePicker);
    Bind(w.findChild<QAction *>("openDeviceAction"), OpenDevicePicker);
    Bind(w.openInside_, IDM_OPEN_INSIDE);
    Bind(w.openInsideOne_, IDM_OPEN_INSIDE_ONE);
    Bind(w.openInsideParser_, IDM_OPEN_INSIDE_PARSER);
    Bind(w.openOutside_, IDM_OPEN_OUTSIDE);
    Bind(w.viewFile_, IDM_FILE_VIEW);
    Bind(w.editFile_, IDM_FILE_EDIT);
    Bind(w.diffFiles_, IDM_DIFF);
    const char *verCtrlNames[] = {"verEditAction", "verCommitAction", "verRevertAction",
                                  "verDiffAction"};
    for (unsigned i = 0; i < 4; ++i)
        Bind(w.findChild<QAction *>(QString::fromLatin1(verCtrlNames[i])), IDM_VER_EDIT + i);
    Bind(w.rename_, IDM_RENAME);
    Bind(w.copy_, IDM_COPY_TO);
    Bind(w.move_, IDM_MOVE_TO);
    Bind(w.remove_, IDM_DELETE);
    Bind(w.split_, IDM_SPLIT);
    Bind(w.combine_, IDM_COMBINE);
    Bind(w.properties_, IDM_PROPERTIES);
    Bind(w.findChild<QAction *>("commentAction"), IDM_COMMENT);
    Bind(w.findChild<QAction *>("linkAction"), IDM_LINK);
    Bind(w.findChild<QAction *>("altStreamsAction"), IDM_ALT_STREAMS);
    Bind(w.createFolder_, IDM_CREATE_FOLDER);
    Bind(w.createFile_, IDM_CREATE_FILE);
    Bind(w.exitAction_, 8);
    const unsigned hashIDs[] = {IDM_CRC32,    IDM_CRC64,    IDM_XXH64,   IDM_MD5,
                                IDM_SHA1,     IDM_SHA256,   IDM_SHA384,  IDM_SHA512,
                                IDM_SHA3_256, IDM_BLAKE2SP, IDM_HASH_ALL};
    for (unsigned i = 0; i < 11; ++i)
        Bind(w.hashActions_[i], hashIDs[i]);
}

void QtFileManagerCommands::AttachPanelActions() {
    auto &w = window_;
    Bind(w.up_, IDM_OPEN_PARENT_FOLDER);
    Bind(w.parent_, IDM_OPEN_PARENT_FOLDER);
    Bind(w.selectAll_, IDM_SELECT_ALL);
    Bind(w.deselectAll_, IDM_DESELECT_ALL);
    Bind(w.invertSelection_, IDM_INVERT_SELECTION);
    Bind(w.selectMask_, IDM_SELECT);
    Bind(w.deselectMask_, IDM_DESELECT);
    Bind(w.selectType_, IDM_SELECT_BY_TYPE);
    Bind(w.deselectType_, IDM_DESELECT_BY_TYPE);
}

void QtFileManagerCommands::AttachViewActions() {
    auto &w = window_;
    Bind(w.twoPanelsAction_, IDM_VIEW_TWO_PANELS);
    Bind(w.refresh_, IDM_VIEW_REFRESH);
    Bind(w.autoRefreshAction_, IDM_VIEW_AUTO_REFRESH);
    Bind(w.foldersHistory_, IDM_FOLDERS_HISTORY);
    Bind(w.root_, IDM_OPEN_ROOT_FOLDER);
    Bind(w.settingsAction_, IDM_OPTIONS);
    Bind(w.findChild<QAction *>("helpContentsAction"), IDM_HELP_CONTENTS);
    Bind(w.findChild<QAction *>("aboutAction"), IDM_ABOUT);
    Bind(w.findChild<QAction *>("tempDirAction"), IDM_TEMP_DIR);
    Bind(w.findChild<QAction *>("benchmarkAction"), IDM_BENCHMARK);
    actions_[IDM_VIEW_FLAT_VIEW].append(w.flatView_);
    QObject::disconnect(w.flatView_, &QAction::toggled, &w, nullptr);
    QObject::connect(w.flatView_, &QAction::toggled, &w, [this](bool checked) {
        const bool previous = window_.app_.GetFlatMode();
        if (checked == previous)
            return;
        {
            QSignalBlocker block(window_.flatView_);
            window_.flatView_->setChecked(previous);
        }
        window_.app_.ExecuteCommand(IDM_VIEW_FLAT_VIEW);
    });
    w.flatView_->setProperty("nativeCommandID", IDM_VIEW_FLAT_VIEW);
    BindToggle(w.standardToolbarOption_, IDM_VIEW_STANDARD_TOOLBAR, &CApp::ShowStandardToolbar);
    BindToggle(w.archiveToolbarOption_, IDM_VIEW_ARCHIVE_TOOLBAR, &CApp::ShowArchiveToolbar);
    BindToggle(w.buttonTextOption_, IDM_VIEW_TOOLBARS_SHOW_BUTTONS_TEXT, &CApp::ShowButtonsLables);
    BindToggle(w.largeButtonsOption_, IDM_VIEW_TOOLBARS_LARGE_BUTTONS, &CApp::LargeButtons);
    for (unsigned i = 0; i < 4; ++i)
        Bind(w.viewActions_[i], IDM_VIEW_LARGE_ICONS + i);
    const unsigned sortIDs[] = {IDM_VIEW_ARANGE_BY_NAME, IDM_VIEW_ARANGE_BY_TYPE,
                                IDM_VIEW_ARANGE_BY_DATE, IDM_VIEW_ARANGE_BY_SIZE,
                                IDM_VIEW_ARANGE_NO_SORT};
    for (unsigned i = 0; i < 5; ++i)
        Bind(w.sortActions_[i], sortIDs[i]);
    for (unsigned i = 0; i < 5; ++i)
        Bind(w.timeActions_[i], IDM_VIEW_TIME + i);
    Bind(w.utcTimeAction_, IDM_VIEW_TIME_UTC);
    for (unsigned i = 0; i < 10; ++i) {
        Bind(w.openBookmarks_[i], 830 + i);
        Bind(w.saveBookmarks_[i], 810 + i);
    }
}

bool QtFileManagerCommands::CanSwitchPanels() const {
    const auto &w = window_;
    return !w.app_.QtOperations().closed() && !w.app_.QtOperations().active() &&
           !w.sourceArchiveProgress_ &&
           !(w.projectedUi().filesystemScanner && w.projectedUi().filesystemScanner->isRunning());
}

bool QtFileManagerCommands::CanExit() const {
    const auto &w = window_;
    return !w.app_.QtOperations().closed() && !w.app_.QtOperations().callActive() &&
           !w.sourceArchiveProgress_ && !QApplication::activeModalWidget();
}

bool QtFileManagerCommands::CanExecuteCommand(unsigned id) {
    auto &w = window_;
    if (w.app_.QtOperations().closed())
        return false;
    if (id >= kMenuCmdID_Plugin_Start)
        return w.programFileMenu_ && w.programFileMenu_->CanInvoke(id);
    if (id == 8)
        return CanExit();
    if (id == IDM_VIEW_TWO_PANELS)
        return CanSwitchPanels();
    return Availability().value(id, false);
}

QtFileManagerCommands::TransportCapabilities QtFileManagerCommands::ReadTransportCapabilities() {
    auto &w = window_;
    TransportCapabilities c;
    auto &panel = w.app_.GetFocusedPanel();
    const auto focus = panel.GetFocusState();
    CRecordVector<UInt32> operated;
    panel.Get_ItemIndices_Operated(operated);
    c.selected = !operated.IsEmpty();
    c.one = operated.Size() == 1;
    c.operated = !operated.IsEmpty();
    c.focused = focus.Present;
    c.focusedNonParent = focus.Present && !focus.Parent;
    c.selectedParent = focus.Present && focus.Parent && focus.Selected;
    c.focusedFile = panel.IsFocusedFile();
    c.comment = panel.CanComment();
    CMyComPtr<IFolderCalcItemFullSize> calcItemFullSize;
    if (panel.GetFolder())
        panel.GetFolder()->QueryInterface(IID_IFolderCalcItemFullSize,
                                          reinterpret_cast<void **>(&calcItemFullSize));
    if (calcItemFullSize)
        FOR_VECTOR(i, operated)
    if (panel.IsItem_Folder(operated[i]))
        c.folderSize = true;
    c.oneFile = c.one && !panel.IsItem_Folder(operated[0]);
    c.filesystem = panel.GetFolder() && panel.IsFSFolder();
    c.filesystemStreams = panel.GetFolder() && panel.IsAltStreamsFolder();
    c.copy = panel.DoesItSupportOperations() &&
             (!panel.IsFSDrivesFolder() || panel.IsDeviceDrivesPrefix());
    c.root = panel.GetFolder() && panel.IsRootFolder();
    {
        const auto catalog = w.backend_->codecCatalog();
        c.batchArchives = catalog && catalog->result() == S_OK;
        CExternalCodecs providers;
        if (w.backend_->loadBenchmarkProviders(providers) == S_OK) {
            CMethodId method;
            UInt32 streams;
            bool filter;
            c.benchmark =
                FindMethod_Index(&providers, AString("LZMA"), true, method, streams, filter) >= 0 &&
                streams == 1 && !filter &&
                FindMethod_Index(&providers, AString("LZMA"), false, method, streams, filter) >= 0;
        }
    }
    if (panel.DoesItSupportOperations()) {
        const auto catalog = w.backend_->codecCatalog();
        CExternalCodecs providers;
        if (w.backend_->loadHashProviders(providers) == S_OK) {
            for (QAction *action : w.hashActions_) {
                const unsigned command = action->property("nativeCommandID").toUInt();
                const QByteArray method = action->objectName().mid(4).chopped(6).toLatin1();
                CMethodId id;
                if (method == "*") {
                    CRecordVector<CMethodId> methods;
                    GetHashMethods(&providers, methods);
                    c.hashMethods[command] = !methods.IsEmpty();
                } else
                    c.hashMethods[command] =
                        FindHashMethod(&providers, AString(method.constData()), id);
            }
        }
    }
    c.archive = panel.GetFolder() && panel.IsArcFolder();
    c.nested = w.isNestedArchive();
    c.mutationWritable = c.archive && !panel.IsThereReadOnlyFolder();
    const auto *agent = dynamic_cast<CAgentFolder *>(panel.GetFolder());
    const auto *resource =
        agent ? dynamic_cast<QtArchiveFileResource *>(agent->GetEngineResource()) : nullptr;
    c.writable = c.mutationWritable && !(resource && resource->Info.encrypted);
    c.rows = w.projectedUi().table->rowCount() > 0;
    UStringVector history;
    w.app_.AppState.FolderHistory.GetList(history);
    c.foldersHistory = !history.IsEmpty();
    if (c.oneFile && c.filesystem) {
        const QFileInfo file(ToQString(panel.GetItemFullPath(operated[0])));
        c.split = !file.isSymLink();
        CVolSeqName sequence;
        c.combine = c.split && sequence.ParseName(ToUString(file.fileName()));
    }
    return c;
}

QHash<unsigned, bool> QtFileManagerCommands::Availability() {
    auto &w = window_;
    QHash<unsigned, bool> availability;
    if (w.app_.QtOperations().closed())
        return availability;
    auto &panel = w.app_.GetFocusedPanel();
    const bool navigating = !w.app_.QtOperations().blocking() && !w.sourceArchiveProgress_;
    availability[IDM_VIEW_TWO_PANELS] = CanSwitchPanels();
    // Enumeration disables list notifications too, but its flat-view toggle
    // remains the cancellation command. Only folder-property queries need
    // the notification guard; native update can replace the folder proxy.
    const bool safeFolderQueries = navigating && panel.ShouldProcessNotifications();
    availability[IDM_HELP_CONTENTS] = navigating && !QApplication::activeModalWidget();
    availability[IDM_ABOUT] = availability[IDM_HELP_CONTENTS];
    availability[IDM_TEMP_DIR] = availability[IDM_HELP_CONTENTS];
    availability[OpenArchivePicker] = availability[IDM_HELP_CONTENTS] && !w.backend_->busy();
    availability[ManageMounts] = availability[IDM_HELP_CONTENTS];
    if (availability[OpenArchivePicker] && safeFolderQueries && !w.projectedUi().scanningArchive &&
        !w.projectedUi().scanningDisk) {
        const auto *folder = dynamic_cast<CAgentFolder *>(panel.GetFolder());
        const auto *resource =
            folder ? dynamic_cast<QtArchiveFileResource *>(folder->GetEngineResource()) : nullptr;
        availability[MountReadOnly] =
            resource && resource->NativeReaderLease() && resource->NativeItemProperties();
    }
    availability[OpenDevicePicker] =
        availability[OpenArchivePicker] && safeFolderQueries && !w.projectedUi().scanningDisk &&
        !w.projectedUi().scanningArchive && w.backend_->codecCatalog() &&
        w.backend_->codecCatalog()->result() == S_OK;
    const bool hasFolder = panel.GetFolder() != nullptr;
    const bool namespaceChild = safeFolderQueries &&
                                w.projectedUi().state == MainWindow::State::Namespace &&
                                hasFolder && !panel.IsRootFolder();
    availability[Back] = navigating && (w.projectedUi().state == MainWindow::State::Viewing ||
                                        namespaceChild || !w.projectedUi().history.isEmpty());
    availability[IDM_OPEN_PARENT_FOLDER] = safeFolderQueries && hasFolder &&
                                           !panel.IsRootFolder() &&
                                           (w.projectedUi().state == MainWindow::State::Viewing ||
                                            panel.IsFSFolder() || namespaceChild);
    availability[IDM_VIEW_FLAT_VIEW] = navigating && (w.projectedUi().scanningDisk || !hasFolder ||
                                                      (safeFolderQueries && !panel.IsRootFolder()));
    availability[8] = CanExit();
    if (!safeFolderQueries || w.projectedUi().scanningDisk || !hasFolder)
        return availability;
    const TransportCapabilities capabilities = ReadTransportCapabilities();
    QtFileMenuProjection menu;
    ReadRegDiff(menu.diffPath, MainWindow::toolbarSettingsPath());
    ReadReg_VerCtrlPath(menu.versionControlPath, MainWindow::toolbarSettingsPath());
    QtDialogScreenServices screen(&w);
    menu.screenServices = &screen;
    // Only command IDs are needed for capability projection; rendered strings
    // and HMENU ownership remain Qt presentation, not a second functional menu.
    for (auto it = actions_.cbegin(); it != actions_.cend(); ++it)
        if (it.key() < IDM_VER_EDIT || it.key() > IDM_VER_DIFF)
            menu.sourceItems.append({it.key(), UString(), false});
    w.app_.GetFocusedPanel().CreateFileMenu(menu, true);
    for (auto it = actions_.cbegin(); it != actions_.cend(); ++it) {
        if (!availability.contains(it.key()))
            availability[it.key()] =
                menu.IsEnabled(it.key()) && TransportAllows(it.key(), capabilities);
    }
    return availability;
}

bool QtFileManagerCommands::TransportAllows(unsigned id, const TransportCapabilities &c) const {
    switch (id) {
    case IDM_BENCHMARK:
    case IDM_BENCHMARK2:
        return c.benchmark;
    case kMenuCmdID_Toolbar_Add:
        return c.filesystem || c.mutationWritable;
    case kMenuCmdID_Toolbar_Extract:
        return c.archive || c.batchArchives;
    case kMenuCmdID_Toolbar_Test:
        return c.archive || c.batchArchives;
    case IDM_OPEN:
        return c.operated || c.selectedParent;
    case IDM_OPEN_OUTSIDE:
        return (c.operated || c.selectedParent) && !c.root;
    case IDM_OPEN_INSIDE:
    case IDM_OPEN_INSIDE_ONE:
    case IDM_OPEN_INSIDE_PARSER:
        return c.focused;
    case IDM_FILE_VIEW:
        return c.folderSize || (c.focusedFile && (c.filesystem || c.archive));
    case IDM_FILE_EDIT:
        return c.focusedFile && (c.filesystem || c.archive);
    case IDM_DIFF:
        return c.filesystem;
    case IDM_VER_EDIT:
    case IDM_VER_COMMIT:
    case IDM_VER_REVERT:
    case IDM_VER_DIFF:
        return c.filesystem && c.oneFile;
    case IDM_COPY_TO:
        return !c.root && c.copy;
    case IDM_PROPERTIES:
        return c.archive || (c.selected && !c.root);
    case IDM_MOVE_TO:
        return c.filesystem || c.filesystemStreams || c.writable;
    case IDM_DELETE:
        return c.selected && (c.filesystem || c.filesystemStreams || c.mutationWritable);
    case IDM_RENAME:
        return c.focusedNonParent && (c.filesystem || c.filesystemStreams || c.mutationWritable);
    case IDM_CREATE_FOLDER:
        return c.filesystem || c.mutationWritable;
    case IDM_CREATE_FILE:
        return c.filesystem || c.filesystemStreams;
    case IDM_COMMENT:
        return c.comment;
    case IDM_LINK:
        return c.filesystem && c.one && c.selected;
    case IDM_ALT_STREAMS:
        return !c.operated || c.one; // Source OpenAltStreams accepts zero or one item.
    case IDM_SPLIT:
        return c.split;
    case IDM_COMBINE:
        return c.combine;
    case IDM_CRC32:
    case IDM_CRC64:
    case IDM_XXH64:
    case IDM_MD5:
    case IDM_SHA1:
    case IDM_SHA256:
    case IDM_SHA384:
    case IDM_SHA512:
    case IDM_SHA3_256:
    case IDM_BLAKE2SP:
    case IDM_HASH_ALL:
        return c.selected && !c.root && c.hashMethods.value(id);
    case IDM_SELECT_ALL:
    case IDM_INVERT_SELECTION:
    case IDM_SELECT:
    case IDM_DESELECT:
        return c.rows;
    case IDM_DESELECT_ALL:
        return c.selected;
    case IDM_SELECT_BY_TYPE:
    case IDM_DESELECT_BY_TYPE:
        return c.focused;
    case IDM_FOLDERS_HISTORY:
        return c.foldersHistory;
    }
    return true;
}
