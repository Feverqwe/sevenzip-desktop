#include "7zip/PropID.h"
#include "Windows/PropVariant.h"
#include "filemanager/panels/qtpaneladdresscombo.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtwindowworkspace.h"
#include "localization/qtlanguageservices.h"

// Source namespace folders own item names, order and binding. This file presents
// their IFolderFolder items in the same list used by disk and archive views.
HRESULT MainWindow::renderRootFolder(bool updateAddress) {
    auto &panel = app_.GetFocusedPanel();
    if (!panel.GetFolder())
        return S_OK;
    const bool drives = panel.IsFSDrivesFolder();
    const bool network = panel.IsNetFolder();
    const bool streams = panel.IsAltStreamsFolder();
    if ((!panel.IsRootFolder() && !drives && !network && !streams) || !panel.GetFolder())
        return E_NOTIMPL;
    auto disableNotify = std::make_unique<CPanel::CDisableNotify>(panel);
    projectedUi().table->setSortingEnabled(false);
    IFolderFolder *folder = panel.GetFolder();
    cancelListPublication();
    projectedUi().archive.clear();
    projectedUi().displayArchive.clear();
    projectedUi().archivePassword.clear();
    projectedUi().archiveFolder.clear();
    panelUi_[projectedPanel_].autoRefreshSnapshot.clear();
    static_cast<FileTable *>(projectedUi().table)->filesystem = false;
    const QString folderPath =
        drives || network || streams ? ToQString(GetFolderPath(folder)) : QString();
    if (updateAddress)
        static_cast<QtPanelAddressCombo *>(projectedUi().pathCombo)->ShowCurrentPath(folderPath);
    const auto finish = [&](HRESULT result) {
        if (result != S_OK)
            projectedUi().table->clearRows();
        projectedUi().table->setSortingEnabled(true);
        applySortMode();
        // Restore the row-rebuild guard before End releases operation guards.
        // Otherwise this guard would restore its saved disabled state afterward.
        disableNotify.reset();
        setState(State::Namespace);
        if (result == S_OK)
            clearStatusMessage();
        else
            showStatusMessage(
                QCoreApplication::translate("mainwindow_root", "Could not read root folder."));
        return result;
    };
    HRESULT result = panel.LoadListItems();
    if (result != S_OK)
        return finish(result);
    const UInt32 count = panel.GetLoadedItemCount();
    configureColumns();
    appendParentEntry();
    for (UInt32 index = 0; index < count; ++index) {
        NWindows::NCOM::CPropVariant name;
        if (folder->GetProperty(index, kpidName, &name) != S_OK || name.vt != VT_BSTR)
            continue;
        const QString nativeName = QString::fromWCharArray(name.bstrVal);
        QIcon icon;
        result = panel.GetItemIcon(index, icon);
        if (result != S_OK)
            return finish(result);
        const bool isDirectory = panel.IsItem_Folder(index);
        FilePanelModel::Row row;
        row.sourceIndex = index;
        row.name = nativeName;
        row.directory = isDirectory;
        if (!isDirectory)
            row.operationPath = folderPath + nativeName;
        row.icon = QApplication::style()->standardIcon(!isDirectory ? QStyle::SP_FileIcon
                                                       : drives     ? QStyle::SP_DriveHDIcon
                                                       : network    ? QStyle::SP_DirIcon
                                                                    : QStyle::SP_ComputerIcon);
#ifdef Q_OS_WIN
        if (!panel.IsRootFolder())
            row.icon = icon;
#endif
        projectedUi().table->panelModel()->append(std::move(row));
    }
    return finish(S_OK);
}

void MainWindow::goRoot(bool remember, RootLocation location) {
    Q_UNUSED(remember);
    workspace_->Navigate([&] { return goRootImpl(location); });
}

HRESULT MainWindow::goRootImpl(RootLocation location) {
    if (app_.QtOperations().closed() || projectedUi().state == State::Loading ||
        projectedUi().state == State::Operating)
        return E_ABORT;
    QtAppOperationState::CallScope call(app_.QtOperations());
    workspace_->ProtectNavigation();
    setState(State::Operating);
    if (location == RootLocation::Drives)
        app_.OpenRootFolder();
    else
        app_.GetFocusedPanel().OpenRootFolder();
    const HRESULT result = renderBoundFolder();
    if (result == S_OK)
        projectedUi().locationUnavailable = false;
    return result;
}
