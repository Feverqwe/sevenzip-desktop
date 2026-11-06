#include "ui/controls/qtdisclosure.h"
#include "ui/dialogs/qtdialoglayout.h"
#include <QAccessibleWidget>
#include <QFrame>
#include <QKeySequence>
#include <QLabel>
#include <QPainterPath>
#include <QStyleOptionToolButton>
#include <QStylePainter>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
class AccessibleDisclosure final : public QAccessibleWidget {
  public:
    explicit AccessibleDisclosure(QToolButton *button)
        : QAccessibleWidget(button, QAccessible::Button) {}

    QString text(QAccessible::Text type) const override {
        if (type == QAccessible::Name && widget()->accessibleName().isEmpty())
            return button()->text();
        return QAccessibleWidget::text(type);
    }

    QAccessible::State state() const override {
        auto result = QAccessibleWidget::state();
        result.expandable = true;
        result.expanded = button()->isChecked();
        result.collapsed = !result.expanded;
        return result;
    }

    QStringList actionNames() const override {
        auto result = QAccessibleWidget::actionNames();
        result.prepend(pressAction());
        return result;
    }

    void doAction(const QString &action) override {
        // Qt's Windows UIA ExpandCollapse provider dispatches showMenuAction;
        // Cocoa keeps using the advertised press action for a disclosure.
        if (action == pressAction() || action == showMenuAction())
            button()->click();
        else
            QAccessibleWidget::doAction(action);
    }

    QStringList keyBindingsForAction(const QString &action) const override {
        if (action == pressAction())
            return {QKeySequence(Qt::Key_Space).toString(QKeySequence::NativeText)};
        return QAccessibleWidget::keyBindingsForAction(action);
    }

  private:
    QToolButton *button() const { return static_cast<QToolButton *>(widget()); }
};

class DisclosureButton final : public QToolButton {
  public:
    using QToolButton::QToolButton;

    QSize sizeHint() const override {
        return QSize(fontMetrics().horizontalAdvance(text()) + qMax(8, fontMetrics().capHeight()) +
                         18,
                     qMax(24, fontMetrics().height() + 8));
    }

  protected:
    void paintEvent(QPaintEvent *) override {
        QStyleOptionToolButton option;
        initStyleOption(&option);
        QStylePainter painter(this);
        // Native toolbar arrows can shrink the glyph differently by direction.
        // Use one font-scaled chevron, rotating it without changing its bounds.
        const qreal extent = qMax(6.0, fontMetrics().capHeight() * 0.75);
        const QRect slot = QStyle::visualRect(layoutDirection(), rect(),
                                              QRect(2, 0, qRound(extent + 4), height()));
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing);
        painter.translate(QRectF(slot).center());
        if (isChecked())
            painter.rotate(90);
        else if (layoutDirection() == Qt::RightToLeft)
            painter.rotate(180);
        painter.setPen(
            QPen(option.palette.color(isEnabled() ? QPalette::Active : QPalette::Disabled,
                                      QPalette::ButtonText),
                 qMax(1.0, extent / 6), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPainterPath chevron;
        chevron.moveTo(-extent / 4, -extent / 2);
        chevron.lineTo(extent / 4, 0);
        chevron.lineTo(-extent / 4, extent / 2);
        painter.drawPath(chevron);
        painter.restore();
        const QRect label =
            QStyle::visualRect(layoutDirection(), rect(),
                               rect().adjusted(qMax(8, fontMetrics().capHeight()) + 12, 0, 0, 0));
        style()->drawItemText(&painter, label, Qt::AlignLeading | Qt::AlignVCenter, option.palette,
                              isEnabled(), text(), QPalette::ButtonText);
        if (hasFocus()) {
            QStyleOptionFocusRect focus;
            focus.initFrom(this);
            focus.rect = rect().adjusted(1, 1, -1, -1);
            painter.drawPrimitive(QStyle::PE_FrameFocusRect, focus);
        }
    }
};
} // namespace

QtDisclosure::QtDisclosure(QWidget *content, QWidget *parent, QWidget *description)
    : QWidget(parent), content_(content), description_(description),
      button_(new DisclosureButton(this)), summary_(new QtWrappingLabel(this)) {
    // Qt owns the interface for the lifetime of this button.
    QAccessible::registerAccessibleInterface(new AccessibleDisclosure(button_));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    auto *separator = new QFrame(this);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Plain);
    separator->setForegroundRole(QPalette::Mid);
    layout->addWidget(separator);
    button_->setCheckable(true);
    button_->setFont(font());
    button_->setAutoRaise(true);
    button_->setIconSize(QSize(10, 10));
    button_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button_->setArrowType(Qt::RightArrow);
    button_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    button_->setFocusPolicy(Qt::StrongFocus);
    auto *header = new QHBoxLayout;
    header->addWidget(button_, 0, Qt::AlignLeading | Qt::AlignTop);
    summary_->setTextFormat(Qt::PlainText);
    summary_->setWordWrap(true);
    summary_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    summary_->setObjectName("disclosureSummary");
    summary_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    header->addWidget(summary_, 1, Qt::AlignVCenter);
    layout->addLayout(header);
    if (description_)
        layout->addWidget(description_);
    layout->addWidget(content_);
    content_->hide();
    summary_->hide();
    connect(button_, &QToolButton::toggled, this, [this](bool expanded) {
        button_->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        content_->setVisible(expanded);
        if (description_)
            description_->setVisible(!expanded);
        summary_->setVisible(!expanded && !summary_->text().isEmpty());
        QAccessible::State changed;
        changed.expanded = true;
        changed.collapsed = true;
        QAccessibleStateChangeEvent event(button_, changed);
        QAccessible::updateAccessibility(&event);
    });
}

void QtDisclosure::setExpanded(bool expanded) { button_->setChecked(expanded); }
bool QtDisclosure::isExpanded() const { return button_->isChecked(); }
void QtDisclosure::setSummary(const QString &text) {
    summary_->setText(text);
    summary_->setVisible(!isExpanded() && !text.isEmpty());
}
