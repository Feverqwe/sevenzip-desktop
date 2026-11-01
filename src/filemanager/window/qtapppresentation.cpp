#include "filemanager/window/qtapppresentation.h"
#include "filemanager/qtfilemanagerdrag.h"
#include "filemanager/views/qtfilelisthover.h"
#include "filemanager/window/mainwindow.h"
#include "fileops/native/Dialog.h"
#include "ui/dialogs/qtdialogscreenservices.h"
#include <QPointer>
#include <QScopedValueRollback>

bool QtAppPresentation::IsSmallScreen() const {
    const QtDialogScreenServices screen(&window_);
    return !NWindows::NControl::IsDialogSizeOK(200, 200, screen);
}

void QtAppPresentation::Initialize(CApp &app) {
    // Preserve the existing single-panel preferences when introducing the
    // original per-panel keys. Explicit source keys always take precedence.
    QSettings key(window_.toolbarSettingsPath(), QSettings::IniFormat);
    if (!key.contains("Toolbars") &&
        (key.contains("toolbar/text") || key.contains("toolbar/large") ||
         key.contains("toolbar/standard") || key.contains("toolbar/archive"))) {
        UInt32 mask = 0;
        if (key.value("toolbar/text", true).toBool())
            mask |= 1;
        if (key.value("toolbar/large", false).toBool())
            mask |= 2;
        if (key.value("toolbar/standard", true).toBool())
            mask |= 4;
        if (key.value("toolbar/archive", true).toBool())
            mask |= 8;
        SaveToolbarsMask(mask, window_.toolbarSettingsPath());
    }
    if (!key.contains("ListMode")) {
        CListMode modes;
        bool ok = false;
        const int mode = key.value("view/mode", 3).toInt(&ok);
        if (ok && mode >= 0 && mode < 4)
            modes.Panels[app.LastFocusedPanel < kNumPanelsMax ? app.LastFocusedPanel : 0] = mode;
        modes.Save(window_.toolbarSettingsPath());
    }
    const QString flatKey =
        QStringLiteral("FM/FlatViewArc%1")
            .arg(app.LastFocusedPanel < kNumPanelsMax ? app.LastFocusedPanel : 0);
    if (!key.contains(flatKey)) {
        key.setValue(flatKey, key.value("view/flatArchive", false));
        key.sync();
    }
    editMenu_ = window_.createMenusAndToolbar();
}

bool QtAppPresentation::CreatePanel(CPanel &panel, unsigned index) {
    if (index >= kNumPanelsMax || window_.panelUi_[index].container)
        return false;
    const unsigned previous = window_.projectedPanel_;
    const bool hadPrevious = window_.panelUi_[previous].container;
    window_.projectedPanel_ = index;
    window_.app_.SetFocusedPanel(index);
    panel.LoadFullPath();
    const QSettings settings(window_.toolbarSettingsPath(), QSettings::IniFormat);
    panel.SetFlatModeForDisk(settings.value("view/flatDisk", false).toBool());
    panel.AttachViewModePresentation([window = QPointer<MainWindow>(&window_), index](UInt32 mode) {
        if (window)
            window->withPanelProjection(index, [window, mode] { window->setViewMode(int(mode)); });
    });
    panel.AttachFlatModePresentation([window = QPointer<MainWindow>(&window_), index](bool flat) {
        if (window)
            window->withPanelProjection(index, [window, flat] { window->applyFlatView(flat); });
    });
    window_.createFilePanel(editMenu_, index);
    panel.AttachStatusPresentation(
        [window = QPointer<MainWindow>(&window_), index](const CPanelStatus &status) {
            if (window)
                window->showPanelStatus(index, status);
        });
    // OnCreate refreshes immediately after this service returns, before the
    // next panel is bound. Install its load/publication owner with its controls.
    panel.AttachReloadPresentation(
        [window = QPointer<MainWindow>(&window_), index](const CSelectedState &selection) {
            return window ? window->reloadPanelPresentation(index, selection) : E_ABORT;
        });
    if (window_.drag_)
        window_.drag_->AttachPanel(index);
    if (hadPrevious && index != previous) {
        window_.projectedPanel_ = previous;
        window_.app_.SetFocusedPanel(previous);
    }
    return true;
}

void QtAppPresentation::RequestInitialFocus(unsigned index) {
    // Source Create has already focused synchronous lists. Defer only when
    // asynchronous startup publication kept its target disabled.
    if (index < kNumPanelsMax && (window_.scanningPanelMask() & (1u << index)))
        RequestListFocus(index);
}

void QtAppPresentation::RequestListFocus(unsigned index) {
    if (index >= kNumPanelsMax || window_.app_.QtOperations().closed())
        return;
    initialFocus_ = index;
    CompleteInitialFocusIfReady();
}

void QtAppPresentation::CompleteInitialFocusIfReady() {
    if (!initialFocus_ || !window_.isVisible() || window_.panelProjectionDepth_ ||
        window_.app_.QtOperations().closed() || window_.app_.QtOperations().active())
        return;
    const unsigned index = *initialFocus_;
    if (window_.scanningPanelMask() & (1u << index))
        return;
    initialFocus_.reset();
    window_.activatePanelUi(index);
    window_.app_.SetFocusedPanel(index);
    window_.app_.Panels[index].SetFocusToList();
    window_.app_.RefreshTitle(true);
}

CPanelCallback *QtAppPresentation::PanelCallback(unsigned) {
    // Source callbacks, including folder synchronization, belong to
    // CApp::m_PanelCallbackImp. This service supplies Qt presentation only.
    return nullptr;
}

void QtAppPresentation::RefreshFolder(unsigned index) {
    window_.withPanelProjection(index, [this] { window_.renderBoundFolder(); });
}

void QtAppPresentation::PrepareFolderChange(unsigned index) {
    window_.withPanelProjection(index, [this] {
        window_.cancelListPublication();
        window_.setState(window_.projectedUi().state);
    });
}

void QtAppPresentation::ExecuteCopy(bool move, bool copyToSame, unsigned sourceIndex,
                                    const QString &destination,
                                    const CRecordVector<UInt32> &indices, bool useDestPanel,
                                    bool useTemp) {
    if (window_.app_.QtOperations().closed() || sourceIndex >= kNumPanelsMax)
        return;
    // OnCopy retains srcPanel across its modal dialog. Native Qt focus
    // restoration can activate the opposite pane; restore the Qt projection
    // for that captured source before executing the original request.
    window_.activatePanelUi(sourceIndex);
    if (!window_.app_.QtOperations().Begin(QtAppOperationState::Kind::Operation))
        return;
    copy_.emplace(sourceIndex, move, copyToSame);
    {
        QScopedValueRollback<bool> executing(copy_->executing, true);
        window_.executeCopyDestination(move, copyToSame, sourceIndex, destination, indices,
                                       useDestPanel, useTemp);
    }
    if (window_.projectedUi().state != MainWindow::State::Operating &&
        window_.app_.QtOperations().kind() == QtAppOperationState::Kind::Operation) {
        window_.app_.QtOperations().End();
        window_.refreshActions();
    }
    if (window_.projectedUi().state != MainWindow::State::Operating)
        CopyFinished();
}

void QtAppPresentation::CopyFinished() {
    if (!copy_)
        return;
    copy_->finished = true;
    CompleteCopyIfReady();
}

void QtAppPresentation::CompleteCopyIfReady() {
    if (!copy_ || copy_->executing || !copy_->finished || window_.app_.QtOperations().active())
        return;
    if (copy_->refreshSource && !window_.app_.QtOperations().closed()) {
        QScopedValueRollback<bool> publishing(copy_->executing, true);
        RefreshCopyPanel(copy_->source);
    }
    if (window_.app_.QtOperations().active())
        return;
    if (copy_->refreshDestination && !window_.app_.QtOperations().closed()) {
        copy_->refreshDestination = false;
        // Original OnCopy refreshes its destination panel even when the typed
        // output is elsewhere, ignoring the RefreshListCtrl result.
        QScopedValueRollback<bool> publishing(copy_->executing, true);
        const unsigned destination =
            window_.app_.NumPanels <= 1 ? copy_->source : 1 - copy_->source;
        RefreshCopyPanel(destination);
    }
    if (window_.app_.QtOperations().active())
        return;
    const unsigned source = copy_->source;
    // Panel activation and source completion can re-enter action refresh.
    // Release this request first so it cannot be completed a second time.
    copy_.reset();
    if (!window_.app_.QtOperations().closed())
        window_.activatePanelUi(source);
    window_.app_.CompleteCopy();
    window_.refreshActions();
}

void QtAppPresentation::CopyDestinationRefreshed(unsigned index) {
    if (!copy_)
        return;
    const unsigned destination = window_.app_.NumPanels <= 1 ? copy_->source : 1 - copy_->source;
    if (index == destination)
        copy_->refreshDestination = false;
}

void QtAppPresentation::CopySourceRefreshed(unsigned index) {
    if (copy_ && index == copy_->source)
        copy_->refreshSource = false;
}

void QtAppPresentation::RefreshCopyPanel(unsigned index) {
    CopySourceRefreshed(index);
    CopyDestinationRefreshed(index);
    if (!window_.app_.RefreshCopyPanel(index))
        window_.app_.Panels[index].OnReload();
}

void QtAppPresentation::SetListSettings(unsigned index, const CFmSettings &settings) {
    if (index >= kNumPanelsMax)
        return;
    window_.fileListSettings_ = settings;
    const auto &ui = window_.panelUi_[index];
    if (ui.table) {
        ui.table->setShowGrid(settings.ShowGrid);
        static_cast<FileTable *>(ui.table)->setFullRowHighlight(settings.FullRow);
    }
    if (ui.tableHover)
        ui.tableHover->SetEnabled(settings.SingleClick);
    if (ui.iconHover)
        ui.iconHover->SetEnabled(settings.SingleClick);
    if (ui.iconList)
        ui.iconList->setSelectionMode(settings.AlternativeSelection
                                          ? QAbstractItemView::SingleSelection
                                          : QAbstractItemView::ExtendedSelection);
}

void QtAppPresentation::ReloadToolbars(CApp &) { window_.updateToolbar(); }
void QtAppPresentation::ShowPanel(unsigned index, bool visible) {
    if (index < kNumPanelsMax && window_.panelUi_[index].container)
        window_.panelUi_[index].container->setVisible(visible);
}
void QtAppPresentation::MoveSubWindows() {
    if (window_.layout())
        window_.layout()->activate();
    if (window_.panelSplitter_)
        window_.panelSplitter_->UpdateSourceLayout();
}
