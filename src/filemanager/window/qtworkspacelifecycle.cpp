#include "filemanager/rename/qtrenamecontroller.h"
#include "filemanager/window/qtlocalfiletask.h"
#include "filemanager/window/qtwindowworkspace.h"
#include "filemanager/window/qtworkspaceruntime.h"
#include "mount/qtarchivemountmanager.h"
#include <QScopeGuard>
#include <QScopedValueRollback>

QtTabRuntime::~QtTabRuntime() {
    delete mounts.data();
    if (source) {
        QtLocalFileTask::StopForSource(*source);
        source->ReleasePanel();
    }
    ClearPresentation();
}

void QtTabRuntime::ClearPresentation() {
    if (ui.container) {
        if (ui.filesystemScanner)
            ui.filesystemScanner->cancelAndWait();
        if (ui.listProjection)
            ui.listProjection->cancel();
        if (ui.table) {
            ui.table->sourcePanel = nullptr;
            ui.table->textFolder = nullptr;
        }
        delete ui.container;
        delete ui.filesystemScanner;
        delete ui.listProjection;
        ui = {};
    }
}

void QtWindowWorkspace::Dispose() {
    navigation_ = {};
    for (unsigned slot = 0; slot < 2; ++slot) {
        auto &panel = panels_[slot];
        if (panel.bound && window_.panelUi_[slot].container) {
            panel.bound->ui = std::move(window_.panelUi_[slot]);
            window_.panelUi_[slot] = {};
        }
        panel.tabs.clear();
        panel.bound.reset();
    }
}

bool QtWindowWorkspace::CloseRuntime(unsigned panel, int index) {
    const auto runtime = panels_[panel].tabs[index].runtime;
    if (!runtime)
        return true;
    if (runtime->closing)
        return false;
    if (runtime->mounts) {
        const QPointer<CPanel> source(runtime->source.get());
        const auto id = panels_[panel].tabs[index].id;
        if (!runtime->mounts->canCloseOwner([source, id] {
                if (!source)
                    return;
                auto *window = dynamic_cast<MainWindow *>(source->GetParent());
                if (window)
                    if (const auto position = window->workspace_->FindTab(id))
                        window->workspace_->CloseTab(position->panel, position->index);
            }))
            return false;
    }
    return WithSource(*runtime->source, [this, runtime, panel, index] {
        const QScopedValueRollback<bool> changing(changing_, true);
        runtime->closing = true;
        QtLocalFileTask::StopForSource(*runtime->source);
        panels_[panel].tabs[index].resumePath = ToQString(runtime->source->GetResumeFolderPath());
        window_.renameController_->Cancel(window_.projectedPanel_);
        window_.cancelListPublication();
        window_.projectedUi().filesystemScanner->cancelAndWait();
        runtime->source->ExitExternalWorkers(false);
        runtime->source->ReleasePanel();
        return true;
    });
}

QtArchiveMountManager *QtWindowWorkspace::CurrentMountManager(bool create) {
    const auto runtime = panels_[window_.projectedPanel_].bound;
    if (!runtime)
        return nullptr;
    if (!runtime->mounts && create)
        runtime->mounts = new QtArchiveMountManager(&window_);
    return runtime->mounts.data();
}

bool QtWindowWorkspace::CanCloseMounts() {
    for (const auto &panel : panels_)
        for (const auto &tab : panel.tabs)
            if (tab.runtime && tab.runtime->mounts && !tab.runtime->mounts->canCloseOwner())
                return false;
    return true;
}

QWidget *QtWindowWorkspace::MountCloseBlocker() const {
    for (const auto &panel : panels_)
        for (const auto &tab : panel.tabs)
            if (tab.runtime && tab.runtime->mounts && tab.runtime->mounts->hasActiveSessions())
                return tab.runtime->mounts.data();
    return nullptr;
}

bool QtWindowWorkspace::SourceIsActive(const CPanel &source) const {
    for (const auto &panel : panels_)
        if (panel.bound && panel.bound->source.get() == &source)
            return true;
    return false;
}

bool QtWindowWorkspace::PreparePanelForShow(unsigned panel) {
    if (window_.app_.Panels[panel].PanelCreated || !panels_[panel].bar)
        return true;
    COpenResult opened;
    return window_.app_.CreateOnePanel(panel, ToUString(QDir::homePath()), UString(), false,
                                       opened) == S_OK;
}

void QtWindowWorkspace::MarkDirty(CPanel &source) {
    for (auto &panel : panels_)
        for (auto &tab : panel.tabs)
            if (tab.runtime && tab.runtime->source.get() == &source)
                tab.runtime->dirty = true;
}

bool QtWindowWorkspace::WithSource(CPanel &source, const std::function<bool()> &body) {
    for (unsigned slot = 0; slot < 2; ++slot) {
        if (&window_.app_.Panels[slot] == &source) {
            bool result = false;
            window_.withPanelProjection(slot, [&] { result = body(); });
            return result;
        }
        for (auto &tab : panels_[slot].tabs) {
            if (!tab.runtime || tab.runtime->source.get() != &source)
                continue;
            if (!Ready())
                return false;
            const auto oldSource = window_.app_.Panels.Share(slot);
            auto oldUi = std::move(window_.panelUi_[slot]);
            const QScopedValueRollback<bool> changing(changing_, true);
            const QScopedValueRollback<unsigned> projected(window_.projectedPanel_, slot);
            const QScopedValueRollback<unsigned> focused(window_.app_.LastFocusedPanel, slot);
            const QScopedValueRollback<unsigned> depth(window_.panelProjectionDepth_,
                                                       window_.panelProjectionDepth_ + 1);
            window_.app_.Panels.Bind(slot, tab.runtime->source);
            window_.panelUi_[slot] = std::move(tab.runtime->ui);
            const auto restore = qScopeGuard([&] {
                window_.app_.QtOperations().End();
                tab.runtime->ui = std::move(window_.panelUi_[slot]);
                window_.panelUi_[slot] = std::move(oldUi);
                window_.app_.Panels.Bind(slot, oldSource);
                QMetaObject::invokeMethod(
                    &window_,
                    [window = QPointer<MainWindow>(&window_)] {
                        if (window && !window->app_.QtOperations().closed())
                            window->refreshActions();
                    },
                    Qt::QueuedConnection);
            });
            return body();
        }
    }
    return false;
}

void QtWindowWorkspace::ExitExternalWorkers(bool hardExit) {
    for (auto &panel : panels_)
        for (auto &tab : panel.tabs)
            if (tab.runtime)
                tab.runtime->source->ExitExternalWorkers(hardExit);
}

bool QtWindowWorkspace::HasExternalWorkers() const {
    for (const auto &panel : panels_)
        for (const auto &tab : panel.tabs)
            if (tab.runtime && tab.runtime->source->HasExternalWorkers())
                return true;
    return false;
}

void QtWindowWorkspace::QuiesceInactive() {
    for (auto &panel : panels_)
        for (auto &tab : panel.tabs) {
            if (!tab.runtime || SourceIsActive(*tab.runtime->source))
                continue;
            auto &ui = tab.runtime->ui;
            if (ui.filesystemScanner)
                ui.filesystemScanner->cancelAndWait();
            if (ui.listProjection)
                ui.listProjection->cancel();
            tab.runtime->source->ReleasePanel();
        }
}
