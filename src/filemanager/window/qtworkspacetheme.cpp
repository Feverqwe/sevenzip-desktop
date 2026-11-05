#include "filemanager/window/qtworkspacetheme.h"
#include <QAbstractItemView>
#include <QApplication>
#include <QFontMetrics>
#include <QHeaderView>
#include <QLayout>
#include <QPalette>
#include <QStatusBar>
#include <QStyleHints>

namespace QtWorkspaceTheme {
namespace {
QColor blend(const QColor &foreground, const QColor &background, qreal amount) {
    return QColor::fromRgbF(foreground.redF() * amount + background.redF() * (1 - amount),
                            foreground.greenF() * amount + background.greenF() * (1 - amount),
                            foreground.blueF() * amount + background.blueF() * (1 - amount));
}
QPalette palette() {
    const auto c = colors();
    QPalette p = QApplication::palette();
    for (const auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        p.setColor(group, QPalette::Window, c.secondary);
        p.setColor(group, QPalette::Base, c.surface);
        p.setColor(group, QPalette::AlternateBase, c.secondary);
        p.setColor(group, QPalette::Button, c.secondary);
        p.setColor(group, QPalette::Text, group == QPalette::Disabled ? c.subtle : c.text);
        p.setColor(group, QPalette::WindowText, group == QPalette::Disabled ? c.subtle : c.text);
        p.setColor(group, QPalette::ButtonText, group == QPalette::Disabled ? c.subtle : c.text);
        p.setColor(group, QPalette::Highlight, c.selection);
        p.setColor(group, QPalette::HighlightedText, c.text);
        p.setColor(group, QPalette::Mid, c.stroke);
    }
    return p;
}
} // namespace

Colors colors() {
    const bool dark = QApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark ||
                      (QApplication::styleHints()->colorScheme() == Qt::ColorScheme::Unknown &&
                       QApplication::palette().color(QPalette::Window).lightness() < 128);
    Colors c = dark ? Colors{QColor("#252527"),
                             QColor("#2c2c2e"),
                             QColor("#202022"),
                             QColor("#f2f2f5"),
                             QColor("#b5b5bd"),
                             QColor("#48484d"),
                             QColor("#0a64d7"),
                             {},
                             {}}
                    : Colors{QColor("#ffffff"),
                             QColor("#f5f5f7"),
                             QColor("#e9e9ec"),
                             QColor("#1d1d1f"),
                             QColor("#626267"),
                             QColor("#c9c9cf"),
                             QColor("#0068d9"),
                             {},
                             {}};
    c.selection = blend(c.accent, c.surface, .18);
    c.hover = blend(c.text, c.window, .07);
    return c;
}

int metric(const QWidget &widget, int value) {
    return qRound(value * qMax(1.0, QFontMetrics(widget.font()).height() / 16.0));
}

void surface(QWidget &widget, const QColor &background) {
    auto p = palette();
    p.setColor(QPalette::Window, background);
    widget.setPalette(p);
    widget.setAutoFillBackground(true);
}

void pane(QWidget &widget) { surface(widget, colors().secondary); }

void fileView(QAbstractItemView &view) {
    view.setPalette(palette());
    view.setFrameShape(QFrame::NoFrame);
    view.viewport()->setAutoFillBackground(true);
}

void header(QWidget &header) {
    const auto c = colors();
    header.setStyleSheet(
        QStringLiteral("QHeaderView { background:%1; }"
                       "QHeaderView::section { background:%1; color:%2; border:0;"
                       "border-bottom:1px solid %3; padding:0 12px; font-weight:400; }")
            .arg(c.secondary.name(), c.subtle.name(), c.stroke.name()));
    if (auto *view = qobject_cast<QHeaderView *>(&header)) {
        view->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        view->setFixedHeight(metric(header, 28));
    }
}
void statusBar(QStatusBar &status) {
    const auto c = colors();
    pane(status);
    auto p = status.palette();
    p.setColor(QPalette::WindowText, c.subtle);
    status.setPalette(p);
    status.setMinimumHeight(metric(status, 32));
    status.layout()->setContentsMargins(metric(status, 16), metric(status, 8), metric(status, 16),
                                        metric(status, 8));
    status.setStyleSheet(
        QStringLiteral("QStatusBar { border-top:1px solid %1; color:%2; }"
                       "QStatusBar::item { border:0; } QStatusBar QLabel { color:%2; }")
            .arg(c.stroke.name(), c.subtle.name()));
}
} // namespace QtWorkspaceTheme
