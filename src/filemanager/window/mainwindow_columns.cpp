#include "filemanager/window/mainwindow.h"

void MainWindow::configureColumns(bool initialized) {
    auto &panel = app_.GetFocusedPanel();
    if (!initialized && panel.InitColumns() != S_OK)
        return;
    configuringColumns_ = true;
    const QSignalBlocker blocked(table_->horizontalHeader());
    auto *fileTable = static_cast<FileTable *>(table_);
    const CPropColumns &columns = panel.GetColumns();
    table_->clearRows();
    fileTable->textFolder = panel.GetFolder();
    fileTable->configureSourceColumns();
    FOR_VECTOR(i, columns) {
        const CPropColumn &prop = columns[i];
        const int column = fileTable->columnForCatalogProperty(i);
        table_->setColumnWidth(column, qMax(32, int(prop.Width)));
        table_->setColumnHidden(column, !prop.IsVisible);
    }
    for (int column : {1, 5, 6})
        table_->setColumnHidden(column, true);
    for (int column : {0, 2, 3, 4}) {
        if (!fileTable->sourceColumn(column))
            table_->setColumnHidden(column, true);
    }
    const CPropColumns &visible = panel.GetVisibleColumns();
    updateIconColumn();
    CIntArr columnOrder;
    const bool ordered = panel.GetColumnOrder(columnOrder);
    FOR_VECTOR(i, visible) {
        const int column =
            fileTable->columnForVisibleProperty(ordered ? unsigned(columnOrder[i]) : i);
        if (column >= 0)
            table_->horizontalHeader()->moveSection(table_->horizontalHeader()->visualIndex(column),
                                                    int(i));
    }
    const int sortColumn = fileTable->columnForProperty(panel.GetSortID());
    if (sortColumn < 0 || table_->isColumnHidden(sortColumn)) {
        const int fallback = panel.GetSortID() == kpidNoProperty ? 6 : 0;
        panel._sortID = fileTable->propertyId(fallback);
        panel.SetSortRawStatus();
    }
    configuringColumns_ = false;
}

void MainWindow::updateIconColumn() {
    if (iconList_)
        iconList_->setModelColumn(
            qMax(0, static_cast<FileTable *>(table_)->columnForVisibleProperty(0)));
}

void MainWindow::saveColumns() {
    if (configuringColumns_ || !table_)
        return;
    auto &panel = app_.GetFocusedPanel();
    if (panel.GetColumns().IsEmpty())
        return;
    const auto *fileTable = static_cast<FileTable *>(table_);
    CPropColumns visible;
    const CPropColumns &columns = panel.GetVisibleColumns();
    FOR_VECTOR(i, columns) {
        const int column = fileTable->columnForVisibleProperty(i);
        if (column < 0 || table_->isColumnHidden(column))
            continue;
        CPropColumn prop = columns[i];
        prop.IsVisible = true;
        prop.Order = 0;
        for (int visual = 0; visual < table_->horizontalHeader()->visualIndex(column); ++visual)
            if (!table_->isColumnHidden(table_->horizontalHeader()->logicalIndex(visual)))
                ++prop.Order;
        prop.Width = UInt32(table_->horizontalHeader()->sectionSize(column));
        visible.Add(prop);
    }
    panel.SaveListViewInfo(visible);
}

void MainWindow::showColumnsMenu(const QPoint &pos) {
    auto &panel = app_.GetFocusedPanel();
    const CPropColumns &columns = panel.GetColumns();
    auto *fileTable = static_cast<FileTable *>(table_);
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
                const QSignalBlocker blocker(table_->horizontalHeader());
                auto *view = static_cast<FileTable *>(table_);
                if (visible)
                    view->appendSourceColumn(index);
                else if (removed >= 0)
                    view->removeSourceColumn(unsigned(removed));
            }
            updateIconColumn();
            const int column = static_cast<FileTable *>(table_)->columnForProperty(source._sortID);
            const Qt::SortOrder order =
                source._ascending ? Qt::AscendingOrder : Qt::DescendingOrder;
            {
                const QSignalBlocker blocker(table_->horizontalHeader());
                table_->horizontalHeader()->setSortIndicator(column, order);
            }
            saveColumns();
        });
    }
    menu.exec(table_->horizontalHeader()->mapToGlobal(pos));
}

void MainWindow::redrawTimestampColumns() {
    if (!table_ || !app_.GetFocusedPanel().GetFolder())
        return;
    const auto *fileTable = static_cast<const FileTable *>(table_);
    for (int column = 0; column < table_->columnCount(); ++column) {
        const CPropColumn *prop = fileTable->sourceColumn(column);
        if (!prop || prop->Type != VT_FILETIME)
            continue;
        table_->panelModel()->invalidateText(column);
    }
    table_->viewport()->update();
}
