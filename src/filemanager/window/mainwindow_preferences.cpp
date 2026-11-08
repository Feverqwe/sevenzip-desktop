#include "7zip/UI/FileManager/resource.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/panels/qtpanelcontextmenu.h"
#include "filemanager/providers/qtlocationfolder.h"
#include "filemanager/qtfilemanagercommands.h"
#include "filemanager/window/mainwindow.h"
#include "localization/qtlanguageservices.h"
#include <QScopedValueRollback>
#include <QSignalBlocker>

void MainWindow::showItemContextMenu(unsigned owner, QAbstractItemView *view, const QPoint &pos) {
    withPanelProjection(owner, [this, view, pos] {
        const bool keyboard = pos == QPoint(-1, -1);
        if (!keyboard) {
            const QModelIndex index = view->indexAt(pos);
            if (index.isValid()) {
                auto *selection = view->selectionModel();
                if (!selection->isRowSelected(index.row(), QModelIndex()))
                    selection->select(index, QItemSelectionModel::ClearAndSelect |
                                                 QItemSelectionModel::Rows);
                selection->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
            }
        }
        refreshActions();
        const QPoint point = keyboard ? pos : view->viewport()->mapToGlobal(pos);
        QtPanelContextMenu menu(*this, *view);
        app_.GetFocusedPanel().OnContextMenu(view, point.x(), point.y(), menu);
    });
}

QString MainWindow::toolbarSettingsPath() { return FmSettingsPath(); }

void MainWindow::showSettings() {
    SettingsDialog dialog(toolbarSettingsPath(), this, folderManager_);
    connect(&dialog, &SettingsDialog::Applied, this, &MainWindow::applyFileListSettings);
    dialog.exec();
    refreshActions();
}

void MainWindow::applyFileListSettings() {
    app_.SetListSettings();
    app_.RefreshAllPanels(true);
    refreshActions();
}

void MainWindow::setViewMode(int mode) {
    if ((mode != 0 && mode != 3) || !projectedUi().viewStack)
        return;
    const QScopedValueRollback<unsigned> projection(panelProjectionDepth_,
                                                    panelProjectionDepth_ + 1);
    auto &panel = app_.GetFocusedPanel();
    auto *stack = projectedUi().viewStack;
    auto *address = projectedUi().pathCombo;
    auto *icons = static_cast<FileIconList *>(projectedUi().iconList);
    const QWidget *focused = QApplication::focusWidget();
    const QWidget *previousView = stack->currentWidget();
    const bool restoreFocus =
        previousView && focused && (focused == previousView || previousView->isAncestorOf(focused));
    if (mode != 3)
        icons->ConfigureLargeIcons();
    // Upstream changes the style of the same list control. Qt replaces the
    // visible view, which can synchronously focus another panel while hiding
    // the old widget. Bind the captured source owner and retain its list focus.
    stack->setCurrentIndex(mode == 3 ? 0 : 1);
    auto *view = static_cast<QAbstractItemView *>(stack->currentWidget());
    panel.AttachPresentation(view, address);
    if (view != previousView) {
        const QModelIndex current = view->currentIndex();
        if (current.isValid()) {
            // The icon view can only navigate to indexes in its displayed column.
            const QModelIndex visible =
                view == icons ? current.siblingAtColumn(icons->modelColumn()) : current;
            if (visible != current)
                view->selectionModel()->setCurrentIndex(visible, QItemSelectionModel::NoUpdate);
            view->scrollTo(visible);
        }
    }
    if (restoreFocus)
        view->setFocus();
    CListMode modes;
    for (unsigned i = 0; i < kNumPanelsMax; ++i)
        modes.Panels[i] = app_.Panels[i].GetListViewMode();
    modes.Save(toolbarSettingsPath());
    if (commands_)
        commands_->RefreshViewActions();
}
