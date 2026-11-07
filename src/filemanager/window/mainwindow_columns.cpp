#include "7zip/PropID.h"
#include "filemanager/qtfilemanagercommands.h"
#include "filemanager/window/mainwindow.h"
#include <QScopedValueRollback>

void MainWindow::configureColumns() {
    auto &panel = app_.GetFocusedPanel();
    const QScopedValueRollback<bool> configuring(configuringColumns_, true);
    const QSignalBlocker blocked(projectedUi().table->horizontalHeader());
    auto *fileTable = static_cast<FileTable *>(projectedUi().table);
    projectedUi().table->clearRows();
    fileTable->textFolder = panel.GetFolder();
    fileTable->configureSourceColumns();
    updateIconColumn();
    const int sortColumn = fileTable->columnForProperty(panel.GetSortID());
    if (sortColumn < 0 ||
        (!fileTable->sourceColumn(sortColumn) && panel.GetSortID() != kpidExtension)) {
        if (panel.GetSortID() == kpidNoProperty)
            panel._ascending = true;
        panel._sortID = fileTable->propertyId(0);
        panel.SetSortRawStatus();
    }
}

void MainWindow::applySortMode(bool enableSorting) {
    if (!projectedUi().table)
        return;
    auto &panel = app_.GetFocusedPanel();
    auto *view = static_cast<FileTable *>(projectedUi().table);
    int column = view->columnForProperty(panel.GetSortID());
    if (column < 0)
        column = 0;
    const Qt::SortOrder order = panel._ascending ? Qt::AscendingOrder : Qt::DescendingOrder;
    panel._sortID = view->propertyId(column);
    panel.SetSortRawStatus();
    if (enableSorting && !projectedUi().table->isSortingEnabled()) {
        // Enabling Qt sorting already sorts; configure the source order first.
        const QSignalBlocker blocker(projectedUi().table->horizontalHeader());
        projectedUi().table->horizontalHeader()->setSortIndicator(column, order);
        projectedUi().table->setSortingEnabled(true);
    } else
        projectedUi().table->sortItems(column, order);
    saveColumns();
}

void MainWindow::sortIndicatorChanged(unsigned index, int column, Qt::SortOrder order,
                                      bool headerClick) {
    const bool active = index == projectedPanel_;
    withPanelProjection(index, [this, index, column, order, active, headerClick] {
        auto *view = static_cast<FileTable *>(projectedUi().table);
        if (!headerClick && view->deferSourceColumnClick(
                                [window = QPointer<MainWindow>(this), index, column, order] {
                                    if (window)
                                        window->sortIndicatorChanged(index, column, order, true);
                                }))
            return;
        auto &panel = app_.GetFocusedPanel();
        {
            const QSignalBlocker blocked(projectedUi().table->horizontalHeader());
            if (headerClick) {
                panel.OnColumnClick(view->propertyId(column));
            } else {
                // Explicit Qt sort requests retain their requested direction.
                panel._sortID = view->propertyId(column);
                panel._ascending = order == Qt::AscendingOrder;
                panel.SetSortRawStatus();
            }
            // Qt's initial automatic sort precedes this signal consumer. Project
            // the authoritative source flag/order onto the clicked section.
            projectedUi().table->sortItems(column, panel._ascending ? Qt::AscendingOrder
                                                                    : Qt::DescendingOrder);
        }
        if (!configuringColumns_) {
            if (active && commands_)
                commands_->RefreshViewActions();
            saveColumns();
        }
    });
}

void MainWindow::updateIconColumn() {
    if (projectedUi().iconList)
        projectedUi().iconList->setModelColumn(
            qMax(0, static_cast<FileTable *>(projectedUi().table)->columnForVisibleProperty(0)));
}

void MainWindow::saveColumns() {
    if (configuringColumns_ || !projectedUi().table)
        return;
    auto &panel = app_.GetFocusedPanel();
    if (panel.GetColumns().IsEmpty())
        return;
    const auto *fileTable = static_cast<FileTable *>(projectedUi().table);
    panel.SaveListViewInfo(fileTable->sourceColumnLayout());
}

void MainWindow::redrawTimestampColumns() {
    if (!projectedUi().table || !app_.GetFocusedPanel().GetFolder())
        return;
    const auto *fileTable = static_cast<const FileTable *>(projectedUi().table);
    for (int column = 0; column < projectedUi().table->columnCount(); ++column) {
        const CPropColumn *prop = fileTable->sourceColumn(column);
        if (!prop || prop->Type != VT_FILETIME)
            continue;
        projectedUi().table->panelModel()->invalidateText(column);
    }
    projectedUi().table->viewport()->update();
}
