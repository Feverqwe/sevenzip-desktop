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

class QtNavigationBar::CrumbButton final : public QToolButton {
    QString label_;

    int Padding() const { return 12 + (icon().isNull() ? 0 : 20); }

  public:
    CrumbButton(const QString &label, QWidget *parent) : QToolButton(parent), label_(label) {
        setText(label);
        QtConfigureChromeToolButton(*this);
    }
    int FullWidth() const { return qMax(20, fontMetrics().horizontalAdvance(label_) + Padding()); }
    void FitWidth(int available) {
        const int width = qBound(20, available, FullWidth());
        setFixedWidth(width);
        const QString text =
            width == FullWidth()
                ? label_
                : fontMetrics().elidedText(label_, Qt::ElideMiddle, qMax(0, width - Padding()));
        if (this->text() != text)
            setText(text);
    }
};

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
    crumbButtons_.clear();
    ancestors_ = new QToolButton(crumbs_);
    ancestors_->setText(QStringLiteral("…"));
    ancestors_->setAutoRaise(true);
    QtConfigureChromeToolButton(*ancestors_);
    ancestors_->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(ancestors_);
    ancestors_->setMenu(menu);
    layout_->addWidget(ancestors_);
    const auto add = [this, menu](const QString &label, const QString &target, bool archive) {
        auto *separator = new QLabel(QStringLiteral("›"), crumbs_);
        separator->setAlignment(Qt::AlignCenter);
        separator->setFixedWidth(10);
        layout_->addWidget(separator);
        auto *button = new CrumbButton(label, crumbs_);
        button->setAutoRaise(true);
        button->setToolTip(target);
        button->setAccessibleName(target);
        connect(button, &QToolButton::clicked, this, [this, target] { navigate_(target); });
        layout_->addWidget(button);
        auto *ancestor = menu->addAction(label);
        ancestor->setToolTip(target);
        connect(ancestor, &QAction::triggered, this, [this, target] { navigate_(target); });
        if (archive) {
            ancestor->setIcon(QtCommandSymbol(QtCommandIcon::Archive));
            button->setIcon(ancestor->icon());
            button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        }
        crumbButtons_.push_back({button, separator, ancestor});
    };
    if (segments_.empty())
        add(path_.isEmpty() ? rootLabel_ : path_, path_, false);
    for (const auto &segment : segments_) {
        const QString target = QString::fromWCharArray(segment.target);
        const QString label =
            segment.label.IsEmpty() ? QStringLiteral("/") : QString::fromWCharArray(segment.label);
        add(label, target, segment.kind == QtAddressSegmentKind::Archive);
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
    edit_ = edit;
    edit_->show();
    FitCrumbs();
}

void QtNavigationBar::FitCrumbs() {
    if (crumbButtons_.empty())
        return;
    const int spacing = layout_->spacing();
    const int separator = crumbButtons_.front().separator->width() + 2 * spacing;
    const auto outerMargins = layout()->contentsMargins();
    const auto innerMargins = layout_->contentsMargins();
    const int available = width() - outerMargins.left() - outerMargins.right() -
                          innerMargins.left() - innerMargins.right() - edit_->sizeHint().width() -
                          spacing;
    int used = -separator;
    for (const auto &crumb : crumbButtons_)
        used += separator + crumb.button->FullWidth();

    size_t first = 0;
    QIcon hiddenArchive;
    if (used > available && crumbButtons_.size() > 1) {
        // Reserve the menu and its separator before fitting the visible suffix.
        ancestors_->setIcon({});
        used += ancestors_->sizeHint().width() + separator;
        while (used > available && first + 1 < crumbButtons_.size()) {
            const auto &crumb = crumbButtons_[first++];
            used -= separator + crumb.button->FullWidth();
            if (hiddenArchive.isNull() && !crumb.ancestor->icon().isNull()) {
                used -= ancestors_->sizeHint().width();
                hiddenArchive = crumb.ancestor->icon();
                ancestors_->setIcon(hiddenArchive);
                used += ancestors_->sizeHint().width();
            }
        }
    }
    ancestors_->setIcon(hiddenArchive);
    ancestors_->setToolTip(hiddenArchive.isNull() ? QString() : path_);
    ancestors_->setVisible(first != 0);
    for (size_t i = 0; i < crumbButtons_.size(); ++i) {
        const auto &crumb = crumbButtons_[i];
        if (i >= first) {
            const int excess = i + 1 == crumbButtons_.size() ? qMax(0, used - available) : 0;
            crumb.button->FitWidth(crumb.button->FullWidth() - excess);
        }
        crumb.button->setVisible(i >= first);
        crumb.separator->setVisible(i >= first && i != 0);
        crumb.ancestor->setVisible(i < first);
    }
    layout_->activate();
}

void QtNavigationBar::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    FitCrumbs();
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
