#include "archive/resources/qtarchivefileresource.h"
#include "filemanager/qtfilemanagerdrag.h"
#include "filemanager/window/qtapppresentation.h"
#include "filemanager/window/qtwindowworkspace.h"
#include "filemanager/window/qtworkspaceapplication.h"
#include "filemanager/window/qtworkspaceruntime.h"
#include "mount/qtarchivemountmanager.h"
#include "platform/associations/qtassociatedfileservices.h"
#include <QScopeGuard>
#include <QScopedValueRollback>
#include <QTabBar>

namespace {
void RebindArchiveResources(CPanel &panel, ArchiveBackend &backend) {
    QSet<QtArchiveFileResource *> rebound;
    const auto rebind = [&](IFolderFolder *folder) {
        const auto *agent = dynamic_cast<CAgentFolder *>(folder);
        auto *resource =
            agent ? dynamic_cast<QtArchiveFileResource *>(agent->GetEngineResource()) : nullptr;
        if (resource && !rebound.contains(resource)) {
            resource->RebindServices(backend);
            rebound.insert(resource);
        }
    };
    rebind(panel.GetFolder());
    const auto &parents = panel.GetParentFolders();
    FOR_VECTOR(i, parents)
    rebind(parents[i].ParentFolder);
}
} // namespace

bool QtWindowWorkspace::TransferTab(const QString &id, QtWindowWorkspace &target,
                                    unsigned destination, std::function<void(bool)> finished) {
    const auto rejected = qScopeGuard([&] {
        if (finished)
            finished(false);
    });
    if (!Ready() || !target.Ready() || destination >= 2 || !target.panels_[destination].bar)
        return false;
    const auto position = FindTab(id);
    if (!position || (this == &target && position->panel == destination))
        return false;
    const auto [origin, index] = *position;
    auto &from = panels_[origin];
    if (index == from.current)
        Capture(origin);
    if (!from.tabs[index].runtime) {
        const QString previous = from.tabs[from.current].id;
        Activate(origin, index);
        const auto request = focusRequest_;
        const QPointer<QtWindowWorkspace> receiver(&target);
        const auto complete = std::exchange(finished, {});
        WhenReady(
            [this, receiver, id, destination, previous, request, complete] {
                if (focusRequest_ == request)
                    if (const auto position = FindTab(previous))
                        Activate(position->panel, position->index);
                if (receiver)
                    TransferTab(id, *receiver, destination, complete);
                else if (complete)
                    complete(false);
            },
            [complete] {
                if (complete)
                    complete(false);
            });
        return true;
    }
    auto runtime = from.tabs[index].runtime;
    if (this != &target && runtime->source->HasExternalWorkers()) {
        window_.statusBar()->showMessage(
            tr("Finish external editing before moving this tab to another window."));
        return false;
    }
    const bool active = from.bound == runtime;
    if (active) {
        window_.activatePanelUi(origin);
        Suspend(origin);
        from.bar->parentWidget()->setParent(&window_);
        from.bar->parentWidget()->hide();
        from.actionRow->setParent(&window_);
        from.actionRow->hide();
        from.sidebar = nullptr;
    }
    target.Capture(destination);
    target.window_.activatePanelUi(destination);
    target.Suspend(destination);
    auto &into = target.panels_[destination];
    Tab incoming = from.tabs[index];
    auto &source = *runtime->source;
    {
        const QScopedValueRollback<bool> originChange(changing_, true);
        const QScopedValueRollback<bool> destinationChange(target.changing_, true);
        const auto sourcePeer = window_.app_.Panels.Share(1 - origin);
        const auto targetPeer = target.window_.app_.Panels.Share(1 - destination);
        const CPanel::CDisableNotify sourceNotifications(*sourcePeer);
        const CPanel::CDisableNotify targetNotifications(*targetPeer);
        const QScopedValueRollback<unsigned> sourceProjection(window_.panelProjectionDepth_,
                                                              window_.panelProjectionDepth_ + 1);
        const QScopedValueRollback<unsigned> targetProjection(
            target.window_.panelProjectionDepth_, target.window_.panelProjectionDepth_ + 1);
        target.window_.app_.Panels.Bind(destination, runtime->source);
        target.window_.app_.SetPanels_AutoRefresh_Mode();
        target.window_.projectedPanel_ = destination;
        target.window_.app_.SetFocusedPanel(destination);
        source.RebindWindowHost(&target.window_,
                                &target.window_.app_.m_PanelCallbackImp[destination],
                                &target.window_.app_.AppState);
        source.AttachFolderManager(target.window_.folderManager_,
                                   target.window_.toolbarSettingsPath());
        source.AttachExternalFileServices(target.window_.associatedFileServices_.get());
        RebindArchiveResources(source, *target.window_.backend_);
        target.window_.connectFilesystemScanner(destination);
        if (!target.window_.appPresentation_->CreatePanel(source, destination))
            return false;
        runtime->ClearPresentation();
        into.tabs.push_back(std::move(incoming));
        if (runtime->mounts)
            runtime->mounts->rebindOwner(target.window_);
        into.current = int(into.tabs.size()) - 1;
        into.bound = runtime;
        from.tabs.erase(from.tabs.begin() + index);
        if (active) {
            from.bound.reset();
            auto empty = std::make_shared<CPanel>();
            empty->AttachFolderManager(window_.folderManager_, window_.toolbarSettingsPath());
            empty->AttachExternalFileServices(window_.associatedFileServices_.get());
            window_.app_.Panels.Bind(origin, std::move(empty));
        }
        if (index < from.current || from.current >= int(from.tabs.size()))
            from.current = qMax(0, from.current - 1);
        Rebuild(origin);
        target.Rebuild(destination);
    }
    if (active && !from.tabs.empty())
        Activate(origin, from.current);
    else if (from.tabs.empty()) {
        if (window_.app_.NumPanels == 2) {
            window_.activatePanelUi(1 - origin);
            window_.togglePanels();
        } else if (!window_.close()) {
            // Existing mount/close protection can refuse closing the origin.
            // The moved source now belongs to the receiver; leave a usable
            // Home tab in the still-open window rather than an empty slot.
            NewTab(origin);
        }
    }
    {
        const auto peer = target.window_.app_.Panels.Share(1 - destination);
        const CPanel::CDisableNotify peerNotifications(*peer);
        const QScopedValueRollback<unsigned> projection(target.window_.panelProjectionDepth_,
                                                        target.window_.panelProjectionDepth_ + 1);
        target.window_.projectedPanel_ = destination;
        target.window_.app_.SetFocusedPanel(destination);
        target.window_.renderBoundFolder();
        if (into.tabs[into.current].unavailable)
            target.CompleteLocationOpen(destination, into.tabs[into.current].path, E_FAIL);
        target.window_.restoreListSelection(into.tabs[into.current].selection);
        auto &ui = target.window_.panelUi_[destination];
        auto *view = qobject_cast<QAbstractItemView *>(ui.viewStack->currentWidget());
        ui.pendingViewport =
            MainWindow::FilesystemViewport(view, ui.directory, {}, into.tabs[into.current].scroll);
        if (!ui.scanningDisk && !ui.scanningArchive)
            target.window_.restoreAutoRefreshViewport();
        target.window_.refreshActions();
        source.SetFocusToList();
    }
    target.RefreshPublishedArchive(destination);
    if (const auto complete = std::exchange(finished, {}))
        complete(true);
    return true;
}

void QtWindowWorkspace::DetachTab(const QString &id) {
    auto *application = QtWorkspaceApplication::Find();
    if (!application || !Ready() || !FindTab(id))
        return;
    const QPointer<QtWindowWorkspace> origin(this);
    application->NewWindow(QDir::homePath(), {}, [origin, id](MainWindow &window) {
        auto *target = window.workspace_.get();
        const unsigned panel = target->window_.projectedPanel_;
        const QString placeholder = target->panels_[panel].tabs[target->panels_[panel].current].id;
        const auto initialPanels = target->Save().value("panels");
        const auto initialCount = target->window_.app_.NumPanels;
        const auto request = target->focusRequest_;
        const QPointer<QtWindowWorkspace> receiver(target);
        const auto complete = [receiver, placeholder, initialPanels, initialCount,
                               request](bool moved) {
            if (!receiver)
                return;
            receiver->WhenReady(
                [receiver, placeholder, initialPanels, initialCount, request, moved] {
                    const auto position = receiver->FindTab(placeholder);
                    if (!position)
                        return;
                    receiver->Capture(position->panel);
                    const auto &state = receiver->panels_[position->panel];
                    if (receiver->focusRequest_ != request ||
                        QDir::cleanPath(state.tabs[position->index].path) != QDir::homePath())
                        return;
                    if (moved)
                        receiver->CloseTab(position->panel, position->index);
                    else if (receiver->window_.app_.NumPanels == initialCount &&
                             receiver->Save().value("panels") == initialPanels)
                        receiver->window_.close();
                });
        };
        if (origin)
            origin->TransferTab(id, *target, panel, complete);
        else
            complete(false);
    });
}
