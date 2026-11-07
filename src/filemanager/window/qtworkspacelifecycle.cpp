#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtlocalfiletask.h"
#include "filemanager/window/qtwindowworkspace.h"
#include "mount/qtarchivemountmanager.h"
#include <utility>

void QtWindowWorkspace::Dispose() {
    navigation_ = {};
    for (unsigned slot = 0; slot < panels_.size(); ++slot) {
        delete panels_[slot].mounts.data();
        auto ui = std::exchange(window_.panelUi_[slot], {});
        if (!ui.container)
            continue;
        auto &source = window_.app_.Panels[slot];
        QtLocalFileTask::StopForSource(source);
        source.ReleasePanel();
        delete ui.container;
        delete ui.filesystemScanner;
        delete ui.listProjection;
    }
}

QtArchiveMountManager *QtWindowWorkspace::CurrentMountManager(bool create) {
    auto &panel = panels_[window_.projectedPanel_];
    if (!panel.mounts && create)
        panel.mounts = new QtArchiveMountManager(&window_);
    return panel.mounts.data();
}

bool QtWindowWorkspace::CanCloseMounts() {
    for (const auto &panel : panels_)
        if (panel.mounts && !panel.mounts->canCloseOwner())
            return false;
    return true;
}

QWidget *QtWindowWorkspace::MountCloseBlocker() const {
    for (const auto &panel : panels_)
        if (panel.mounts && panel.mounts->hasActiveSessions())
            return panel.mounts.data();
    return nullptr;
}

bool QtWindowWorkspace::PreparePanelForShow(unsigned panel) {
    if (window_.app_.Panels[panel].PanelCreated || !window_.panelUi_[panel].table)
        return true;
    COpenResult opened;
    return window_.app_.CreateOnePanel(panel, ToUString(QDir::homePath()), UString(), false,
                                       opened) == S_OK;
}

void QtWindowWorkspace::ExitExternalWorkers(bool hardExit) {
    for (auto &source : window_.app_.Panels)
        source.ExitExternalWorkers(hardExit);
}
