#include "archive/resources/qtarchivefileresource.h"
#include "filemanager/qtfilemanagerdrag.h"
#include "filemanager/window/qtapppresentation.h"
#include "filemanager/window/qtwindowworkspace.h"
#include "filemanager/window/qtworkspaceapplication.h"
#include "filemanager/window/qtworkspaceruntime.h"
#include "platform/associations/qtassociatedfileservices.h"
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
                                    unsigned destination) {
    if (!Ready() || !target.Ready() || destination >= 2 || !target.panels_[destination].bar)
        return false;
    unsigned origin = 0;
    int index = -1;
    for (unsigned panel = 0; panel < 2; ++panel)
        for (int tab = 0; tab < int(panels_[panel].tabs.size()); ++tab)
            if (panels_[panel].tabs[tab].id == id) {
                origin = panel;
                index = tab;
            }
    if (index < 0 || (this == &target && origin == destination))
        return false;
    auto &from = panels_[origin];
    if (index == from.current)
        Capture(origin);
    if (!from.tabs[index].runtime) {
        Activate(origin, index);
        const QPointer<QtWindowWorkspace> receiver(&target);
        WhenReady([this, receiver, id, destination] {
            if (receiver)
                TransferTab(id, *receiver, destination);
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
        from.actions->setParent(&window_);
        from.actions->hide();
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
        target.window_.app_.Panels.Bind(destination, runtime->source);
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
    target.window_.projectedPanel_ = destination;
    target.window_.app_.SetFocusedPanel(destination);
    target.window_.renderBoundFolder();
    if (into.tabs[into.current].unavailable)
        target.CompleteLocationOpen(destination, into.tabs[into.current].path, E_FAIL);
    target.window_.restoreListSelection(into.tabs[into.current].selection);
    auto &ui = target.window_.panelUi_[destination];
    ui.history = into.tabs[into.current].history;
    auto *view = qobject_cast<QAbstractItemView *>(ui.viewStack->currentWidget());
    ui.pendingViewport =
        MainWindow::FilesystemViewport(view, ui.directory, {}, into.tabs[into.current].scroll);
    if (!ui.scanningDisk && !ui.scanningArchive)
        target.window_.restoreAutoRefreshViewport();
    target.window_.refreshActions();
    source.SetFocusToList();
    return true;
}

void QtWindowWorkspace::DetachTab(const QString &id) {
    auto *application = QtWorkspaceApplication::Find();
    if (!application || !Ready())
        return;
    const QPointer<QtWindowWorkspace> origin(this);
    application->NewWindow(QDir::homePath(), {}, [origin, id](MainWindow &window) {
        if (!origin)
            return;
        auto *target = window.workspace_.get();
        if (origin->TransferTab(id, *target, target->window_.projectedPanel_))
            target->CloseTab(target->window_.projectedPanel_, 0);
    });
}
