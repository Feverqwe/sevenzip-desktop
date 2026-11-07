// Application presentation colors derived from the current native palette.
#pragma once
#include <QPalette>

class QWidget;
namespace QtSemanticColors {
QColor readable(const QColor &preferred, const QColor &background);
QColor surface(const QPalette &palette, QPalette::ColorRole background = QPalette::Base);
QColor border(const QPalette &palette, QPalette::ColorRole background = QPalette::Base);
QColor error(const QColor &background);
QColor success(const QColor &background);
void styleSurface(QWidget &widget, int radius);
void styleLink(QWidget &widget);
} // namespace QtSemanticColors
