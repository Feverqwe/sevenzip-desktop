#include "filemanager/panels/qtpaneladdresscombo.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtnavigationbar.h"
#include "filemanager/window/qtwindowworkspace.h"
#include <QTimer>

void QtWindowWorkspace::RequestInitialFocus(unsigned index) {
    // Source Create has already focused synchronous lists. Defer only when
    // asynchronous startup publication kept its target disabled.
    if (index < kNumPanelsMax && (window_.scanningPanelMask() & (1u << index)))
        RequestListFocus(index);
}

void QtWindowWorkspace::RequestListFocus(unsigned index) {
    if (index >= kNumPanelsMax || window_.app_.QtOperations().closed())
        return;
    CancelListFocus();
    pendingListFocus_ = index;
    CompleteListFocusIfReady();
}

void QtWindowWorkspace::CompleteListFocusIfReady() {
    if (!pendingListFocus_ || !window_.isVisible() || window_.panelProjectionDepth_ ||
        window_.app_.QtOperations().closed() || window_.app_.QtOperations().blocking())
        return;
    const unsigned index = *pendingListFocus_;
    if (window_.scanningPanelMask() & (1u << index))
        return;
    CancelListFocus();
    if (!window_.app_.IsPanelVisible(index) || !window_.panelUi_[index].container)
        return;
    window_.activatePanelUi(index);
    window_.app_.SetFocusedPanel(index);
    if (window_.panelUi_[index].locationUnavailable) {
        if (auto *retry = panels_[index].retry; retry && retry->isVisible())
            retry->setFocus(Qt::OtherFocusReason);
        return;
    }
    window_.app_.Panels[index].SetFocusToList();
    // Address FocusOut can occur while modal progress disables the pane.
    // Finish its presentation only after the owning list-focus request is delivered.
    if (window_.panelUi_[index].navigation)
        window_.panelUi_[index].navigation->FinishEditing();
    window_.app_.RefreshTitle(true);
}

void QtWindowWorkspace::CancelListFocus() {
    pendingListFocus_.reset();
    ++focusRequest_;
}

void QtWindowWorkspace::FocusListAfterLocationOpen(unsigned panel, quint64 interaction) {
    // The scan completion can still be inside a temporary panel projection.
    QTimer::singleShot(0, this, [this, panel, interaction] {
        if (window_.projectedPanel_ == panel && interactionRevision_ == interaction)
            RequestListFocus(panel);
    });
}

bool QtWindowWorkspace::eventFilter(QObject *object, QEvent *event) {
    if (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress ||
        event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick ||
        event->type() == QEvent::Wheel) {
        const auto *widget = qobject_cast<QWidget *>(object);
        const auto *modal = QApplication::activeModalWidget();
        // Answering a modal prompt continues the pending panel action.
        if (widget && (widget->window() == &window_ || window_.isAncestorOf(widget)) &&
            (!modal || (widget != modal && !modal->isAncestorOf(widget)))) {
            CancelListFocus();
            ++interactionRevision_;
        }
    }
    return QObject::eventFilter(object, event);
}

void QtWindowWorkspace::RetainListFocus(unsigned panel) {
    const QPointer<QWidget> focused = QApplication::focusWidget();
    const auto request = focusRequest_;
    auto *container = window_.panelUi_[panel].container;
    if (!focused || !container || !container->isAncestorOf(focused))
        return;
    container->setFocus(Qt::OtherFocusReason);
    WhenReady([this, panel, request, focused] { RestoreListFocus(panel, request, focused); });
}

void QtWindowWorkspace::RestoreAddressFocus(unsigned panel, const QtAddressDraft &draft) {
    CancelListFocus();
    const auto request = focusRequest_;
    WhenReady([this, panel, draft, request] {
        auto &ui = window_.panelUi_[panel];
        if (focusRequest_ != request || window_.projectedPanel_ != panel ||
            ui.path->text() != draft.text)
            return;
        ui.navigation->ResumeEditing();
        ui.path->setFocus(Qt::OtherFocusReason);
        draft.Restore(*ui.path);
    });
}

void QtWindowWorkspace::RestoreListFocus(unsigned panel, quint64 request,
                                         QPointer<QWidget> widget) {
    if (focusRequest_ != request || !widget || !widget->isVisible() ||
        window_.app_.GetFocusedPanelIndex() != panel)
        return;
    if (!Ready() || !widget->isEnabled()) {
        WhenReady([this, panel, request, widget] { RestoreListFocus(panel, request, widget); });
        return;
    }
    // QWidget also remembers this focus for an inactive window.
    widget->setFocus(Qt::OtherFocusReason);
}
