#include "filemanager/qtfilemanagerdrag.h"
#include "filemanager/window/mainwindow.h"
#include <QContextMenuEvent>
#include <QMouseEvent>

bool QtFileManagerDrag::HandleRightDrag(QWidget &widget, QEvent &event) {
    const unsigned panel = window_.panelIndexFor(&widget);
    if (revoked_ || panel >= kNumPanelsMax)
        return false;
    const auto &ui = window_.panelUi_[panel];
    if ((!ui.table || &widget != ui.table->viewport()) &&
        (!ui.iconList || &widget != ui.iconList->viewport()))
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
        if (mouse.button() == Qt::RightButton)
            rightPress_.emplace(RightPress{&widget, mouse.position().toPoint()});
    } else if (event.type() == QEvent::MouseMove && rightPress_ && rightPress_->widget == &widget) {
        const auto &mouse = static_cast<QMouseEvent &>(event);
        if ((mouse.buttons() & Qt::RightButton) && PanelEnabled(panel) &&
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
