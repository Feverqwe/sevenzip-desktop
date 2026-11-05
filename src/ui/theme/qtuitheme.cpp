#include "ui/theme/qtuitheme.h"
#include <QApplication>
#include <QStyleHints>
#include <QWidget>

namespace QtUiTheme {
namespace {
QColor blend(const QColor &foreground, const QColor &background, qreal amount) {
    return QColor::fromRgbF(foreground.redF() * amount + background.redF() * (1 - amount),
                            foreground.greenF() * amount + background.greenF() * (1 - amount),
                            foreground.blueF() * amount + background.blueF() * (1 - amount));
}
} // namespace

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

void surface(QWidget &widget, const QColor &background) {
    auto p = palette();
    p.setColor(QPalette::Window, background);
    widget.setPalette(p);
    widget.setAutoFillBackground(true);
}

} // namespace QtUiTheme
