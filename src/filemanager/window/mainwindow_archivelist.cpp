#include "7zip/PropID.h"
#include "Common/Defs.h"
#include "Windows/PropVariant.h"
#include "Windows/PropVariantConv.h"
#include "archive/resources/qtarchivedirectoryread.h"
#include "filemanager/FormatUtils.h"
#include "filemanager/SysIconUtils.h"
#include "filemanager/panels/qtpaneladdresscombo.h"
#include "filemanager/providers/qtlocationfolder.h"
#include "filemanager/window/mainwindow.h"
#include "localization/qtlanguageservices.h"
#include <QScopedValueRollback>

HRESULT MainWindow::renderArchiveFolder(bool updateAddress) {
    cancelListPublication();
    auto &panel = app_.GetFocusedPanel();
    std::optional<CPanel::CDisableNotify> disableNotify;
    disableNotify.emplace(panel);
    CMyComPtr<IFolderFolder> folderOwner = panel.GetFolder();
    if (!folderOwner)
        return S_OK;
    auto *archiveFolder = static_cast<CAgentFolder *>((IFolderFolder *)folderOwner);
    const bool flat = panel.GetFlatMode();
    const HRESULT directoryResult =
        ReadArchiveDirectory(*archiveFolder, flat, this, toolbarSettingsPath());
    if (directoryResult != S_OK) {
        projectedUi().table->clearRows();
        panel.LoadListItems(true, directoryResult);
        if (directoryResult != E_ABORT)
            panel.MessageBox_Error_HRESULT(directoryResult);
        return directoryResult;
    }
    const HRESULT loaded = panel.LoadListItems();
    if (loaded != S_OK)
        return loaded;
    static_cast<FileTable *>(projectedUi().table)->filesystem = false;
    panelUi_[projectedPanel_].autoRefreshSnapshot.clear();
    panel.LoadFullPath();
    recordVisitedPath(ToQString(panel.GetCurrentFolderPrefix()));
    const QString prefix =
        projectedUi().archiveFolder.isEmpty() ? QString() : projectedUi().archiveFolder + '/';
    projectedUi().table->setSortingEnabled(false);
    if (updateAddress)
        static_cast<QtPanelAddressCombo *>(projectedUi().pathCombo)
            ->ShowCurrentPath(archiveDisplayPath());
    configureColumns();
    appendParentEntry();
    if (archiveFolder->GetNumberOfItems() > 256) {
        // Finish the synchronous notification guard before retaining operation
        // guards. Otherwise an idle flat-view change saves disabled flags
        // and leaves notifications disabled when asynchronous loading ends.
        disableNotify.reset();
        projectedUi().scanningArchive = true;
        setState(State::Viewing);
        projectedUi().listProjection->start(
            folderOwner, archiveFolder->GetNumberOfItems(),
            [this, owner = projectedPanel_, archiveFolder, prefix, flat](quint32 index) {
                HRESULT result = S_OK;
                withPanelProjection(owner, [&] {
                    result = appendArchiveEntry(*archiveFolder, index, prefix, flat);
                });
                return result;
            },
            [this, owner = projectedPanel_,
             completed = panel.GetReloadCompletion()](HRESULT result) {
                withPanelProjection(owner, [this, result, &completed] {
                    // Restore controls and source guards before reporting the
                    // original caller's result, and before Copy can continue.
                    QScopedValueRollback<unsigned> projection(panelProjectionDepth_,
                                                              panelProjectionDepth_ + 1);
                    if (result != S_OK) {
                        projectedUi().table->clearRows();
                        panelUi_[projectedPanel_].pendingSelection.reset();
                    }
                    projectedUi().scanningArchive = false;
                    applySortMode(true);
                    completeListSelection();
                    setState(State::Viewing);
                    if (completed)
                        completed(result);
                    if (app_.QtOperations().closed())
                        return;
                    if (result == S_OK)
                        statusBar()->clearMessage();
                });
                if (!app_.QtOperations().closed())
                    refreshActions();
            });
        return S_OK;
    }
    for (unsigned index = 0; index < archiveFolder->GetNumberOfItems(); ++index) {
        const HRESULT result = appendArchiveEntry(*archiveFolder, index, prefix, flat);
        if (result != S_OK) {
            projectedUi().table->clearRows();
            return result;
        }
    }
    applySortMode(true);
    statusBar()->clearMessage();
    refreshActions();
    return S_OK;
}

HRESULT MainWindow::appendArchiveEntry(CAgentFolder &folder, unsigned index, const QString &prefix,
                                       bool flat) {
    QIcon icon;
    RINOK(app_.GetFocusedPanel().GetItemIcon(index, icon))
    FilePanelModel::Row row;
    row.sourceIndex = index;
    row.directory = app_.GetFocusedPanel().IsItem_Folder(index);
    row.name = ToQString(folder.GetName(index));
    const QString panelPrefix = QDir::fromNativeSeparators(ToQString(folder.GetFullPrefix(index)));
    row.path = panelPrefix + row.name;
    row.operationPath = row.path;
    row.prefix = flat ? panelPrefix.mid(prefix.size()) : QString();
    row.toolTip = row.path;
    row.icon = icon;
    projectedUi().table->panelModel()->append(std::move(row));
    return S_OK;
}
