#include "filemanager/window/mainwindow.h"

void MainWindow::configureColumns(bool initialized) {
    auto &panel = app_.GetFocusedPanel();
    if (!initialized && panel.InitColumns() != S_OK)
        return;
    configuringColumns_ = true;
    const QSignalBlocker blocked(projectedUi().table->horizontalHeader());
    auto *fileTable = static_cast<FileTable *>(projectedUi().table);
    const CPropColumns &columns = panel.GetColumns();
    projectedUi().table->clearRows();
    fileTable->textFolder = panel.GetFolder();
    fileTable->configureSourceColumns();
    FOR_VECTOR(i, columns) {
        const CPropColumn &prop = columns[i];
        const int column = fileTable->columnForCatalogProperty(i);
        projectedUi().table->setColumnWidth(column, qMax(32, int(prop.Width)));
        projectedUi().table->setColumnHidden(column, !prop.IsVisible);
    }
    for (int column : {1, 5, 6})
        projectedUi().table->setColumnHidden(column, true);
    for (int column : {0, 2, 3, 4}) {
        if (!fileTable->sourceColumn(column))
            projectedUi().table->setColumnHidden(column, true);
    }
    const CPropColumns &visible = panel.GetVisibleColumns();
    updateIconColumn();
    CIntArr columnOrder;
    const bool ordered = panel.GetColumnOrder(columnOrder);
    FOR_VECTOR(i, visible) {
        const int column =
            fileTable->columnForVisibleProperty(ordered ? unsigned(columnOrder[i]) : i);
        if (column >= 0)
            projectedUi().table->horizontalHeader()->moveSection(
                projectedUi().table->horizontalHeader()->visualIndex(column), int(i));
    }
    const int sortColumn = fileTable->columnForProperty(panel.GetSortID());
    if (sortColumn < 0 || projectedUi().table->isColumnHidden(sortColumn)) {
        const int fallback = panel.GetSortID() == kpidNoProperty ? 6 : 0;
        panel._sortID = fileTable->propertyId(fallback);
        panel.SetSortRawStatus();
    }
    configuringColumns_ = false;
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
    CPropColumns visible;
    const CPropColumns &columns = panel.GetVisibleColumns();
    FOR_VECTOR(i, columns) {
        const int column = fileTable->columnForVisibleProperty(i);
        if (column < 0 || projectedUi().table->isColumnHidden(column))
            continue;
        CPropColumn prop = columns[i];
        prop.IsVisible = true;
        prop.Order = 0;
        for (int visual = 0; visual < projectedUi().table->horizontalHeader()->visualIndex(column);
             ++visual)
            if (!projectedUi().table->isColumnHidden(
                    projectedUi().table->horizontalHeader()->logicalIndex(visual)))
                ++prop.Order;
        prop.Width = UInt32(projectedUi().table->horizontalHeader()->sectionSize(column));
        visible.Add(prop);
    }
    panel.SaveListViewInfo(visible);
}

void MainWindow::showColumnsMenu(const QPoint &pos) {
    auto &panel = app_.GetFocusedPanel();
    const CPropColumns &columns = panel.GetColumns();
    auto *fileTable = static_cast<FileTable *>(projectedUi().table);
    QMenu menu(this);
    FOR_VECTOR(i, columns) {
        const CPropColumn &prop = columns[i];
        const int column = fileTable->columnForCatalogProperty(i);
        if (column < 0)
            continue;
        QAction *option = menu.addAction(ToQString(prop.Name));
        option->setCheckable(true);
        option->setChecked(prop.IsVisible);
        option->setEnabled(i != 0);
        connect(option, &QAction::toggled, this, [this, index = i](bool) {
            auto &source = app_.GetFocusedPanel();
            const int removed =
                source.GetColumns()[index].IsVisible
                    ? source.GetVisibleColumns().FindItem_for_PropID(source.GetColumns()[index].ID)
                    : -1;
            source.ShowColumnsContextMenu(index);
            const bool visible = source.GetColumns()[index].IsVisible;
            {
                const QSignalBlocker blocker(projectedUi().table->horizontalHeader());
                auto *view = static_cast<FileTable *>(projectedUi().table);
                if (visible)
                    view->appendSourceColumn(index);
                else if (removed >= 0)
                    view->removeSourceColumn(unsigned(removed));
            }
            updateIconColumn();
            const int column =
                static_cast<FileTable *>(projectedUi().table)->columnForProperty(source._sortID);
            const Qt::SortOrder order =
                source._ascending ? Qt::AscendingOrder : Qt::DescendingOrder;
            {
                const QSignalBlocker blocker(projectedUi().table->horizontalHeader());
                projectedUi().table->horizontalHeader()->setSortIndicator(column, order);
            }
            saveColumns();
        });
    }
    menu.exec(projectedUi().table->horizontalHeader()->mapToGlobal(pos));
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
