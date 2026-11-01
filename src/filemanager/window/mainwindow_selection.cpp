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

void MainWindow::handleSelectionClick(int row) {
    if (!selectionClickLeft_ || row < 0 || !app_.GetFocusedPanel()._mySelectMode)
        return;
    app_.GetFocusedPanel().OnLeftClick(row, selectionClickModifiers_);
    refreshActions();
}

void MainWindow::SelectSpec(bool selectMode) {
    if (projectedUi().state != State::Filesystem && projectedUi().state != State::Viewing &&
        projectedUi().state != State::Namespace)
        return;
    CPanel &panel = app_.GetFocusedPanel();
    panel.SelectSpec(selectMode);
    refreshActions();
}

void MainWindow::SelectByType(bool selectMode) {
    if (projectedUi().state != State::Filesystem && projectedUi().state != State::Viewing &&
        projectedUi().state != State::Namespace)
        return;
    CPanel &panel = app_.GetFocusedPanel();
    panel.SelectByType(selectMode);
    refreshActions();
}

void MainWindow::SelectAll(bool selectMode) {
    if (projectedUi().state != State::Filesystem && projectedUi().state != State::Viewing &&
        projectedUi().state != State::Namespace)
        return;
    CPanel &panel = app_.GetFocusedPanel();
    panel.SelectAll(selectMode);
    refreshActions();
}

void MainWindow::InvertSelection() {
    if (projectedUi().state != State::Filesystem && projectedUi().state != State::Viewing &&
        projectedUi().state != State::Namespace)
        return;
    CPanel &panel = app_.GetFocusedPanel();
    panel.InvertSelection();
    refreshActions();
}
