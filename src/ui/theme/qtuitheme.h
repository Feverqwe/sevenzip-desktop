#pragma once
#include <QColor>
#include <QPalette>

class QWidget;

namespace QtUiTheme {
struct Colors {
    QColor surface, window, secondary, text, subtle, stroke, accent, selection, hover;
};
Colors colors();
QPalette palette();
void surface(QWidget &widget, const QColor &background);
} // namespace QtUiTheme
