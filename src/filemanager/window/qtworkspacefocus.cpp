#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtwindowworkspace.h"
#include <QTabBar>

bool QtWindowWorkspace::eventFilter(QObject *object, QEvent *event) {
    if (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress ||
        event->type() == QEvent::MouseButtonPress || event->type() == QEvent::Wheel)
        if (const auto *widget = qobject_cast<QWidget *>(object);
            widget && (widget->window() == &window_ || window_.isAncestorOf(widget)))
            ++focusRequest_;
    return QObject::eventFilter(object, event);
}

void QtWindowWorkspace::RestoreTabFocus(unsigned panel, const QString &id, quint64 request) {
    auto &target = panels_[panel];
    if (focusRequest_ != request || !window_.isActiveWindow() ||
        window_.app_.GetFocusedPanelIndex() != panel || !target.bar || target.current < 0 ||
        target.current >= int(target.tabs.size()) || target.tabs[target.current].id != id)
        return;
    if (!Ready() || !target.bar->isVisible() || !target.bar->isEnabled()) {
        WhenReady([this, panel, id, request] { RestoreTabFocus(panel, id, request); });
        return;
    }
    target.bar->setFocus();
}

void QtWindowWorkspace::RetainListFocus(unsigned panel) {
    const auto &state = panels_[panel];
    if (!state.bar || state.tabs.empty())
        return;
    const QPointer<QWidget> focused = QApplication::focusWidget();
    const auto request = focusRequest_;
    const QString id = state.tabs[state.current].id;
    state.bar->setFocus(Qt::OtherFocusReason);
    WhenReady(
        [this, panel, id, request, focused] { RestoreListFocus(panel, id, request, focused); });
}

void QtWindowWorkspace::RestoreListFocus(unsigned panel, const QString &id, quint64 request,
                                         QPointer<QWidget> widget) {
    const auto &state = panels_[panel];
    if (focusRequest_ != request || !widget || !widget->isVisible() || !window_.isActiveWindow() ||
        window_.app_.GetFocusedPanelIndex() != panel || state.tabs.empty() ||
        state.tabs[state.current].id != id)
        return;
    if (!Ready() || !widget->isEnabled()) {
        WhenReady(
            [this, panel, id, request, widget] { RestoreListFocus(panel, id, request, widget); });
        return;
    }
    widget->setFocus(Qt::OtherFocusReason);
}
