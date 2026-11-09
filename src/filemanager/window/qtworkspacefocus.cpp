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

void QtWindowWorkspace::RequestListFocus(unsigned index, bool switchingPane) {
    if (index >= kNumPanelsMax || window_.app_.QtOperations().closed())
        return;
    CancelListFocus();
    pendingListFocus_ = index;
    switchingPane_ = switchingPane;
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
    switchingPane_ = false;
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
        const auto *key =
            event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress
                ? static_cast<QKeyEvent *>(event)
                : nullptr;
        // A modifier alone does not choose a new input target. Let Qt and
        // the panel track it, and arbitrate the actual shortcut separately.
        if (key && (key->key() == Qt::Key_Control || key->key() == Qt::Key_Shift))
            return QObject::eventFilter(object, event);
        const unsigned panel = window_.projectedPanel_;
        if (key && (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab) &&
            !(key->modifiers() & (Qt::AltModifier | Qt::ControlModifier | Qt::MetaModifier)) &&
            widget == window_.panelUi_[panel].container && window_.app_.NumPanels == 2 &&
            (window_.scanningPanelMask() & (1u << panel)) && !modal &&
            !window_.app_.QtOperations().blocking()) {
            if (event->type() == QEvent::KeyPress) {
                ++interactionRevision_;
                RequestListFocus(1 - panel, true);
            }
            event->accept();
            return true;
        }
        const bool waitingForPeer = switchingPane_ && pendingListFocus_ &&
                                    *pendingListFocus_ != window_.projectedPanel_ &&
                                    window_.panelInputTarget(object, window_.projectedPanel_) ==
                                        MainWindow::PanelInputTarget::List;
        if (waitingForPeer && key) {
            const auto *command = window_.panelShortcut(*key);
            if (command && command->shortcutContext() == Qt::WidgetShortcut) {
                event->accept();
                return true;
            }
        }
        // Plain list navigation does not change the pending target, including
        // after Tab leaves input in the old list until the peer is ready.
        if (pendingListFocus_ && key && widget &&
            (widget == window_.panelUi_[*pendingListFocus_].container || waitingForPeer)) {
            if (key->modifiers() == Qt::NoModifier &&
                (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down ||
                 key->key() == Qt::Key_Left || key->key() == Qt::Key_Right ||
                 key->key() == Qt::Key_PageUp || key->key() == Qt::Key_PageDown ||
                 key->key() == Qt::Key_Home || key->key() == Qt::Key_End)) {
                event->accept();
                return true;
            }
        }
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
    auto *focused = window_.focusWidget();
    auto *container = window_.panelUi_[panel].container;
    if (!focused || !container || !container->isAncestorOf(focused))
        return;
    container->setFocus(Qt::OtherFocusReason);
    if (!pendingListFocus_)
        RequestListFocus(panel);
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
