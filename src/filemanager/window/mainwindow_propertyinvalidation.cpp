#include "Windows/PropVariant.h"
#include "filemanager/window/mainwindow.h"

// Keep the numeric size role used by interaction in sync with source stats.
// Display text is requested from the source panel when Qt paints a cell.
void MainWindow::redrawSourceProperties() {
    auto &panel = app_.GetFocusedPanel();
    if (!projectedUi().table || !panel.GetFolder() || app_.QtOperations().closed())
        return;
    projectedUi().table->panelModel()->invalidateText();
    if (projectedUi().table->isSortingEnabled())
        projectedUi().table->panelModel()->sort(
            projectedUi().table->horizontalHeader()->sortIndicatorSection(),
            projectedUi().table->horizontalHeader()->sortIndicatorOrder());
    projectedUi().table->viewport()->update();
    refreshActions();
}
