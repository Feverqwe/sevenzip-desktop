#pragma once
#include "ui/theme/qtuitheme.h"

class QWidget;
class QAbstractItemView;
class QStatusBar;

namespace QtWorkspaceTheme {
using QtUiTheme::Colors;
using QtUiTheme::colors;
using QtUiTheme::surface;
int metric(const QWidget &widget, int value);
void pane(QWidget &widget);
void fileView(QAbstractItemView &view);
void header(QWidget &header);
void statusBar(QStatusBar &status);
} // namespace QtWorkspaceTheme
