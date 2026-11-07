#pragma once
#include <QItemSelectionModel>
#include <QPoint>
#include <QRect>
#include <functional>

class QAbstractItemView;
class QMouseEvent;

struct QtFileSelectionGeometry {
    std::function<void(const QRect &, QItemSelectionModel::SelectionFlags)> select;
    std::function<bool(QPoint)> hitItem;
    std::function<QPoint()> offset;
    std::function<void(QMouseEvent *, QMouseEvent *)> click;
};

void QtInstallFileSelectionGesture(QAbstractItemView &view, QtFileSelectionGeometry geometry,
                                   std::function<bool()> activate);
