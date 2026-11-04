#include "filemanager/window/qtworkspacetabbar.h"
#include "filemanager/window/qtmainwindowchrome.h"
#include "filemanager/window/qtwindowworkspace.h"
#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPointer>
#include <utility>

namespace {
class TabMime final : public QMimeData {
  public:
    QPointer<QtWindowWorkspace> owner;
    QString id;
};
} // namespace

QtWorkspaceTabBar::QtWorkspaceTabBar(QtWindowWorkspace &workspace, unsigned panel, QWidget *parent)
    : QTabBar(parent), workspace_(workspace), panel_(panel) {
    setAcceptDrops(true);
    QtConfigureWorkspaceTabs(*this);
}

void QtWorkspaceTabBar::mousePressEvent(QMouseEvent *event) {
    pressed_ = event->position().toPoint();
    pressedId_ = tabData(tabAt(pressed_)).toString();
    QTabBar::mousePressEvent(event);
}

void QtWorkspaceTabBar::mouseMoveEvent(QMouseEvent *event) {
    if (!(event->buttons() & Qt::LeftButton) || pressedId_.isEmpty() ||
        rect().contains(event->position().toPoint()) ||
        (event->position().toPoint() - pressed_).manhattanLength() <
            QApplication::startDragDistance()) {
        QTabBar::mouseMoveEvent(event);
        return;
    }
    QMouseEvent released(QEvent::MouseButtonRelease, event->position(), event->globalPosition(),
                         Qt::LeftButton, Qt::NoButton, event->modifiers());
    QTabBar::mouseReleaseEvent(&released);
    QDrag drag(this);
    auto *mime = new TabMime;
    mime->owner = &workspace_;
    mime->id = std::exchange(pressedId_, {});
    mime->setData("application/x-sevenzip-tab", mime->id.toUtf8());
    drag.setMimeData(mime);
    drag.exec(Qt::MoveAction);
}

void QtWorkspaceTabBar::dragEnterEvent(QDragEnterEvent *event) {
    if (const auto *mime = dynamic_cast<const TabMime *>(event->mimeData()); mime && mime->owner)
        event->acceptProposedAction();
}
void QtWorkspaceTabBar::dragMoveEvent(QDragMoveEvent *event) {
    if (const auto *mime = dynamic_cast<const TabMime *>(event->mimeData()); mime && mime->owner)
        event->acceptProposedAction();
}
void QtWorkspaceTabBar::dropEvent(QDropEvent *event) {
    const auto *mime = dynamic_cast<const TabMime *>(event->mimeData());
    if (mime && mime->owner && mime->owner->TransferTab(mime->id, workspace_, panel_))
        event->acceptProposedAction();
}
