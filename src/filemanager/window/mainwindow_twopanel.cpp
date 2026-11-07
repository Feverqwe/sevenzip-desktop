#include "7zip/UI/FileManager/resource.h"
#include "filemanager/qtfilemanagercommands.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtapppresentation.h"
#include "filemanager/window/qtwindowworkspace.h"
#include <QScopeGuard>
#include <QScopedValueRollback>

// Qt keeps each panel's controls and presentation state while CPanel owns the
// bound folder, selection and source navigation state. The projected index
// selects those existing bindings for commands and temporary callbacks.

void MainWindow::activatePanelUi(unsigned index) {
    if (index >= kNumPanelsMax || !panelUi_[index].container)
        return;
    if (index == projectedPanel_) {
        if (statusPanel_ != index) {
            statusPanel_ = index;
            refreshActions();
        }
        return;
    }
    if (app_.QtOperations().blocking()) {
        app_.SetFocusedPanel(projectedPanel_);
        return;
    }
    {
        // Enabling Qt views can synchronously focus the previously active pane.
        // Keep source callbacks and the Qt projection on this captured target.
        CPanel::CDisableNotify sourceNotify(app_.Panels[projectedPanel_]);
        CPanel::CDisableNotify targetNotify(app_.Panels[index]);
        QScopedValueRollback<unsigned> projection(panelProjectionDepth_, panelProjectionDepth_ + 1);
        projectedPanel_ = index;
        statusPanel_ = index;
        app_.SetFocusedPanel(index);
        setState(projectedUi().state);
    }
    refreshActions();
    app_.RefreshTitle(true);
}

unsigned MainWindow::scanningPanelMask() const {
    unsigned mask = 0;
    for (unsigned index = 0; index < kNumPanelsMax; ++index) {
        if (panelUi_[index].scanningDisk || panelUi_[index].scanningArchive)
            mask |= 1u << index;
    }
    return mask;
}

void MainWindow::withPanelProjection(unsigned index, const std::function<void()> &body) {
    if (app_.QtOperations().closed() || index >= kNumPanelsMax || !panelUi_[index].container)
        return;
    if (index == projectedPanel_) {
        body();
        return;
    }
    const unsigned original = projectedPanel_;
    const QPointer<MainWindow> guard(this);
    projectedPanel_ = index;
    app_.SetFocusedPanel(index);
    ++panelProjectionDepth_;
    const auto restore = qScopeGuard([&] {
        if (!guard)
            return;
        projectedPanel_ = original;
        app_.SetFocusedPanel(original);
        --panelProjectionDepth_;
        if (!app_.QtOperations().closed()) {
            refreshActions();
        }
    });
    body();
}

void MainWindow::togglePanels() {
    if (!commands_ || !commands_->CanExecuteCommand(IDM_VIEW_TWO_PANELS)) {
        twoPanelsAction_->setChecked(app_.NumPanels == 2);
        return;
    }
    const QPointer<QWidget> focused = focusWidget();
    const bool editingAddress =
        focused && (focused == projectedUi().path || focused == projectedUi().pathCombo);
    if (app_.NumPanels == 2 && panelSplitter_)
        panelSplitter_->RememberPosition();
    if (app_.NumPanels == 1 && !workspace_->PreparePanelForShow(1 - app_.LastFocusedPanel))
        return;
    if (app_.SwitchOnOffOnePanel() != S_OK) {
        twoPanelsAction_->setChecked(app_.NumPanels == 2);
        return;
    }
    twoPanelsAction_->setChecked(app_.NumPanels == 2);
    statusPanel_ = projectedPanel_;
    refreshActions();
    if (panelSplitter_ && app_.NumPanels == 2) {
        panelSplitter_->UpdateSourceLayout();
    }
    if (editingAddress && focused && focused->isVisible() && focused->isEnabled())
        focused->setFocus(Qt::OtherFocusReason);
    else
        app_.GetFocusedPanel().SetFocusToList();
}

QString MainWindow::copyDefaultDestination() const {
    if (app_.NumPanels == 2) {
        const QString other = ToQString(app_.Panels[1 - app_.GetFocusedPanelIndex()].GetFsPath());
        if (!other.isEmpty())
            return other;
    }
    return projectedUi().state == State::Viewing ? projectedUi().returnDirectory
                                                 : projectedUi().directory;
}
