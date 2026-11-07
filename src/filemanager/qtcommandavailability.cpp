#include "7zip/PropID.h"
#include "7zip/UI/FileManager/resource.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/resources/qtarchivefileresource.h"
#include "archive/resources/qtarchiveproperties.h"
#include "filemanager/qtfilemanagercommands.h"
#include "filemanager/qtfilemenuprojection.h"
#include "filemanager/qtprogramfilemenu.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtwindowworkspace.h"
#include "ui/dialogs/qtdialogscreenservices.h"

#include "filemanager/MyLoadMenu.h"
#include <QApplication>
#include <QFileInfo>
#include <QScopedValueRollback>

QHash<unsigned, bool> QtFileManagerCommands::AvailabilityForPanel(unsigned panel) {
    if (panel >= kNumPanelsMax || !window_.panelUi_[panel].table)
        return {};
    const QScopedValueRollback<unsigned> projected(window_.projectedPanel_, panel);
    const QScopedValueRollback<unsigned> focused(window_.app_.LastFocusedPanel, panel);
    return Availability();
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
    c.one = operated.Size() == 1;
    c.operated = !operated.IsEmpty();
    c.focused = focus.Present;
    c.focusedNonParent = focus.Present && !focus.Parent;
    c.selectedParent = focus.Present && focus.Parent && focus.Selected;
    c.focusedFile = panel.IsFocusedFile();
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
    c.trash = c.filesystem && !panel.IsThereReadOnlyFolder();
    c.filesystemStreams = panel.GetFolder() && panel.IsAltStreamsFolder();
    c.copy = panel.DoesItSupportOperations() &&
             (!panel.IsFSDrivesFolder() || panel.IsDeviceDrivesPrefix());
    c.root = panel.GetFolder() && panel.IsRootFolder();
    {
        const auto catalog = w.backend_->codecCatalog();
        c.batchArchives = catalog && catalog->result() == S_OK;
    }
    if (panel.DoesItSupportOperations()) {
        QHash<unsigned, QString> requested;
        for (auto it = w.hashActions_.cbegin(); it != w.hashActions_.cend(); ++it)
            if (const char *method = GetHashMethodForCommand(it.key()))
                requested.insert(it.key(), QString::fromLatin1(method));
        const QStringList available = w.backend_->supportedHashMethods(requested.values());
        for (auto it = requested.cbegin(); it != requested.cend(); ++it)
            c.hashMethods[it.key()] = available.contains(it.value());
    }
    c.archive = panel.GetFolder() && panel.IsArcFolder();
    c.mutationWritable = c.archive && !panel.IsThereReadOnlyFolder();
    const auto *agent = dynamic_cast<CAgentFolder *>(panel.GetFolder());
    const auto *resource =
        agent ? dynamic_cast<QtArchiveFileResource *>(agent->GetEngineResource()) : nullptr;
    c.writable = c.mutationWritable && !(resource && resource->Info.encrypted);
    c.rows = w.projectedUi().table->rowCount() > 0;
    UStringVector history;
    w.app_.AppState.FolderHistory.GetList(history);
    c.foldersHistory = !history.IsEmpty();
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
    const bool safeFolderQueries =
        navigating && !w.projectedUi().locationUnavailable && panel.ShouldProcessNotifications();
    const bool windowCommands = navigating && !QApplication::activeModalWidget();
    availability[IDM_ABOUT] = windowCommands;
    availability[OpenArchivePicker] = windowCommands && !w.backend_->busy();
    availability[ManageMounts] = windowCommands;
    availability[IDM_FOLDERS_HISTORY] = navigating && !w.projectedUi().scanningDisk &&
                                        !w.projectedUi().scanningArchive &&
                                        !QApplication::activeModalWidget();
    if (availability[OpenArchivePicker] && safeFolderQueries && !w.projectedUi().scanningArchive &&
        !w.projectedUi().scanningDisk) {
        const auto *folder = dynamic_cast<CAgentFolder *>(panel.GetFolder());
        availability[MountReadOnly] =
            folder && ArchiveUsesNativeItemIndexes(folder->GetArchiveProperties());
        availability[CalculateDirectorySize] = CanCalculateDirectorySize();
    }
    availability[OpenDevicePicker] =
        availability[OpenArchivePicker] && safeFolderQueries && !w.projectedUi().scanningDisk &&
        !w.projectedUi().scanningArchive && w.backend_->codecCatalog() &&
        w.backend_->codecCatalog()->result() == S_OK;
    const bool hasFolder = panel.GetFolder() != nullptr;
    const bool namespaceChild = safeFolderQueries &&
                                w.projectedUi().state == MainWindow::State::Namespace &&
                                hasFolder && !panel.IsRootFolder();
    availability[Back] = safeFolderQueries && w.workspace_->CanTravel(w.projectedPanel_, -1);
    availability[Forward] = safeFolderQueries && w.workspace_->CanTravel(w.projectedPanel_, 1);
    availability[IDM_OPEN_PARENT_FOLDER] = safeFolderQueries && hasFolder &&
                                           !panel.IsRootFolder() &&
                                           (w.projectedUi().state == MainWindow::State::Viewing ||
                                            panel.IsFSFolder() || namespaceChild);
    availability[8] = CanExit();
    if (!safeFolderQueries || w.projectedUi().scanningDisk || !hasFolder)
        return availability;
    const TransportCapabilities capabilities = ReadTransportCapabilities();
    QtFileMenuProjection menu;
    QtDialogScreenServices screen(&w);
    menu.screenServices = &screen;
    // Only command IDs are needed for capability projection; rendered strings
    // and HMENU ownership remain Qt presentation, not a second functional menu.
    for (auto it = actions_.cbegin(); it != actions_.cend(); ++it)
        menu.sourceItems.append({it.key(), UString(), false});
    w.app_.GetFocusedPanel().CreateFileMenu(menu, true);
    for (auto it = actions_.cbegin(); it != actions_.cend(); ++it) {
        if (!availability.contains(it.key()))
            availability[it.key()] =
                menu.IsEnabled(it.key()) && TransportAllows(it.key(), capabilities);
    }
    availability[SortAscending] = availability.value(IDM_VIEW_ARANGE_BY_NAME);
    availability[SortDescending] = availability[SortAscending];
    return availability;
}

bool QtFileManagerCommands::TransportAllows(unsigned id, const TransportCapabilities &c) const {
    switch (id) {
    case kMenuCmdID_Toolbar_Add:
        return c.filesystem || c.mutationWritable;
    case kMenuCmdID_Toolbar_Extract:
        return c.archive || (c.filesystem && c.oneFile && c.batchArchives);
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
    case IDM_COPY_TO:
        return !c.root && c.copy;
    case IDM_PROPERTIES:
        return c.archive || (c.operated && !c.root);
    case IDM_MOVE_TO:
        return c.filesystem || c.filesystemStreams || c.writable;
    case IDM_DELETE:
        return c.operated && c.trash;
    case IDM_RENAME:
        return c.focusedNonParent && (c.filesystem || c.filesystemStreams || c.mutationWritable);
    case IDM_CREATE_FOLDER:
        return c.filesystem || c.mutationWritable;
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
        return c.operated && !c.root && c.hashMethods.value(id);
    case IDM_SELECT_ALL:
        return c.rows;
    case IDM_DESELECT_ALL:
        return c.operated;
    case IDM_FOLDERS_HISTORY:
        return c.foldersHistory;
    }
    return true;
}
