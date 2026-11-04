// Qt execution and publication continuation for native CApp::OnCopy.
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtapppresentation.h"
#include <QScopedValueRollback>

void QtAppPresentation::ExecuteCopy(bool move, bool copyToSame, unsigned sourceIndex,
                                    const QString &destination,
                                    const CRecordVector<UInt32> &indices, QtCopyRoute route) {
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
        window_.executeCopyDestination(move, copyToSame, sourceIndex, destination, indices, route);
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
