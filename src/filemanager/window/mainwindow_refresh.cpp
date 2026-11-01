#include "archive/resources/qtarchivereopen.h"
#include "filemanager/providers/qtlocationfolder.h"
#include "filemanager/window/mainwindow.h"
#include "localization/qtlanguageservices.h"

#include <QScopeGuard>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <utility>

HRESULT MainWindow::reloadPanelPresentation(unsigned index, const CSelectedState &selection) {
    if (!panelUi_[index].container ||
        (app_.QtOperations().active() &&
         (app_.QtOperations().blocking() ||
          (app_.Panels[index].PanelCreated && (scanningPanelMask() & (1u << index))))))
        return S_OK;
    // Source OnCreate owns the initial refresh before PanelCreated is set.
    // OnCopy also refreshes both panels. A different panel's scan must not
    // suppress this publication; the operation owner retains each scan's guard.
    app_.Panels[index].TakeRefreshSelection();
    const unsigned original = projectedPanel_;
    const QString originalStatus = statusBar()->currentMessage();
    if (index != original) {
        // ReOpen can retire this panel's proxy before its old rows are cleared.
        // Do not derive command state from those rows during activation.
        CPanel::CDisableNotify notify(app_.Panels[index]);
        activatePanelUi(index);
    }
    // A native modal close can restore peer focus while the Qt projection is
    // held on this command owner. Source RefreshListCtrl reloads its own panel.
    app_.SetFocusedPanel(index);
    const auto restore = qScopeGuard([&] {
        if (index != original) {
            activatePanelUi(original);
            statusBar()->showMessage(originalStatus);
        }
    });
    const auto &panel = app_.Panels[index];
    const bool archive = panel.IsArcFolder();
    const bool root = !archive && (panel.IsRootFolder() || panel.IsFSDrivesFolder() ||
                                   panel.IsNetFolder() || panel.IsAltStreamsFolder());
    if (archive || root || projectedUi().state != State::Filesystem ||
        QDir::cleanPath(ToQString(GetFolderPath(panel.GetFolder()))) != projectedUi().directory) {
        const HRESULT loaded = root ? renderRootFolder(!selection.CalledFromTimer)
                                    : renderBoundFolder(!selection.CalledFromTimer);
        if (loaded == S_OK)
            restoreListSelection(selection);
        return loaded;
    }
    if (!QFileInfo(projectedUi().directory).isDir()) {
        const HRESULT loaded = app_.GetFocusedPanel().LoadListItems();
        auto &snapshot = panelUi_[index].autoRefreshSnapshot;
        snapshot.clear();
        snapshot.insert(projectedUi().directory, filesystemSignature(projectedUi().directory));
        projectedUi().table->clearSelection();
        projectedUi().table->clearRows();
        statusBar()->showMessage(
            QCoreApplication::translate("mainwindow_refresh", "Folder not found: ") +
            projectedUi().directory);
        refreshActions();
        return loaded;
    }
    const int firstRow = projectedUi().table->rowAt(0);
    const FilesystemViewport viewport(
        projectedUi().directory, projectedUi().table->model()->index(firstRow, 1).data().toString(),
        projectedUi().table->verticalScrollBar()->value());
    return refreshFilesystem(selection, true, viewport);
}

HRESULT MainWindow::refreshFilesystem(const CSelectedState &selection, bool sourceAlreadyBound,
                                      std::optional<FilesystemViewport> viewport) {
    const HRESULT loaded = showDirectory(projectedUi().directory, false, sourceAlreadyBound,
                                         !selection.CalledFromTimer);
    if (loaded != S_OK)
        return loaded;
    panelUi_[projectedPanel_].pendingViewport = std::move(viewport);
    restoreListSelection(selection);
    if (!projectedUi().scanningDisk) {
        restoreAutoRefreshViewport();
        refreshActions();
    }
    return S_OK;
}

void MainWindow::refreshCurrent(const CSelectedState *preserved) {
    if (app_.QtOperations().closed())
        return;
    QtAppOperationState::CallScope call(app_.QtOperations());
    if (projectedUi().state != State::Namespace && projectedUi().state != State::Filesystem &&
        projectedUi().state != State::Viewing)
        return;
    auto &panel = app_.GetFocusedPanel();
    CSelectedState selection;
    if (preserved)
        selection = *preserved;
    else
        panel.SaveSelectedState(selection);
    if (projectedUi().state == State::Namespace) {
        renderRootFolder();
        panel.RestoreLoadedSelection(selection);
        refreshActions();
        return;
    }
    if (projectedUi().state == State::Filesystem) {
        refreshFilesystem(selection, false);
    } else {
        CMyComPtr<IFolderFolder> oldFolder = panel.GetFolder();
        auto *folder = dynamic_cast<CAgentFolder *>((IFolderFolder *)oldFolder);
        if (!folder)
            return;
        const UString previousFolderPath = GetFolderPath(oldFolder);
        setState(State::Loading);
        CMyComPtr<IFolderFolder> refreshed;
        HRESULT result;
        {
            QScopedValueRollback<bool> source(sourceArchiveProgress_, true);
            const auto &parents = panel.GetParentFolders();
            result = ReOpenArchiveFolder(*folder, toolbarSettingsPath(), this, &refreshed,
                                         !parents.IsEmpty() && parents.Back().UsePassword,
                                         parents.IsEmpty() ? UString() : parents.Back().Password);
        }
        if (result == S_OK) {
            panel.SetNewFolder(refreshed);
            renderBoundFolder();
            restoreListSelection(GetFolderPath(refreshed) == previousFolderPath ? selection
                                                                                : CSelectedState());
            refreshActions();
        } else {
            // Native ReOpen releases the old proxy; never render stale folders.
            showDirectory(projectedUi().returnDirectory, false);
            if (result != E_ABORT)
                QMessageBox::warning(this, QStringLiteral("7-Zip"),
                                     QCoreApplication::translate("mainwindow_refresh",
                                                                 "Could not update archive (%1).")
                                         .arg(quint32(result), 0, 16));
        }
    }
}

MainWindow::FilesystemSignature MainWindow::filesystemSignature(const QString &path) const {
    const QFileInfo info(path);
    return {info.lastModified(), info.metadataChangeTime(), info.size(), info.exists(),
            info.isDir()};
}

void MainWindow::pollAutoRefresh() {
    // Native modal commands hold panel timer/notification guards while their
    // workers can rebuild archive proxies. Do not change the Qt projection
    // (and query the other panel) before CPanel::OnTimer sees those guards.
    if (projectedUi().scanningDisk || app_.QtOperations().active() ||
        app_.QtOperations().closed() || app_.QtOperations().callActive() || sourceArchiveProgress_)
        return;
    // Unlike native WM_TIMER, the Qt timer visits both panel projections.
    // Their activation queries status, so honor source notification/status
    // guards before entering either projection while a worker can reopen it.
    for (unsigned index = 0; index < kNumPanelsMax; ++index)
        if (app_.IsPanelVisible(index) && !app_.Panels[index].ShouldProcessNotifications())
            return;
    const unsigned original = projectedPanel_;
    for (unsigned index = 0; index < kNumPanelsMax; ++index) {
        if (!app_.IsPanelVisible(index) || !panelUi_[index].container)
            continue;
        if (index != projectedPanel_)
            activatePanelUi(index);
        if (projectedUi().state == State::Filesystem) {
            CPanel &panel = app_.GetFocusedPanel();
            const auto &snapshot = panelUi_[index].autoRefreshSnapshot;
            if (panel.AutoRefresh_Mode) {
                for (auto it = snapshot.cbegin(); it != snapshot.cend(); ++it) {
                    if (!(filesystemSignature(it.key()) == it.value())) {
                        panel.MarkPortableFolderChanged();
                        break;
                    }
                }
            }
            panel.OnTimer();
        }
        if (app_.QtOperations().active())
            break;
    }
    if (original != projectedPanel_)
        activatePanelUi(original);
}

void MainWindow::restoreAutoRefreshViewport() {
    const auto viewport = std::exchange(panelUi_[projectedPanel_].pendingViewport, std::nullopt);
    if (!viewport || viewport->directory != projectedUi().directory)
        return;
    int row = -1;
    for (int i = 0; i < projectedUi().table->rowCount(); ++i) {
        if (projectedUi().table->model()->index(i, 1).data().toString() == viewport->topPath) {
            row = i;
            break;
        }
    }
    if (row >= 0)
        projectedUi().table->scrollTo(projectedUi().table->model()->index(row, 0),
                                      QAbstractItemView::PositionAtTop);
    else
        projectedUi().table->verticalScrollBar()->setValue(viewport->scrollValue);
}
