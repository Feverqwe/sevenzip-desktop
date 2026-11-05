#pragma once
#include <QColor>

class QWidget;
class QAbstractItemView;
class QStatusBar;

namespace QtWorkspaceTheme {
struct Colors {
    QColor surface, window, secondary, text, subtle, stroke, accent, selection, hover;
};
Colors colors();
int metric(const QWidget &widget, int value);
void surface(QWidget &widget, const QColor &background);
void pane(QWidget &widget);
void fileView(QAbstractItemView &view);
void header(QWidget &header);
void statusBar(QStatusBar &status);
} // namespace QtWorkspaceTheme
