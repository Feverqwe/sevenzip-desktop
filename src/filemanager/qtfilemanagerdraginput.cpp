#include "filemanager/qtfilemanagerdrag.h"
#include "filemanager/window/mainwindow.h"
#ifdef Q_OS_MACOS
#include "filemanager/panels/qtmacfilepromisedrag.h"
#endif
#include <QContextMenuEvent>
#include <QDropEvent>
#include <QMouseEvent>
#include <QScopeGuard>

bool QtFileManagerDrag::eventFilter(QObject *object, QEvent *event) {
    auto *widget = qobject_cast<QWidget *>(object);
    if (!widget)
        return false;
    switch (event->type()) {
    case QEvent::DragEnter:
    case QEvent::DragMove:
    case QEvent::DragLeave:
    case QEvent::Drop:
        return HandleDropEvent(*widget, *event);
    default:
        return HandleRightDrag(*widget, *event);
    }
}
CDataObject *QtFileManagerDrag::ReadData(const QMimeData &mime) {
    if (auto *internal = dynamic_cast<const QtPanelDragMime *>(&mime))
        return &internal->Data();
#ifdef Q_OS_MACOS
    if (auto *source = QtMacFilePromiseDragSource(mime))
        return static_cast<CDataObject *>(source);
#endif
    external_.NativeData.reset();
#ifdef _WIN32
    if (nativeConverter_)
        external_.NativeData = nativeConverter_->Read(mime);
    if (external_.NativeData)
        return &external_;
#endif
    external_.Names.Clear();
    for (const QUrl &url : mime.urls()) {
        if (!url.isLocalFile())
            return nullptr;
        external_.Names.Add(ToUString(url.toLocalFile()));
    }
    return external_.Names.IsEmpty() ? nullptr : &external_;
}
static DWORD DropKeys(const QDropEvent &drop) {
    DWORD keys = 0;
    if (drop.modifiers() & Qt::ControlModifier)
        keys |= MK_CONTROL;
    if (drop.modifiers() & Qt::ShiftModifier)
        keys |= MK_SHIFT;
    if (drop.modifiers() & Qt::AltModifier)
        keys |= MK_ALT;
    if (drop.buttons() & Qt::RightButton)
        keys |= MK_RBUTTON;
    if (drop.buttons() & Qt::MiddleButton)
        keys |= MK_MBUTTON;
    return keys;
}
bool QtFileManagerDrag::HandleRightDrag(QWidget &widget, QEvent &event) {
    const unsigned panel = window_.panelIndexFor(&widget);
    if (revoked_ || panel >= kNumPanelsMax)
        return false;
    const auto &ui = window_.panelUi_[panel];
    auto *view = qobject_cast<QAbstractItemView *>(ui.viewStack->currentWidget());
    if (!view || &widget != view->viewport())
        return false;
    if (event.type() == QEvent::ContextMenu) {
        const auto &context = static_cast<QContextMenuEvent &>(event);
        if (context.reason() == QContextMenuEvent::Mouse) {
            if (rightPress_ && rightPress_->widget == &widget) {
                // macOS presents a context menu on press; defer until release
                // so a right-button drag can reach the source drop menu.
                rightPress_->contextDeferred = true;
                return true;
            }
            if (rightDragConsumed_)
                return true;
        }
    } else if (event.type() == QEvent::MouseButtonPress) {
        const auto &mouse = static_cast<QMouseEvent &>(event);
        rightDragConsumed_ = false;
        rightPress_.reset();
        if (mouse.button() == Qt::RightButton) {
            const QPoint position = mouse.position().toPoint();
            const QModelIndex item = view->indexAt(position);
            const bool onItem =
                item.isValid() && item.data(QtPanelList::SourceIndexRole).toUInt() != UInt32(-1);
            rightPress_.emplace(RightPress{&widget, position, onItem});
        }
    } else if (event.type() == QEvent::MouseMove && rightPress_ && rightPress_->widget == &widget) {
        const auto &mouse = static_cast<QMouseEvent &>(event);
        if (rightPress_->onItem && (mouse.buttons() & Qt::RightButton) && PanelEnabled(panel) &&
            (mouse.position().toPoint() - rightPress_->position).manhattanLength() >=
                QApplication::startDragDistance()) {
            rightDragConsumed_ = true;
            rightPress_.reset();
            StartDrag(panel, true);
            return true;
        }
    } else if (event.type() == QEvent::MouseButtonRelease) {
        const auto &mouse = static_cast<QMouseEvent &>(event);
        if (mouse.button() == Qt::RightButton && rightPress_ && rightPress_->widget == &widget) {
            const bool contextDeferred = rightPress_->contextDeferred;
            rightPress_.reset();
            if (contextDeferred) {
                QContextMenuEvent context(QContextMenuEvent::Mouse, mouse.position().toPoint(),
                                          mouse.globalPosition().toPoint(), mouse.modifiers());
                QCoreApplication::sendEvent(&widget, &context);
            }
        }
    }
    return false;
}

bool QtFileManagerDrag::HandleDropEvent(QWidget &widget, QEvent &event) {
    if (event.type() == QEvent::DragLeave) {
        window_.app_.DropTarget().DragLeave();
        ResetDropTarget();
        return true;
    }
    auto &drop = static_cast<QDropEvent &>(event);
    auto &target = window_.app_.DropTarget();
    const QPoint position = widget.mapToGlobal(drop.position().toPoint());
    const int panel = PanelAtPoint(position.x(), position.y());
    const bool blocked = revoked_ || window_.app_.QtOperations().closed() ||
                         window_.backend_->busy() || QApplication::activeModalWidget() ||
                         (window_.sourceArchiveProgress_ && !active_) ||
                         (panel >= 0 && !PanelEnabled(unsigned(panel)));
    CDataObject *data = blocked ? nullptr : ReadData(*drop.mimeData());
    if (!data) {
        if (blocked && target.HasDragData())
            target.DragLeave();
        ResetDropTarget();
        drop.ignore();
        return true;
    }
    const DWORD keys = DropKeys(drop);
    const bool entering = event.type() == QEvent::DragEnter || !target.HasDragData();
    const bool menuDrop = (keys & (MK_RBUTTON | MK_MBUTTON)) ||
                          (event.type() != QEvent::DragEnter && target.IsMenuDrop());
    // A left drop of external URLs opens or archives the source in place.
    const bool canMove = data != &external_ || data->NativeData || menuDrop;
    DWORD effect = 0;
    if (drop.possibleActions() & Qt::CopyAction)
        effect |= DROPEFFECT_COPY;
    if (canMove && (drop.possibleActions() & Qt::MoveAction))
        effect |= DROPEFFECT_MOVE;
    const DWORD allowedEffects = effect;
    HRESULT result;
    const auto finishDrop = qScopeGuard([this, &event] {
        if (event.type() == QEvent::Drop) {
            ResetDropTarget();
            window_.refreshActions();
        }
    });
    if (entering)
        result = target.DragEnter(data, keys, position.x(), position.y(), &effect);
    else
        result = target.DragOver(keys, position.x(), position.y(), &effect);
    if (event.type() == QEvent::Drop && result == S_OK && effect) {
        if (target.IsMenuDrop())
            effect = allowedEffects;
        result = ExecuteDrop(data, keys, position, effect);
    }
    if (result == S_OK && effect) {
        drop.setDropAction(effect == DROPEFFECT_MOVE ? Qt::MoveAction : Qt::CopyAction);
        drop.accept();
    } else {
        drop.ignore();
        if (entering && event.type() != QEvent::Drop) {
            // Qt sends subsequent move/leave events only after accepting entry.
            // Balance the native Enter notification before discarding its state.
            target.DragLeave();
            ResetDropTarget();
        }
    }
    return true;
}

void QtFileManagerDrag::ResetDropTarget() {
    window_.app_.DropTarget().ClearState();
    external_.NativeData.reset();
}
