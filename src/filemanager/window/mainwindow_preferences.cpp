#include "7zip/UI/FileManager/resource.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/panels/qtpanelcontextmenu.h"
#include "filemanager/providers/qtlocationfolder.h"
#include "filemanager/qtfilemanagercommands.h"
#include "filemanager/rename/qtrenamecontroller.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtwindowworkspace.h"
#include "filemanager/window/qtworkspaceapplication.h"
#include "localization/qtlanguageservices.h"
#include <QScopedValueRollback>
#include <QScrollBar>
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
    const auto apply = [this] {
        const auto previous = fileListSettings_;
        applyFileListSettings();
        if (previous.ShowDots != fileListSettings_.ShowDots ||
            previous.ShowHiddenFolders != fileListSettings_.ShowHiddenFolders)
            if (auto *application = QtWorkspaceApplication::Find())
                application->FileListSettingsChanged(*this);
    };
    connect(&dialog, &SettingsDialog::Applied, this, [this, apply] {
        if (scanningPanelMask() || renameController_->Pending())
            workspace_->WhenReady(apply, {}, false,
                                  [this] { return !renameController_->Pending(); });
        else
            apply();
    });
    dialog.exec();
    refreshActions();
}

void MainWindow::applyFileListSettings() {
    const auto previous = fileListSettings_;
    app_.SetListSettings();
    if (previous.ShowDots != fileListSettings_.ShowDots)
        app_.RefreshAllPanels(true);
    else if (previous.ShowHiddenFolders != fileListSettings_.ShowHiddenFolders)
        for (auto &panel : app_.Panels)
            if (panel.PanelCreated && panel.IsFSFolder())
                panel.OnReload(true);
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
    auto *table = projectedUi().table;
    auto *horizontal = table->horizontalScrollBar();
    const int tablePosition = horizontal->value();
    const QWidget *focused = QApplication::focusWidget();
    const QWidget *previousView = stack->currentWidget();
    if (mode != 3 && previousView == table) {
        const int column = table->currentIndex().column();
        if (column >= 0)
            projectedUi().tableCursorColumn.emplace(table->propertyId(column),
                                                    table->isRawProperty(column));
        else
            projectedUi().tableCursorColumn.reset();
    }
    const bool restoreFocus =
        previousView && focused && (focused == previousView || previousView->isAncestorOf(focused));
    const bool restoreAfterScan =
        focused == projectedUi().container && (scanningPanelMask() & (1u << projectedPanel_));
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
            int column = view == icons ? icons->modelColumn() : current.column();
            if (view == table && projectedUi().tableCursorColumn) {
                const auto [property, raw] = *projectedUi().tableCursorColumn;
                for (int candidate = 0; candidate < table->model()->columnCount(); ++candidate)
                    if (!table->isColumnHidden(candidate) &&
                        table->propertyId(candidate) == property &&
                        table->isRawProperty(candidate) == raw) {
                        column = candidate;
                        break;
                    }
            }
            const QModelIndex visible = current.siblingAtColumn(column);
            if (visible != current)
                view->selectionModel()->setCurrentIndex(visible, QItemSelectionModel::NoUpdate);
            view->scrollTo(visible);
        }
    }
    if (restoreFocus)
        view->setFocus();
    else if (restoreAfterScan)
        workspace_->RequestListFocus(projectedPanel_);
    if (mode == 3 && view != previousView)
        horizontal->setValue(tablePosition);
    CListMode modes;
    for (unsigned i = 0; i < kNumPanelsMax; ++i)
        modes.Panels[i] = app_.Panels[i].GetListViewMode();
    modes.Save(toolbarSettingsPath());
    if (commands_)
        commands_->RefreshViewActions();
}
