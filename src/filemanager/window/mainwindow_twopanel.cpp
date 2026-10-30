#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtapppresentation.h"
#include <QScopeGuard>
#include <QScopedValueRollback>

// Qt keeps each panel's controls and presentation state while CPanel owns the
// bound folder, selection and source navigation state. The active fields in
// MainWindow are a projection used by existing command consumers.
void MainWindow::storePanelUi(unsigned index) {
    PanelUiState &ui = panelUi_[index];
    ui.path = path_;
    ui.pathCombo = pathCombo_;
    ui.table = table_;
    ui.iconList = iconList_;
    ui.viewStack = viewStack_;
    ui.tableHover = tableHover_;
    ui.iconHover = iconHover_;
    ui.directory = directory_;
    ui.returnDirectory = returnDirectory_;
    ui.archive = archive_;
    ui.displayArchive = displayArchive_;
    ui.archivePassword = archivePassword_;
    ui.archiveFolder = archiveFolder_;
    ui.history = history_;
    ui.state = state_;
    ui.scanningDisk = scanningDisk_;
    ui.scanningArchive = scanningArchive_;
    ui.filesystemScanner = filesystemScanner_;
    ui.listProjection = listProjection_;
    ui.pendingSelection = scanSelectedState_;
    ui.pendingSelectedPaths = scanSelectedPaths_;
    ui.autoRefreshSnapshot = autoRefreshSnapshot_;
    ui.autoRefreshTopPath = autoRefreshTopPath_;
    ui.autoRefreshScrollValue = autoRefreshScrollValue_;
}

void MainWindow::restorePanelUi(unsigned index) {
    const PanelUiState &ui = panelUi_[index];
    path_ = ui.path;
    pathCombo_ = ui.pathCombo;
    table_ = ui.table;
    iconList_ = ui.iconList;
    viewStack_ = ui.viewStack;
    tableHover_ = ui.tableHover;
    iconHover_ = ui.iconHover;
    directory_ = ui.directory;
    returnDirectory_ = ui.returnDirectory;
    archive_ = ui.archive;
    displayArchive_ = ui.displayArchive;
    archivePassword_ = ui.archivePassword;
    archiveFolder_ = ui.archiveFolder;
    history_ = ui.history;
    state_ = ui.state;
    scanningDisk_ = ui.scanningDisk;
    scanningArchive_ = ui.scanningArchive;
    filesystemScanner_ = ui.filesystemScanner;
    listProjection_ = ui.listProjection;
    scanSelectedState_ = ui.pendingSelection;
    scanSelectedPaths_ = ui.pendingSelectedPaths;
    autoRefreshSnapshot_ = ui.autoRefreshSnapshot;
    autoRefreshTopPath_ = ui.autoRefreshTopPath;
    autoRefreshScrollValue_ = ui.autoRefreshScrollValue;
}

void MainWindow::activatePanelUi(unsigned index) {
    if (index == projectedPanel_ || index >= kNumPanelsMax || !panelUi_[index].container)
        return;
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
        storePanelUi(projectedPanel_);
        restorePanelUi(index);
        projectedPanel_ = index;
        app_.SetFocusedPanel(index);
        setState(state_);
    }
    refreshActions();
    app_.RefreshTitle(true);
}

unsigned MainWindow::scanningPanelMask() const {
    unsigned mask = 0;
    for (unsigned index = 0; index < kNumPanelsMax; ++index) {
        if (index == projectedPanel_
                ? scanningDisk_ || scanningArchive_
                : panelUi_[index].scanningDisk || panelUi_[index].scanningArchive)
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
    const QString status = statusBar()->currentMessage();
    storePanelUi(original);
    restorePanelUi(index);
    projectedPanel_ = index;
    app_.SetFocusedPanel(index);
    ++panelProjectionDepth_;
    const auto restore = qScopeGuard([&] {
        if (!guard)
            return;
        storePanelUi(index);
        restorePanelUi(original);
        projectedPanel_ = original;
        app_.SetFocusedPanel(original);
        --panelProjectionDepth_;
        if (!app_.QtOperations().closed()) {
            refreshActions();
            statusBar()->showMessage(status);
        }
    });
    body();
}

void MainWindow::togglePanels() {
    if (app_.QtOperations().closed() || app_.QtOperations().active() ||
        (filesystemScanner_ && filesystemScanner_->isRunning())) {
        twoPanelsAction_->setChecked(app_.NumPanels == 2);
        return;
    }
    const QPointer<QWidget> focused = focusWidget();
    const bool editingAddress = focused && (focused == path_ || focused == pathCombo_);
    if (app_.NumPanels == 2 && panelSplitter_)
        panelSplitter_->RememberPosition();
    if (app_.SwitchOnOffOnePanel() != S_OK) {
        twoPanelsAction_->setChecked(app_.NumPanels == 2);
        return;
    }
    twoPanelsAction_->setChecked(app_.NumPanels == 2);
    if (panelSplitter_ && app_.NumPanels == 2) {
        panelSplitter_->UpdateSourceLayout();
    }
    if (editingAddress && focused && focused->isVisible() && focused->isEnabled())
        focused->setFocus(Qt::OtherFocusReason);
    else
        app_.GetFocusedPanel().SetFocusToList();
}

unsigned MainWindow::panelIndexFor(QObject *object) const {
    for (unsigned index = 0; index < kNumPanelsMax; ++index) {
        const PanelUiState &ui = panelUi_[index];
        if (!ui.container)
            continue;
        if (object == ui.path || object == ui.pathCombo || object == ui.table ||
            object == ui.iconList || (ui.table && object == ui.table->viewport()) ||
            (ui.iconList && object == ui.iconList->viewport()))
            return index;
    }
    return kNumPanelsMax;
}

QString MainWindow::copyDefaultDestination() const {
    if (app_.NumPanels == 2) {
        const QString other = ToQString(app_.Panels[1 - app_.GetFocusedPanelIndex()].GetFsPath());
        if (!other.isEmpty())
            return other;
    }
    return state_ == State::Viewing ? returnDirectory_ : directory_;
}

void MainWindow::rememberTransferDestination(const QString &path) {
    transferDestinationPanel_.reset();
    if (app_.NumPanels != 2)
        return;
    const unsigned other = 1 - app_.GetFocusedPanelIndex();
    if (panelUi_[other].state != State::Filesystem)
        return;
    const QString destination = QFileInfo(path).canonicalFilePath();
    const QString panelPath = QFileInfo(panelUi_[other].directory).canonicalFilePath();
    if (!destination.isEmpty() && destination == panelPath)
        transferDestinationPanel_ = other;
}

void MainWindow::refreshTransferDestination(unsigned index) {
    if (app_.NumPanels != 2 || index >= kNumPanelsMax || app_.GetFocusedPanelIndex() == index ||
        app_.QtOperations().blocking())
        return;
    const unsigned source = app_.GetFocusedPanelIndex();
    activatePanelUi(index);
    appPresentation_->RefreshCopyPanel(index);
    activatePanelUi(source);
}
