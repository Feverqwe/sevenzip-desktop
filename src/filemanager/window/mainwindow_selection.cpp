#include "filemanager/window/mainwindow.h"

void MainWindow::completeListSelection() {
    if (scanSelectedState_)
        app_.GetFocusedPanel().RestoreLoadedSelection(*scanSelectedState_);
    scanSelectedState_.reset();
}

void MainWindow::restoreListSelection(const CSelectedState &selection) {
    if (scanningDisk_ || scanningArchive_)
        scanSelectedState_ = selection;
    else
        app_.GetFocusedPanel().RestoreLoadedSelection(selection);
}

void MainWindow::handleSelectionClick(int row) {
    if (!selectionClickLeft_ || row < 0 || !app_.GetFocusedPanel()._mySelectMode)
        return;
    app_.GetFocusedPanel().OnLeftClick(row, selectionClickModifiers_);
    refreshActions();
}

void MainWindow::SelectSpec(bool selectMode) {
    if (state_ != State::Filesystem && state_ != State::Viewing && state_ != State::Namespace)
        return;
    CPanel &panel = app_.GetFocusedPanel();
    panel.SelectSpec(selectMode);
    refreshActions();
}

void MainWindow::SelectByType(bool selectMode) {
    if (state_ != State::Filesystem && state_ != State::Viewing && state_ != State::Namespace)
        return;
    CPanel &panel = app_.GetFocusedPanel();
    panel.SelectByType(selectMode);
    refreshActions();
}

void MainWindow::SelectAll(bool selectMode) {
    if (state_ != State::Filesystem && state_ != State::Viewing && state_ != State::Namespace)
        return;
    CPanel &panel = app_.GetFocusedPanel();
    panel.SelectAll(selectMode);
    refreshActions();
}

void MainWindow::InvertSelection() {
    if (state_ != State::Filesystem && state_ != State::Viewing && state_ != State::Namespace)
        return;
    CPanel &panel = app_.GetFocusedPanel();
    panel.InvertSelection();
    refreshActions();
}
