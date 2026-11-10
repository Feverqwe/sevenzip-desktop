#include "filemanager/views/qtfileselectiongesture.h"
#include "ui/theme/qtuitheme.h"
#include <QAbstractItemView>
#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QScrollBar>
#include <QTimer>
#include <memory>

namespace {
class SelectionFrame final : public QWidget {
    QRect rectangle_;

  public:
    explicit SelectionFrame(QWidget &viewport) : QWidget(&viewport) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setFocusPolicy(Qt::NoFocus);
        hide();
    }
    void SetRectangle(QRect rectangle) {
        setGeometry(parentWidget()->rect());
        rectangle_ = rectangle.intersected(rect());
        show();
        raise();
        update();
    }

  protected:
    void paintEvent(QPaintEvent *) override {
        if (rectangle_.isEmpty())
            return;
        QPainter painter(this);
        const QColor accent = QtUiTheme::colors().accent;
        QColor fill = accent;
        fill.setAlphaF(.12);
        painter.setPen(accent);
        painter.setBrush(fill);
        painter.drawRect(QRectF(rectangle_).adjusted(.5, .5, -.5, -.5));
    }
};

class FileSelectionGesture final : public QObject {
    QtFileSelectionGeometry geometry_;
    std::function<bool()> activate_;
    SelectionFrame *frame_;
    QTimer scroll_;
    QItemSelection baseline_;
    QPersistentModelIndex current_;
    std::unique_ptr<QMouseEvent> cellPress_;
    QPoint origin_, pointer_;
    QItemSelectionModel::SelectionFlags command_;
    bool pressed_ = false, dragging_ = false, cancelled_ = false;
    QPointer<QAbstractItemModel> model_;
    QList<QMetaObject::Connection> modelConnections_;

    QAbstractItemView *View() const { return qobject_cast<QAbstractItemView *>(parent()); }
    void Cancel(bool restore) {
        if (cancelled_)
            return;
        auto *view = View();
        if (restore && view && view->model() == model_) {
            view->selectionModel()->select(baseline_, QItemSelectionModel::ClearAndSelect);
            view->selectionModel()->setCurrentIndex(current_, QItemSelectionModel::NoUpdate);
        }
        cancelled_ = true;
        scroll_.stop();
        frame_->hide();
    }
    void CancelForModelChange() {
        if (!pressed_)
            return;
        Cancel(false);
        baseline_.clear();
        current_ = QPersistentModelIndex();
    }
    void Finish(bool restore) {
        if (!pressed_)
            return;
        auto *view = View();
        if (view && restore && !cancelled_ && view->model() == model_ && view->selectionModel()) {
            view->selectionModel()->select(baseline_, QItemSelectionModel::ClearAndSelect);
            view->selectionModel()->setCurrentIndex(current_, QItemSelectionModel::NoUpdate);
        }
        scroll_.stop();
        frame_->hide();
        if (view && QWidget::mouseGrabber() == view->viewport())
            view->viewport()->releaseMouse();
        pressed_ = dragging_ = cancelled_ = false;
        baseline_ = {};
        current_ = QPersistentModelIndex();
        cellPress_.reset();
    }
    void WatchModel(QAbstractItemModel &model) {
        if (model_ == &model)
            return;
        for (const auto &connection : modelConnections_)
            disconnect(connection);
        model_ = &model;
        const auto abort = [this] { CancelForModelChange(); };
        modelConnections_ = {
            connect(&model, &QAbstractItemModel::modelAboutToBeReset, this, abort),
            connect(&model, &QAbstractItemModel::rowsAboutToBeRemoved, this, abort),
            connect(&model, &QAbstractItemModel::rowsAboutToBeInserted, this, abort),
            connect(&model, &QAbstractItemModel::layoutAboutToBeChanged, this, abort)};
    }
    bool NearEdge(const QAbstractItemView &view) const {
        const auto size = view.viewport()->size();
        const int margin = view.autoScrollMargin();
        return pointer_.x() < margin || pointer_.y() < margin ||
               pointer_.x() >= size.width() - margin || pointer_.y() >= size.height() - margin;
    }
    void Update() {
        auto *view = View();
        if (!view || !pressed_ || cancelled_)
            return;
        if (view->model() != model_) {
            CancelForModelChange();
            return;
        }
        if (!view->isEnabled() || view->selectionMode() != QAbstractItemView::ExtendedSelection) {
            Finish(false);
            return;
        }
        const QRect area = QRect(origin_ - geometry_.offset(), pointer_).normalized();
        geometry_.select(area, command_ | QItemSelectionModel::Rows);
        frame_->SetRectangle(area);
        if (view->hasAutoScroll() && NearEdge(*view)) {
            if (!scroll_.isActive())
                scroll_.start();
        } else
            scroll_.stop();
    }
    void Scroll() {
        auto *view = View();
        if (!view || !dragging_ || cancelled_)
            return;
        const auto scrollAxis = [](QScrollBar &bar, int point, int length, int margin) {
            if (point < margin)
                bar.triggerAction(QAbstractSlider::SliderSingleStepSub);
            else if (point >= length - margin)
                bar.triggerAction(QAbstractSlider::SliderSingleStepAdd);
        };
        scrollAxis(*view->horizontalScrollBar(), pointer_.x(), view->viewport()->width(),
                   view->autoScrollMargin());
        scrollAxis(*view->verticalScrollBar(), pointer_.y(), view->viewport()->height(),
                   view->autoScrollMargin());
        Update();
    }
    bool eventFilter(QObject *object, QEvent *event) override {
        auto *view = View();
        if (!view)
            return false;
        if (pressed_ && (event->type() == QEvent::Hide || event->type() == QEvent::FocusOut ||
                         event->type() == QEvent::EnabledChange)) {
            Finish(true);
            return false;
        }
        if (pressed_ &&
            (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress)) {
            const auto *key = static_cast<QKeyEvent *>(event);
            if (key->key() == Qt::Key_Shift || key->key() == Qt::Key_Control ||
                key->key() == Qt::Key_Meta || key->key() == Qt::Key_Alt)
                return false;
            if (key->key() == Qt::Key_Escape) {
                Cancel(true);
                event->accept();
                return true;
            }
            Cancel(true);
            return false;
        }
        if (object != view->viewport())
            return false;
        if (event->type() == QEvent::MouseButtonPress) {
            const auto &mouse = *static_cast<QMouseEvent *>(event);
            if (mouse.button() != Qt::LeftButton || !view->isEnabled())
                return false;
            view->keyboardSearch({});
            if (view->selectionMode() != QAbstractItemView::ExtendedSelection ||
                geometry_.hitItem(mouse.position().toPoint()))
                return false;
            if (activate_ && !activate_())
                return true;
            if (geometry_.hitItem(mouse.position().toPoint()))
                return false;
            Finish(true);
            WatchModel(*view->model());
            view->setFocus(Qt::MouseFocusReason);
            baseline_ = view->selectionModel()->selection();
            current_ = view->currentIndex();
            origin_ = mouse.position().toPoint() + geometry_.offset();
            pointer_ = mouse.position().toPoint();
            const auto modifiers = mouse.modifiers();
            if (geometry_.click && view->indexAt(pointer_).isValid())
                cellPress_.reset(mouse.clone());
            command_ = modifiers.testFlag(Qt::ShiftModifier) ? QItemSelectionModel::SelectCurrent
                       : modifiers.testFlag(Qt::ControlModifier)
                           ? QItemSelectionModel::ToggleCurrent
                           : QItemSelectionModel::ClearAndSelect;
            // Commit any previous current-selection segment before Qt starts
            // replacing this gesture's SelectCurrent/ToggleCurrent segment.
            view->selectionModel()->select(QItemSelection(), QItemSelectionModel::Select);
            pressed_ = true;
            view->viewport()->grabMouse();
            event->accept();
            return true;
        }
        if (!pressed_)
            return false;
        if (event->type() == QEvent::MouseMove) {
            const auto &mouse = *static_cast<QMouseEvent *>(event);
            pointer_ = mouse.position().toPoint();
            if (!(mouse.buttons() & Qt::LeftButton)) {
                Finish(true);
                return false;
            }
            if (!dragging_ && (pointer_ + geometry_.offset() - origin_).manhattanLength() >=
                                  QApplication::startDragDistance())
                dragging_ = true;
            if (dragging_)
                Update();
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease &&
            static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
            auto *release = static_cast<QMouseEvent *>(event);
            pointer_ = release->position().toPoint();
            if (!cancelled_) {
                if ((pointer_ + geometry_.offset() - origin_).manhattanLength() >=
                    QApplication::startDragDistance())
                    dragging_ = true;
                if (dragging_)
                    Update();
                else if (cellPress_) {
                    auto press = std::move(cellPress_);
                    Finish(false);
                    geometry_.click(press.get(), release);
                    return true;
                } else if (command_ == QItemSelectionModel::ClearAndSelect)
                    view->selectionModel()->clearSelection();
                view->selectionModel()->select(QItemSelection(), QItemSelectionModel::Select);
            }
            Finish(false);
            event->accept();
            return true;
        }
        return false;
    }

  public:
    FileSelectionGesture(QAbstractItemView &view, QtFileSelectionGeometry geometry,
                         std::function<bool()> activate)
        : QObject(&view), frame_(new SelectionFrame(*view.viewport())) {
        scroll_.setInterval(30);
        connect(&scroll_, &QTimer::timeout, this, &FileSelectionGesture::Scroll);
        Reset(std::move(geometry), std::move(activate));
    }
    void Reset(QtFileSelectionGeometry geometry, std::function<bool()> activate) {
        Finish(true);
        geometry_ = std::move(geometry);
        activate_ = std::move(activate);
        auto *view = View();
        view->installEventFilter(this);
        view->viewport()->installEventFilter(this);
    }
};
} // namespace

void QtInstallFileSelectionGesture(QAbstractItemView &view, QtFileSelectionGeometry geometry,
                                   std::function<bool()> activate) {
    for (auto *child : view.children())
        if (auto *gesture = dynamic_cast<FileSelectionGesture *>(child)) {
            gesture->Reset(std::move(geometry), std::move(activate));
            return;
        }
    new FileSelectionGesture(view, std::move(geometry), std::move(activate));
}
