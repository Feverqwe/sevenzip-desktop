#include "filemanager/panels/qtpaneladdresscombo.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtnavigationbar.h"
#include "filemanager/window/qtwindowworkspace.h"

bool QtWindowWorkspace::eventFilter(QObject *object, QEvent *event) {
    if (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress ||
        event->type() == QEvent::MouseButtonPress || event->type() == QEvent::Wheel) {
        const auto *widget = qobject_cast<QWidget *>(object);
        const auto *modal = QApplication::activeModalWidget();
        // Answering a modal prompt continues the pending panel action.
        if (widget && (widget->window() == &window_ || window_.isAncestorOf(widget)) &&
            (!modal || (widget != modal && !modal->isAncestorOf(widget)))) {
            ++focusRequest_;
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
    const auto request = ++focusRequest_;
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
