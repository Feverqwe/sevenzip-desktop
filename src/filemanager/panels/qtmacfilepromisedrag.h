#pragma once
#include <QList>
#include <QString>
#include <functional>
#include <optional>

class QMimeData;
class QWidget;

struct QtMacPromisedItem {
    QString Path;
    bool Directory;
};

void QtRegisterMacFilePromiseDrag();
void *QtMacFilePromiseDragSource(const QMimeData &mime);
std::optional<bool> QtRunMacFilePromiseDrag(QWidget &widget, const QList<QtMacPromisedItem> &items,
                                            void *source, const std::function<bool()> &prepare,
                                            const std::function<void()> &cleanup);
