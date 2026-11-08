#include "archive/password/archivepassword.h"
#include "archive/resources/qtarchivereopen.h"
#include "filemanager/providers/qtlocationfolder.h"
#include "filemanager/rename/qtrenamecontroller.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtwindowworkspace.h"
#include "localization/qtlanguageservices.h"

#include <QScopedValueRollback>
#include <QScrollBar>
#include <utility>

HRESULT MainWindow::reloadPanelPresentation(unsigned index, const CSelectedState &selection) {
    if (!panelUi_[index].container || panelUi_[index].locationUnavailable ||
        (app_.QtOperations().active() &&
         (app_.QtOperations().blocking() ||
          (app_.Panels[index].PanelCreated && (scanningPanelMask() & (1u << index))))))
        return S_OK;
    // Source OnCreate owns the initial refresh before PanelCreated is set.
    // OnCopy also refreshes both panels. A different panel's scan must not
    // suppress this publication; the operation owner retains each scan's guard.
    app_.Panels[index].TakeRefreshSelection();
    HRESULT result = E_ABORT;
    withPanelProjection(index, [&] {
        // A native modal close can restore peer focus while the Qt projection
        // remains on this owner. Reload the source panel that made the request.
        app_.SetFocusedPanel(index);
        result = reloadProjectedPanel(selection);
        const auto &ui = panelUi_[index];
        if (result != S_OK || (!ui.scanningDisk && !ui.scanningArchive))
            workspace_->NavigationFinished(result);
    });
    return result;
}

HRESULT MainWindow::reloadProjectedPanel(const CSelectedState &selection) {
    const auto &panel = app_.GetFocusedPanel();
    const bool archive = panel.IsArcFolder();
    const bool root = !archive && (panel.IsRootFolder() || panel.IsFSDrivesFolder() ||
                                   panel.IsNetFolder() || panel.IsAltStreamsFolder());
    if (archive || root || projectedUi().state != State::Filesystem ||
        QDir::cleanPath(ToQString(GetFolderPath(panel.GetFolder()))) != projectedUi().directory) {
        const bool updateAddress = !selection.CalledFromTimer && !reloadingFileListSettings_;
        const HRESULT loaded =
            root ? renderRootFolder(updateAddress) : renderBoundFolder(updateAddress);
        if (loaded == S_OK)
            restoreListSelection(selection);
        return loaded;
    }
    if (!QFileInfo(projectedUi().directory).isDir()) {
        const HRESULT loaded = app_.GetFocusedPanel().LoadListItems();
        auto &snapshot = panelUi_[projectedPanel_].autoRefreshSnapshot;
        snapshot.clear();
        snapshot.insert(projectedUi().directory, filesystemSignature(projectedUi().directory));
        projectedUi().table->clearSelection();
        projectedUi().table->clearRows();
        showStatusMessage(QCoreApplication::translate("mainwindow_refresh", "Folder not found: ") +
                          projectedUi().directory);
        refreshActions();
        return loaded;
    }
    if (!selection.KeepViewport)
        return refreshFilesystem(selection, true);

    auto *view = qobject_cast<QAbstractItemView *>(projectedUi().viewStack->currentWidget());
    int topRow = -1;
    int topOffset = 0;
    if (view == projectedUi().table) {
        topRow = projectedUi().table->rowAt(0);
        if (topRow >= 0)
            topOffset = projectedUi().table->rowViewportPosition(topRow);
    } else {
        const QRect visible = view->viewport()->rect();
        for (int row = 0; row < view->model()->rowCount(); ++row) {
            const QRect item = view->visualRect(view->model()->index(row, 0));
            if (visible.intersects(item)) {
                topRow = row;
                topOffset = item.top();
                break;
            }
        }
    }
    const QString topPath = view->model()->index(topRow, 1).data().toString();
    const FilesystemViewport viewport(
        view, projectedUi().directory, topPath,
        QPoint(view->horizontalScrollBar()->value(), view->verticalScrollBar()->value()),
        topOffset);
    return refreshFilesystem(selection, true, viewport);
}

HRESULT MainWindow::refreshFilesystem(const CSelectedState &selection, bool sourceAlreadyBound,
                                      std::optional<FilesystemViewport> viewport) {
    const HRESULT loaded = showDirectory(projectedUi().directory, false, sourceAlreadyBound,
                                         !selection.CalledFromTimer && !reloadingFileListSettings_);
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
                                         parents.IsEmpty() ? NativePassword()
                                                           : parents.Back().Password.native());
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
    // Honor source notification/status guards before querying either folder
    // while a worker can reopen it.
    for (unsigned index = 0; index < kNumPanelsMax; ++index)
        if (app_.IsPanelVisible(index) && !app_.Panels[index].ShouldProcessNotifications())
            return;
    for (unsigned index = 0; index < kNumPanelsMax; ++index) {
        if (!app_.IsPanelVisible(index) || !panelUi_[index].container ||
            panelUi_[index].state != State::Filesystem || panelUi_[index].locationUnavailable ||
            renameController_->PendingFor(index))
            continue;
        withPanelProjection(index, [&] {
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
        });
        if (app_.QtOperations().active())
            break;
    }
}

void MainWindow::restoreAutoRefreshViewport() {
    const auto viewport = std::exchange(panelUi_[projectedPanel_].pendingViewport, std::nullopt);
    if (!viewport || viewport->directory != projectedUi().directory)
        return;
    auto *view = viewport->view.data();
    if (!view || view != projectedUi().viewStack->currentWidget())
        return;
    view->doItemsLayout();
    int row = -1;
    if (!viewport->topPath.isEmpty()) {
        for (int i = 0; i < projectedUi().table->rowCount(); ++i) {
            if (view->model()->index(i, 1).data().toString() == viewport->topPath) {
                row = i;
                break;
            }
        }
    }
    if (row < 0) {
        view->verticalScrollBar()->setValue(viewport->scrollPosition.y());
    } else if (view == projectedUi().table) {
        view->scrollTo(view->model()->index(row, 0), QAbstractItemView::PositionAtTop);
        if (view->verticalScrollMode() == QAbstractItemView::ScrollPerPixel) {
            auto *scroll = view->verticalScrollBar();
            const int displacement =
                projectedUi().table->rowViewportPosition(row) - viewport->topOffset;
            scroll->setValue(scroll->value() + displacement);
        }
    } else {
        auto *scroll = view->verticalScrollBar();
        scroll->setValue(viewport->scrollPosition.y());
        const QModelIndex anchor = view->model()->index(row, 0);
        // Keep the original offset even when the icon is partly scrolled out.
        const int displacement = view->visualRect(anchor).top() - viewport->topOffset;
        scroll->setValue(scroll->value() + displacement);
    }
    view->horizontalScrollBar()->setValue(viewport->scrollPosition.x());
}
