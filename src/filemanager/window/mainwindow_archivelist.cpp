#include "7zip/PropID.h"
#include "Common/Defs.h"
#include "Windows/PropVariant.h"
#include "Windows/PropVariantConv.h"
#include "archive/resources/qtarchivefileresource.h"
#include "filemanager/FormatUtils.h"
#include "filemanager/SysIconUtils.h"
#include "filemanager/panels/PanelSort.h"
#include "filemanager/panels/PanelSplitFile.h"
#include "filemanager/panels/qtpaneladdresscombo.h"
#include "filemanager/providers/qtlocationfolder.h"
#include "filemanager/qtfilemanagercommands.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtapppresentation.h"
#include "localization/qtlangcontrols.h"
#include "localization/qtlanguageservices.h"
#include <QScopedValueRollback>

HRESULT MainWindow::renderArchiveFolder(bool updateAddress) {
    cancelListPublication();
    auto &panel = app_.GetFocusedPanel();
    std::optional<CPanel::CDisableTimerProcessing> disableTimerProcessing;
    std::optional<CPanel::CDisableNotify> disableNotify;
    disableTimerProcessing.emplace(panel);
    disableNotify.emplace(panel);
    CMyComPtr<IFolderFolder> folderOwner = panel.GetFolder();
    if (!folderOwner)
        return S_OK;
    auto *archiveFolder = static_cast<CAgentFolder *>((IFolderFolder *)folderOwner);
    const bool flat = panel.GetFlatMode();
    const HRESULT loaded = panel.LoadListItems();
    if (loaded != S_OK)
        return loaded;
    static_cast<FileTable *>(table_)->filesystem = false;
    autoRefreshSnapshot_.clear();
    panel.LoadFullPath();
    recordVisitedPath(ToQString(panel.GetCurrentFolderPrefix()));
    const QString prefix = archiveFolder_.isEmpty() ? QString() : archiveFolder_ + '/';
    table_->setSortingEnabled(false);
    if (updateAddress)
        static_cast<QtPanelAddressCombo *>(pathCombo_)
            ->ShowCurrentPath(archiveDisplayPath(), false);
    configureColumns(true);
    appendParentEntry();
    if (archiveFolder->GetNumberOfItems() > 256) {
        // Finish the synchronous source guards before retaining operation
        // guards. Otherwise an idle flat-view change saves disabled flags
        // and leaves notifications disabled when asynchronous loading ends.
        disableNotify.reset();
        disableTimerProcessing.reset();
        scanningArchive_ = true;
        setState(State::Viewing);
        listProjection_->start(
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
                        table_->clearRows();
                        scanSelectedState_.reset();
                        scanSelectedPaths_.reset();
                    }
                    scanningArchive_ = false;
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
            table_->clearRows();
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
    table_->panelModel()->append(std::move(row));
    return S_OK;
}

void MainWindow::setState(State state) {
    const unsigned scanningPanels = scanningPanelMask();
    const auto kind = state == State::Loading     ? QtAppOperationState::Kind::Open
                      : state == State::Operating ? QtAppOperationState::Kind::Operation
                      : scanningPanels            ? QtAppOperationState::Kind::Scan
                                                  : QtAppOperationState::Kind::Idle;
    if (!(panelProjectionDepth_ && app_.QtOperations().blocking()) &&
        !app_.QtOperations().Begin(kind, scanningPanels))
        return;
    state_ = state;
    refreshActions();
}

void MainWindow::refreshActions() {
    // Disabling a view can synchronously move native Qt focus. Keep the
    // current source transition bound until its caller finishes scheduling.
    {
        QScopedValueRollback<unsigned> projection(panelProjectionDepth_, panelProjectionDepth_ + 1);
        app_.RefreshTitle();
        const bool navigating = !app_.QtOperations().closed() && !app_.QtOperations().blocking();
        const bool idle = navigating && !scanningDisk_ && !scanningArchive_;
        refreshBookmarks();
        if (commands_)
            commands_->RefreshActions();
        if (commands_ && !scanningDisk_ && !scanningArchive_ &&
            app_.GetFocusedPanel().ShouldProcessNotifications())
            refreshPanelStatus();
        for (unsigned index = 0; index < kNumPanelsMax; ++index) {
            const auto &ui = panelUi_[index];
            if (!ui.container || !ui.table || !ui.iconList || !ui.path)
                continue;
            const bool projecting = index == projectedPanel_
                                        ? scanningDisk_ || scanningArchive_
                                        : ui.scanningDisk || ui.scanningArchive;
            const bool panelIdle = navigating && !projecting;
            QWidget *focused = QApplication::focusWidget();
            if (projecting && focused &&
                (ui.table == focused || ui.table->isAncestorOf(focused) || ui.iconList == focused ||
                 ui.iconList->isAncestorOf(focused)))
                ui.path->setFocus();
            // RefreshListCtrl disables redraw until native sorting and selection
            // restoration finish. Keep that policy across Qt's asynchronous batches.
            ui.table->setUpdatesEnabled(!projecting);
            ui.iconList->setUpdatesEnabled(!projecting);
            ui.table->setEnabled(panelIdle);
            ui.iconList->setEnabled(panelIdle);
            // Native archive progress is modal to the file manager. Qt disables
            // its lists during the operation; disable the address controls too so
            // focus cannot move to an inactive pane while its source guards block
            // the corresponding focus notification.
            ui.pathCombo->setEnabled(navigating);
            ui.path->setReadOnly(!navigating);
        }
        const bool dragAvailable = idle && app_.GetFocusedPanel().DoesItSupportOperations() &&
                                   (state_ == State::Filesystem || state_ == State::Viewing);
        table_->setDragEnabled(dragAvailable);
        iconList_->setDragEnabled(dragAvailable);
    }
    if (appPresentation_ && !panelProjectionDepth_) {
        appPresentation_->CompleteInitialFocusIfReady();
        appPresentation_->CompleteCopyIfReady();
    }
}

void MainWindow::refreshPanelStatus() {
    auto &panel = app_.GetFocusedPanel();
    panel.Refresh_StatusBar();
    const auto direct = [](IFolderFolder *folder) {
        const auto *agent = dynamic_cast<CAgentFolder *>(folder);
        const auto *resource =
            agent ? dynamic_cast<QtArchiveFileResource *>(agent->GetEngineResource()) : nullptr;
        return resource;
    };
    bool device = false, limitedRead = false;
    const auto inspect = [&](IFolderFolder *folder) {
        if (const auto *resource = direct(folder)) {
            const bool filesystemLimitations = !resource->FilesystemReadLimitations().isEmpty();
            device |= resource->HasDirectInput() || filesystemLimitations;
            limitedRead |= resource->AllowsIncompleteDeviceRead() || filesystemLimitations;
        }
    };
    inspect(panel.GetFolder());
    const auto &parents = panel.GetParentFolders();
    FOR_VECTOR(i, parents) { inspect(parents[i].ParentFolder); }
    QtBindLocalText(deviceReadOnly_, "mainwindow_panel",
                    limitedRead
                        ? QT_TRANSLATE_NOOP("mainwindow_panel", "Read only · limited access")
                        : QT_TRANSLATE_NOOP("mainwindow_panel", "Read only"));
    deviceReadOnly_->setVisible(device);
    statusBar()->setVisible(device || !statusBar()->currentMessage().isEmpty());
}

void MainWindow::showPanelStatus(unsigned index, const CPanelStatus &status) {
    if (index >= kNumPanelsMax || app_.QtOperations().closed())
        return;
    const auto &labels = panelUi_[index].statusLabels;
    if (!labels[0])
        return;
    labels[0]->setText(QtSelectionStatusText(status.selectedCount, status.totalCount));
    labels[1]->setText(status.selectedSize);
    labels[2]->setText(status.focusedSize);
    labels[3]->setText(status.focusedDate);
}
