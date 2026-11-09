#include "filemanager/window/qtmainwindowchrome.h"
#include "filemanager/window/qtworkspacetheme.h"
#include <QApplication>
#include <QPainter>
#include <QProxyStyle>
#include <QPushButton>
#include <QStyleOptionToolButton>
#include <QToolBar>
#include <QToolButton>

namespace {
class PanelChromeStyle final : public QProxyStyle {
  public:
    explicit PanelChromeStyle(QWidget &owner) { setParent(&owner); }
    void drawComplexControl(ComplexControl control, const QStyleOptionComplex *option,
                            QPainter *painter, const QWidget *widget) const override {
        const auto *tool = qstyleoption_cast<const QStyleOptionToolButton *>(option);
        const auto *button = qobject_cast<const QToolButton *>(widget);
        if (control != CC_ToolButton || !tool || !button || !button->autoRaise()) {
            QProxyStyle::drawComplexControl(control, option, painter, widget);
            return;
        }
        // QMacStyle ignores autoRaise. Keep quiet navigation on every platform
        // with palette-derived hover/pressed/focus feedback and native labels.
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const QRectF frame = tool->rect.adjusted(1, 1, -1, -1);
        if (tool->state & (State_MouseOver | State_Sunken | State_On)) {
            QColor fill = QApplication::palette().color(QPalette::Highlight);
            fill.setAlphaF(tool->state & (State_Sunken | State_On) ? 0.20 : 0.10);
            painter->setPen(Qt::NoPen);
            painter->setBrush(fill);
            painter->drawRoundedRect(frame, 4, 4);
        }
        if (tool->state & State_HasFocus) {
            painter->setPen(QPen(QApplication::palette().color(QPalette::Highlight), 1));
            painter->setBrush(Qt::NoBrush);
            painter->drawRoundedRect(frame, 4, 4);
        }
        QStyleOptionToolButton label(*tool);
        if (tool->features & QStyleOptionToolButton::HasMenu) {
            label.rect.adjust(0, 0, -10, 0);
            QStyleOption arrow;
            arrow.rect =
                visualRect(tool->direction, tool->rect,
                           QRect(tool->rect.right() - 9, tool->rect.center().y() - 3, 7, 7));
            arrow.palette = tool->palette;
            arrow.state = tool->state;
            QProxyStyle::drawPrimitive(PE_IndicatorArrowDown, &arrow, painter, widget);
        }
        QProxyStyle::drawControl(CE_ToolButtonLabel, &label, painter, widget);
        painter->restore();
    }
};

class MainChromeStyle final : public QProxyStyle {
    const QToolBar &toolbar_;

  public:
    explicit MainChromeStyle(QToolBar &toolbar) : toolbar_(toolbar) { setParent(&toolbar); }

    int pixelMetric(PixelMetric metric, const QStyleOption *option,
                    const QWidget *widget) const override {
        if (metric == PM_ToolBarItemMargin)
            return 8;
        if (metric == PM_ToolBarItemSpacing)
            return 4;
        return QProxyStyle::pixelMetric(metric, option, widget);
    }

    QSize sizeFromContents(ContentsType type, const QStyleOption *option, const QSize &size,
                           const QWidget *widget) const override {
        const QSize native = QProxyStyle::sizeFromContents(type, option, size, widget);
        if (type != CT_ToolButton)
            return native;
        const auto *tool = qstyleoption_cast<const QStyleOptionToolButton *>(option);
        if (!tool)
            return native;
        const int horizontalPadding = 16;
        const int labelGap = tool->toolButtonStyle == Qt::ToolButtonTextBesideIcon ? 2 : 0;
        return native.expandedTo(
            QSize(size.width() + horizontalPadding + labelGap, qMax(26, size.height() + 8)));
    }

    void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter,
                       const QWidget *widget) const override {
        if (element != PE_IndicatorToolBarSeparator) {
            QProxyStyle::drawPrimitive(element, option, painter, widget);
            return;
        }
        painter->save();
        QColor line = option->palette.color(QPalette::WindowText);
        line.setAlphaF(0.22);
        painter->setPen(line);
        const QPoint center = option->rect.center();
        const int halfHeight = qMin(10, option->rect.height() / 2);
        painter->drawLine(center + QPoint(0, -halfHeight), center + QPoint(0, halfHeight));
        painter->restore();
    }

    void drawControl(ControlElement control, const QStyleOption *option, QPainter *painter,
                     const QWidget *widget) const override {
        const auto *tool = qstyleoption_cast<const QStyleOptionToolButton *>(option);
        if (control != CE_ToolButtonLabel || !tool) {
            QProxyStyle::drawControl(control, option, painter, widget);
            return;
        }
        // Cocoa's toolbar label uses a small bold font even when the widget
        // carries the application font. Keep native panel/focus painting, but
        // draw this label with the same regular font as the surrounding UI.
        painter->save();
        painter->setFont(widget ? widget->font() : QApplication::font());
        const bool text = tool->toolButtonStyle == Qt::ToolButtonTextBesideIcon;
        const int textWidth = text ? painter->fontMetrics().horizontalAdvance(tool->text) : 0;
        const int width = tool->iconSize.width() + (text ? 6 + textWidth : 0);
        const QRect row(tool->rect.center().x() - width / 2, tool->rect.top(), width,
                        tool->rect.height());
        QRect icon(row.left(), row.center().y() - tool->iconSize.height() / 2,
                   tool->iconSize.width(), tool->iconSize.height());
        icon = visualRect(tool->direction, tool->rect, icon);
        const bool enabled = tool->state & State_Enabled;
        tool->icon.paint(painter, icon, Qt::AlignCenter, enabled ? QIcon::Normal : QIcon::Disabled);
        if (text) {
            const QRect label = visualRect(
                tool->direction, tool->rect,
                QRect(row.left() + tool->iconSize.width() + 6, row.top(), textWidth, row.height()));
            const auto group = !enabled                     ? QPalette::Disabled
                               : tool->state & State_Active ? QPalette::Active
                                                            : QPalette::Inactive;
            painter->setPen(toolbar_.palette().color(group, QPalette::ButtonText));
            painter->drawText(label, Qt::AlignVCenter | Qt::AlignLeft, tool->text);
        }
        painter->restore();
    }

    void drawComplexControl(ComplexControl control, const QStyleOptionComplex *option,
                            QPainter *painter, const QWidget *widget) const override {
        const auto *tool = qstyleoption_cast<const QStyleOptionToolButton *>(option);
        if (control != CC_ToolButton || !tool) {
            QProxyStyle::drawComplexControl(control, option, painter, widget);
            return;
        }
        QStyleOptionToolButton panel(*tool);
        panel.icon = {};
        panel.text.clear();
        QProxyStyle::drawComplexControl(control, &panel, painter, widget);
        drawControl(CE_ToolButtonLabel, tool, painter, widget);
    }
};
} // namespace

void QtConfigureChromeToolButton(QToolButton &button) {
    button.setStyle(new PanelChromeStyle(button));
    button.setFont(QApplication::font());
    button.setMinimumHeight(QtWorkspaceTheme::metric(button, 30));
    button.setIconSize(QSize(16, 16));
}
void QtConfigureMainToolbar(QToolBar &toolbar) {
    toolbar.setStyle(new MainChromeStyle(toolbar));
    toolbar.setFont(QApplication::font());
    QtWorkspaceTheme::pane(toolbar);
    toolbar.setStyleSheet("QToolBar { border:0; border-bottom:1px solid " +
                          QtWorkspaceTheme::colors().stroke.name() + "; }");
}

void QtConfigureMainToolbarButton(QToolButton &button) {
    button.setStyle(button.parentWidget()->style());
    button.setFont(QApplication::font());
}

void QtConfigurePanelParentButton(QPushButton &button) {
    button.setIconSize(QSize(16, 16));
    button.setFixedSize(QtWorkspaceTheme::metric(button, 30), QtWorkspaceTheme::metric(button, 30));
    const auto c = QtWorkspaceTheme::colors();
    button.setStyleSheet(
        QStringLiteral("QPushButton { border:0; border-radius:4px; background:transparent; }"
                       "QPushButton:hover { background:%1; }"
                       "QPushButton:focus { border:1px solid %2; }")
            .arg(c.hover.name(), c.accent.name()));
    button.setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    button.setFont(QApplication::font());
#ifdef Q_OS_MACOS
    button.setAttribute(Qt::WA_LayoutUsesWidgetRect);
#endif
}
