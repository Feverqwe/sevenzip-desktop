#include "filemanager/window/qtworkspacetabbar.h"
#include "filemanager/window/qtmainwindowchrome.h"
#include "filemanager/window/qtwindowworkspace.h"
#include "filemanager/window/qtworkspacetheme.h"
#include <QApplication>
#include <QCursor>
#include <QDrag>
#include <QDragEnterEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPointer>
#include <QScopedValueRollback>
#include <QTimer>
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
    setMouseTracking(true);
    QtConfigureWorkspaceTabs(*this);
    connect(this, &QTabBar::currentChanged, this, [this] { UpdateCloseButtons(); });
}

QSize QtWorkspaceTabBar::tabSizeHint(int index) const {
    const auto native = QTabBar::tabSizeHint(index);
    return {qBound(QtWorkspaceTheme::metric(*this, 44), native.width(),
                   QtWorkspaceTheme::metric(*this, 220)),
            QtWorkspaceTheme::metric(*this, 34)};
}

QSize QtWorkspaceTabBar::minimumTabSizeHint(int) const {
    return {QtWorkspaceTheme::metric(*this, 44), QtWorkspaceTheme::metric(*this, 34)};
}

void QtWorkspaceTabBar::mousePressEvent(QMouseEvent *event) {
    pressed_ = event->position().toPoint();
    pressedId_ = tabData(tabAt(pressed_)).toString();
    QTabBar::mousePressEvent(event);
}

void QtWorkspaceTabBar::mouseMoveEvent(QMouseEvent *event) {
    UpdateCloseButtons();
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

void QtWorkspaceTabBar::UpdateCloseButtons() {
    if (updating_)
        return;
    const QScopedValueRollback<bool> updating(updating_, true);
    const QPoint cursor = QCursor::pos();
    const auto *hit = QApplication::widgetAt(cursor);
    const int hovered =
        hit && (hit == this || isAncestorOf(hit)) ? tabAt(mapFromGlobal(cursor)) : -1;
    for (int index = 0; index < count(); ++index)
        if (auto *close = tabButton(index, QTabBar::RightSide))
            close->setVisible(tabRect(index).width() >= QtWorkspaceTheme::metric(*this, 100) ||
                              index == currentIndex() || index == hovered);
    update();
}

void QtWorkspaceTabBar::tabLayoutChange() {
    QTabBar::tabLayoutChange();
    if (updatePending_)
        return;
    updatePending_ = true;
    QTimer::singleShot(0, this, [this] {
        updatePending_ = false;
        UpdateCloseButtons();
    });
}

void QtWorkspaceTabBar::resizeEvent(QResizeEvent *event) {
    QTabBar::resizeEvent(event);
    UpdateCloseButtons();
}

void QtWorkspaceTabBar::leaveEvent(QEvent *event) {
    QTabBar::leaveEvent(event);
    UpdateCloseButtons();
}

void QtWorkspaceTabBar::enterEvent(QEnterEvent *event) {
    QTabBar::enterEvent(event);
    UpdateCloseButtons();
}

void QtWorkspaceTabBar::mouseReleaseEvent(QMouseEvent *event) {
    const int index = tabAt(event->position().toPoint());
    const QString pressed = std::exchange(pressedId_, {});
    if (event->button() == Qt::MiddleButton && index >= 0 && tabData(index).toString() == pressed) {
        event->accept();
        emit tabCloseRequested(index);
        return;
    }
    QTabBar::mouseReleaseEvent(event);
}

bool QtWorkspaceTabBar::event(QEvent *event) {
    if (event->type() == QEvent::ShortcutOverride) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->modifiers() == Qt::NoModifier &&
            (key->key() == Qt::Key_Home || key->key() == Qt::Key_End ||
             key->key() == Qt::Key_Left || key->key() == Qt::Key_Right)) {
            event->accept();
            return true;
        }
    }
    return QTabBar::event(event);
}

void QtWorkspaceTabBar::keyPressEvent(QKeyEvent *event) {
    if (event->modifiers() == Qt::NoModifier &&
        (event->key() == Qt::Key_Home || event->key() == Qt::Key_End)) {
        if (count())
            setCurrentIndex(event->key() == Qt::Key_Home ? 0 : count() - 1);
        event->accept();
        return;
    }
    QTabBar::keyPressEvent(event);
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
