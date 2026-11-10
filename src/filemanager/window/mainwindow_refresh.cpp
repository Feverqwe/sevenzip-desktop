#include "archive/password/archivepassword.h"
#include "archive/resources/qtarchivereopen.h"
#include "filemanager/panels/qtpanellist.h"
#include "filemanager/providers/qtlocationfolder.h"
#include "filemanager/rename/qtrenamecontroller.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtwindowworkspace.h"
#include "localization/qtlanguageservices.h"

#include <QScopedValueRollback>
#include <QScrollBar>
#include <QThreadPool>
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
    const bool filesystem =
        !archive && !root && projectedUi().state == State::Filesystem && panel.GetFolder();
    const QString boundPath =
        filesystem ? QDir::cleanPath(ToQString(GetFolderPath(panel.GetFolder()))) : QString();
    const QString &displayedPath = projectedUi().directory;
    if (!filesystem ||
        (boundPath != displayedPath && QFileInfo(boundPath).canonicalFilePath() != displayedPath)) {
        const auto viewport =
            archive && selection.KeepViewport ? std::optional(captureListViewport()) : std::nullopt;
        const bool updateAddress = !selection.PreserveAddress;
        const HRESULT loaded =
            root ? renderRootFolder(updateAddress) : renderBoundFolder(updateAddress);
        if (loaded == S_OK) {
            restoreListSelection(selection);
            if (viewport) {
                if (projectedUi().scanningArchive)
                    projectedUi().pendingViewport = viewport;
                else
                    restoreListViewport(*viewport);
            }
        }
        return loaded;
    }
    if (!selection.KeepViewport)
        return refreshFilesystem(selection, true);
    return refreshFilesystem(selection, true, captureListViewport());
}

MainWindow::ListViewport MainWindow::captureListViewport() {
    auto *view = qobject_cast<QAbstractItemView *>(projectedUi().viewStack->currentWidget());
    if (!view)
        return {nullptr, {}, {}, {}, 0};
    int topRow = -1;
    int topOffset = 0;
    if (view == projectedUi().table) {
        topRow = projectedUi().table->rowAt(0);
        if (topRow >= 0)
            topOffset = projectedUi().table->rowViewportPosition(topRow);
    } else {
        const QRect visible = view->viewport()->rect();
        const int column = projectedUi().iconList->modelColumn();
        const auto *model = view->model();
        // The fixed icon grid is laid out left to right, then top to bottom.
        int first = 0;
        int last = model->rowCount();
        while (first < last) {
            const int middle = first + (last - first) / 2;
            if (view->visualRect(model->index(middle, column)).bottom() < visible.top())
                first = middle + 1;
            else
                last = middle;
        }
        for (int row = first; row < model->rowCount(); ++row) {
            const QRect item = view->visualRect(model->index(row, column));
            if (item.top() > visible.bottom())
                break;
            if (visible.intersects(item)) {
                topRow = row;
                topOffset = item.top();
                break;
            }
        }
    }
    QStringList visiblePaths;
    const int height = view->viewport()->height();
    const int column = view == projectedUi().table ? 0 : projectedUi().iconList->modelColumn();
    for (int row = topRow; row >= 0 && row < view->model()->rowCount(); ++row) {
        const int offset = view == projectedUi().table
                               ? projectedUi().table->rowViewportPosition(row)
                               : view->visualRect(view->model()->index(row, column)).top();
        if (offset >= height)
            break;
        const auto path =
            view->model()->index(row, 0).data(QtPanelList::OperationPathRole).toString();
        if (path.isEmpty())
            break;
        visiblePaths.append(path);
    }
    ListViewport viewport{
        view, projectedUi().directory, visiblePaths,
        QPoint(view->horizontalScrollBar()->value(), view->verticalScrollBar()->value()),
        topOffset};
    if (auto *table = qobject_cast<FileTable *>(view)) {
        viewport.cursorColumn = table->currentIndex().column();
        viewport.cursorProperty = table->propertyId(viewport.cursorColumn);
        viewport.cursorRawProperty = table->isRawProperty(viewport.cursorColumn);
    }
    return viewport;
}

HRESULT MainWindow::refreshFilesystem(const CSelectedState &selection, bool sourceAlreadyBound,
                                      std::optional<ListViewport> viewport) {
    const QString directory = projectedUi().directory;
    const auto signature = filesystemSignature(directory);
    if (!signature.directory) {
        app_.GetFocusedPanel().LoadListItems();
        auto &snapshot = projectedUi().autoRefreshSnapshot;
        projectedUi().autoRefreshProbe.reset();
        snapshot.clear();
        snapshot.insert(directory, signature);
        projectedUi().table->clearSelection();
        projectedUi().table->clearRows();
        showStatusMessage(
            signature.exists
                ? QCoreApplication::translate("CopyRedesign", "Not a folder: %1").arg(directory)
                : QCoreApplication::translate("mainwindow_refresh", "Folder not found: ") +
                      directory);
        refreshActions();
        // The missing-folder state has been published, including the diagnostic.
        return S_OK;
    }
    const HRESULT loaded = showDirectory(projectedUi().directory, false, sourceAlreadyBound,
                                         !selection.PreserveAddress, true);
    if (loaded != S_OK)
        return loaded;
    panelUi_[projectedPanel_].pendingViewport = std::move(viewport);
    restoreListSelection(selection);
    if (!projectedUi().scanningDisk) {
        completeListViewport();
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
            if (GetFolderPath(refreshed) == previousFolderPath)
                reloadProjectedPanel(selection);
            else if (renderBoundFolder() == S_OK)
                restoreListSelection(CSelectedState());
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

MainWindow::FilesystemSignature MainWindow::filesystemSignature(const QString &path) {
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
    for (unsigned offset = 0; offset < kNumPanelsMax; ++offset) {
        const unsigned index = (nextAutoRefreshPanel_ + offset) % kNumPanelsMax;
        checkPanelForFilesystemChanges(index);
        if (app_.QtOperations().active()) {
            nextAutoRefreshPanel_ = (index + 1) % kNumPanelsMax;
            break;
        }
    }
}

void MainWindow::checkPanelForFilesystemChanges(unsigned index) {
    auto &ui = panelUi_[index];
    auto &panel = app_.Panels[index];
    if (!app_.IsPanelVisible(index) || !ui.container || ui.state != State::Filesystem ||
        ui.locationUnavailable || renameController_->PendingFor(index) || !panel.AutoRefresh_Mode ||
        !panel.ShouldProcessNotifications())
        return;
    withPanelProjection(index, [&] {
        const auto &snapshot = ui.autoRefreshSnapshot;
        const auto root = snapshot.constFind(ui.directory);
        if (root != snapshot.cend() && filesystemSignature(root.key()) != root.value()) {
            ui.autoRefreshProbe.reset();
            panel.MarkPortableFolderChanged();
        } else if (snapshot.size() > 256) {
            using Probe = PanelUiState::RefreshProbe;
            auto &probe = ui.autoRefreshProbe;
            if (probe) {
                const Probe result = probe->load();
                if (result != Probe::Pending)
                    probe.reset();
                if (result == Probe::Changed)
                    panel.MarkPortableFolderChanged();
            }
            if (!probe) {
                probe = std::make_shared<std::atomic<Probe>>(Probe::Pending);
                QThreadPool::globalInstance()->start([snapshot, pending = std::weak_ptr(probe)] {
                    auto result = Probe::Unchanged;
                    for (auto it = snapshot.cbegin(); it != snapshot.cend(); ++it) {
                        if (pending.expired())
                            return;
                        if (filesystemSignature(it.key()) != it.value()) {
                            result = Probe::Changed;
                            break;
                        }
                    }
                    if (const auto active = pending.lock())
                        active->store(result);
                });
            }
        } else {
            for (auto it = snapshot.cbegin(); it != snapshot.cend(); ++it) {
                if (it != root && filesystemSignature(it.key()) != it.value()) {
                    panel.MarkPortableFolderChanged();
                    break;
                }
            }
        }
        panel.OnTimer();
    });
}

void MainWindow::completeListViewport() {
    const auto viewport = std::exchange(panelUi_[projectedPanel_].pendingViewport, std::nullopt);
    if (!viewport || viewport->directory != projectedUi().directory)
        return;
    restoreListViewport(*viewport);
}

void MainWindow::restoreListViewport(const ListViewport &viewport) {
    auto *view = viewport.view.data();
    if (!view || view != projectedUi().viewStack->currentWidget())
        return;
    if (auto *table = qobject_cast<FileTable *>(view)) {
        const int column = viewport.cursorColumn;
        const QModelIndex current = table->currentIndex();
        if (current.isValid() && column >= 0 && column < table->columnCount() &&
            !table->isColumnHidden(column) &&
            table->propertyId(column) == viewport.cursorProperty &&
            table->isRawProperty(column) == viewport.cursorRawProperty)
            table->selectionModel()->setCurrentIndex(current.siblingAtColumn(column),
                                                     QItemSelectionModel::NoUpdate);
    }
    view->doItemsLayout();
    if (!viewport.keepVertical) {
        view->horizontalScrollBar()->setValue(viewport.scrollPosition.x());
        return;
    }
    int row = -1;
    if (!viewport.visiblePaths.isEmpty()) {
        QHash<QString, qsizetype> rank;
        for (qsizetype i = viewport.visiblePaths.size(); i > 0; --i)
            rank.insert(viewport.visiblePaths[i - 1], i - 1);
        qsizetype best = viewport.visiblePaths.size();
        for (int i = 0; i < view->model()->rowCount(); ++i) {
            const auto path =
                view->model()->index(i, 0).data(QtPanelList::OperationPathRole).toString();
            const auto candidate = rank.constFind(path);
            if (candidate == rank.cend() || candidate.value() >= best)
                continue;
            row = i;
            best = candidate.value();
            if (best == 0)
                break;
        }
    }
    if (row < 0) {
        view->verticalScrollBar()->setValue(viewport.scrollPosition.y());
    } else if (view == projectedUi().table) {
        view->scrollTo(view->model()->index(row, 0), QAbstractItemView::PositionAtTop);
        if (view->verticalScrollMode() == QAbstractItemView::ScrollPerPixel) {
            auto *scroll = view->verticalScrollBar();
            const int displacement =
                projectedUi().table->rowViewportPosition(row) - viewport.topOffset;
            scroll->setValue(scroll->value() + displacement);
        }
    } else {
        auto *scroll = view->verticalScrollBar();
        scroll->setValue(viewport.scrollPosition.y());
        const QModelIndex anchor = view->model()->index(row, projectedUi().iconList->modelColumn());
        // Keep the original offset even when the icon is partly scrolled out.
        const int displacement = view->visualRect(anchor).top() - viewport.topOffset;
        scroll->setValue(scroll->value() + displacement);
    }
    view->horizontalScrollBar()->setValue(viewport.scrollPosition.x());
}
