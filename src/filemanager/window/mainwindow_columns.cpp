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
    if (sortColumn < 0 || projectedUi().table->isColumnHidden(sortColumn)) {
        const int fallback = panel.GetSortID() == kpidNoProperty ? 6 : 0;
        panel._sortID = fileTable->propertyId(fallback);
        panel.SetSortRawStatus();
    }
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
