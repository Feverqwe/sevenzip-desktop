#include "filemanager/window/mainwindow.h"
#include <utility>

void MainWindow::completeListSelection() {
    const unsigned owner = projectedPanel_;
    const auto selection = std::exchange(panelUi_[owner].pendingSelection, std::nullopt);
    if (selection)
        app_.Panels[owner].RestoreLoadedSelection(*selection);
}

void MainWindow::restoreListSelection(const CSelectedState &selection) {
    if (projectedUi().scanningDisk || projectedUi().scanningArchive)
        panelUi_[projectedPanel_].pendingSelection = selection;
    else
        app_.Panels[projectedPanel_].RestoreLoadedSelection(selection);
}

void MainWindow::SelectAll(bool selectMode) {
    if (projectedUi().state != State::Filesystem && projectedUi().state != State::Viewing &&
        projectedUi().state != State::Namespace)
        return;
    if (selectMode)
        app_.GetFocusedPanel().SelectAll(true);
    else
        projectedUi().table->clearSelection();
    refreshActions();
}
