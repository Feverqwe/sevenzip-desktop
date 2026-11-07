#include "filemanager/window/qtnavigationbar.h"
#include "filemanager/window/qtmainwindowchrome.h"
#include "filemanager/window/qtworkspacetheme.h"
#include "localization/qtlangcontrols.h"
#include "ui/theme/qtcommandicons.h"
#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>

namespace {
class CrumbButton final : public QToolButton {
    QString label_;

  public:
    CrumbButton(const QString &label, QWidget *parent) : QToolButton(parent), label_(label) {
        setText(label);
        QtConfigureChromeToolButton(*this);
        setMinimumWidth(20);
        setMaximumWidth(160);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    }
    QSize minimumSizeHint() const override { return {20, QToolButton::minimumSizeHint().height()}; }
    QSize sizeHint() const override {
        return {qBound(20,
                       fontMetrics().horizontalAdvance(label_) + 12 + (icon().isNull() ? 0 : 20),
                       160),
                QToolButton::sizeHint().height()};
    }

  protected:
    void resizeEvent(QResizeEvent *event) override {
        QToolButton::resizeEvent(event);
        setText(fontMetrics().elidedText(label_, Qt::ElideMiddle,
                                         qMax(0, width() - 12 - (icon().isNull() ? 0 : 20))));
    }
};
} // namespace

QtNavigationRow::QtNavigationRow(QWidget *parent) : QWidget(parent) {
    QtWorkspaceTheme::pane(*this);
}

void QtNavigationRow::SetActivePane(bool active) {
    if (active_ == active)
        return;
    active_ = active;
    update();
}

void QtNavigationRow::paintEvent(QPaintEvent *) {
    if (active_) {
        QPainter painter(this);
        painter.fillRect(0, height() - 2, width(), 2, QtWorkspaceTheme::colors().accent);
    }
}

QtNavigationBar::QtNavigationBar(QComboBox &address, std::function<void(const QString &)> navigate,
                                 QWidget *parent, std::function<void()> beginEdit)
    : QWidget(parent), address_(address), stack_(new QStackedWidget(this)),
      crumbs_(new QWidget(stack_)), layout_(new QHBoxLayout(crumbs_)),
      navigate_(std::move(navigate)), beginEdit_(std::move(beginEdit)) {
    setMinimumWidth(80);
    setMinimumHeight(QtWorkspaceTheme::metric(*this, 32));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *outer = new QHBoxLayout(this);
    outer->setContentsMargins(1, 1, 1, 1);
    outer->setSpacing(0);
    layout_->setContentsMargins(4, 0, 4, 0);
    layout_->setSpacing(2);
    stack_->addWidget(crumbs_);
    stack_->addWidget(&address_);
    outer->addWidget(stack_);
    address_.lineEdit()->installEventFilter(this);
    SetLocation({});
}

void QtNavigationBar::paintEvent(QPaintEvent *) {
    const auto c = QtWorkspaceTheme::colors();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(c.stroke);
    painter.setBrush(c.surface);
    painter.drawRoundedRect(QRectF(rect()).adjusted(.5, .5, -.5, -.5), 4, 4);
}

void QtNavigationBar::SetLocation(const QString &path,
                                  const std::vector<QtAddressSegment> &segments) {
    const QString rootLabel = QCoreApplication::translate("QtWindowWorkspace", "Locations");
    if (path_ == path && segments_ == segments && rootLabel_ == rootLabel && layout_->count())
        return;
    path_ = path;
    segments_ = segments;
    setToolTip(path);
    rootLabel_ = rootLabel;
    Rebuild();
}

void QtNavigationBar::Rebuild() {
    while (auto *item = layout_->takeAt(0)) {
        if (item->widget()) {
            item->widget()->hide();
            item->widget()->deleteLater();
        }
        delete item;
    }
    const auto add = [this](const QString &label, const QString &target) {
        if (layout_->count()) {
            auto *separator = new QLabel(QStringLiteral("›"), crumbs_);
            separator->setAlignment(Qt::AlignCenter);
            separator->setFixedWidth(10);
            layout_->addWidget(separator);
        }
        auto *button = new CrumbButton(label, crumbs_);
        button->setAutoRaise(true);
        button->setText(label);
        button->setToolTip(target);
        button->setAccessibleName(target);
        connect(button, &QToolButton::clicked, this, [this, target] { navigate_(target); });
        layout_->addWidget(button);
    };
    if (segments_.empty())
        add(path_.isEmpty() ? rootLabel_ : path_, path_);
    // Earlier ancestors remain available in a menu at compact widths.
    auto *ancestors = new QToolButton(crumbs_);
    ancestors->setText(QStringLiteral("…"));
    ancestors->setAutoRaise(true);
    QtConfigureChromeToolButton(*ancestors);
    ancestors->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(ancestors);
    ancestors->setMenu(menu);
    const int visibleFrom = qMax(0, int(segments_.size()) - visibleCrumbs_);
    if (visibleFrom)
        layout_->addWidget(ancestors);
    else {
        ancestors->hide();
        ancestors->deleteLater();
    }
    for (int i = 0; i < int(segments_.size()); ++i) {
        const auto &segment = segments_[i];
        const QString target = QString::fromWCharArray(segment.target);
        const QString label =
            segment.label.IsEmpty() ? QStringLiteral("/") : QString::fromWCharArray(segment.label);
        const bool boundary = segment.kind == QtAddressSegmentKind::Archive;
        if (i < visibleFrom) {
            auto *ancestor = menu->addAction(label);
            ancestor->setToolTip(target);
            if (boundary) {
                ancestor->setIcon(QtCommandSymbol(QtCommandIcon::Archive));
                ancestors->setIcon(ancestor->icon());
                ancestors->setToolTip(path_);
            }
            connect(ancestor, &QAction::triggered, this, [this, target] { navigate_(target); });
        } else {
            add(label, target);
            if (boundary) {
                auto *button =
                    qobject_cast<QToolButton *>(layout_->itemAt(layout_->count() - 1)->widget());
                button->setIcon(QtCommandSymbol(QtCommandIcon::Archive));
                button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            }
        }
    }
    auto *edit = new QToolButton(crumbs_);
    edit->setObjectName("editLocationButton");
    edit->setIcon(QtCommandSymbol(QtCommandIcon::EditLocation));
    edit->setAutoRaise(true);
    QtConfigureChromeToolButton(*edit);
    edit->setToolTip(tr("Edit location"));
    QtBindLocalText(edit, "QtNavigationBar", QT_TRANSLATE_NOOP("QtNavigationBar", "Edit location"),
                    true);
    QtBindLocalToolTip(edit, "QtNavigationBar",
                       QT_TRANSLATE_NOOP("QtNavigationBar", "Edit location"));
    edit->setAccessibleName(edit->toolTip());
    connect(edit, &QToolButton::clicked, this, &QtNavigationBar::Edit);
    layout_->addStretch();
    layout_->addWidget(edit);
    // Rebuild can run inside resizeEvent on an already visible path field.
    // Make the replacement crumbs visible in that same layout pass.
    for (int i = 0; i < layout_->count(); ++i)
        if (auto *widget = layout_->itemAt(i)->widget())
            widget->show();
    layout_->activate();
}

void QtNavigationBar::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    const int visible = width() < 300 ? 1 : width() < 500 ? 2 : 3;
    if (visible != visibleCrumbs_) {
        visibleCrumbs_ = visible;
        Rebuild();
    }
}

void QtNavigationBar::Edit() {
    if (beginEdit_)
        beginEdit_();
    ResumeEditing();
    address_.lineEdit()->setFocus();
    address_.lineEdit()->selectAll();
}

void QtNavigationBar::ResumeEditing() {
    stack_->setCurrentWidget(&address_);
    if (window()->isActiveWindow())
        address_.lineEdit()->setFocus();
}

void QtNavigationBar::FinishEditing() { stack_->setCurrentWidget(crumbs_); }

bool QtNavigationBar::eventFilter(QObject *object, QEvent *event) {
    if (object == address_.lineEdit() && event->type() == QEvent::FocusIn)
        stack_->setCurrentWidget(&address_);
    if (object == address_.lineEdit() && event->type() == QEvent::FocusOut)
        QTimer::singleShot(0, this, [this] {
            if (window()->isActiveWindow() && !QApplication::activeModalWidget() &&
                !address_.hasFocus() && !address_.lineEdit()->hasFocus() &&
                !address_.view()->isVisible())
                FinishEditing();
        });
    return QWidget::eventFilter(object, event);
}
