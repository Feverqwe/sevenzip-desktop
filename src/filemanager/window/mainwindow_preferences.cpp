#include "7zip/UI/FileManager/resource.h"
#include "filemanager/MyLoadMenu.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/panels/PanelSort.h"
#include "filemanager/panels/qtpanelcontextmenu.h"
#include "filemanager/providers/qtlocationfolder.h"
#include "filemanager/qtfilemanagercommands.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtmainwindowchrome.h"
#include "localization/qtlanguageservices.h"
#include "ui/theme/qtcommandicons.h"
#include <QSignalBlocker>
#include <QToolButton>

namespace {
QString SavedSourcePath(QString path) {
    // Earlier Qt settings used a display separator after the real archive.
    // Convert only that boundary; spaces inside source path components survive.
    const QString separator = QStringLiteral(" / ");
    for (int pos = path.indexOf(separator); pos >= 0;
         pos = path.indexOf(separator, pos + separator.size()))
        if (QFileInfo(path.left(pos)).isFile()) {
            path.replace(pos, separator.size(), QStringLiteral("/"));
            break;
        }
    return path;
}
} // namespace

void MainWindow::showItemContextMenu(unsigned owner, QAbstractItemView *view, const QPoint &pos) {
    withPanelProjection(owner, [this, view, pos] {
        const bool keyboard = pos == QPoint(-1, -1);
        if (!keyboard) {
            const QModelIndex index = view->indexAt(pos);
            if (index.isValid() &&
                !view->selectionModel()->isRowSelected(index.row(), QModelIndex())) {
                view->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect |
                                                          QItemSelectionModel::Rows);
                view->setCurrentIndex(index);
            }
        }
        refreshActions();
        const QPoint point = keyboard ? pos : view->viewport()->mapToGlobal(pos);
        QtPanelContextMenu menu(*this, *view);
        app_.GetFocusedPanel().OnContextMenu(view, point.x(), point.y(), menu);
    });
}

void MainWindow::applySortMode(bool enableSorting) {
    if (!projectedUi().table)
        return;
    auto &panel = app_.GetFocusedPanel();
    auto *view = static_cast<FileTable *>(projectedUi().table);
    int column = view->columnForProperty(panel.GetSortID());
    if (column < 0)
        column = panel.GetSortID() == kpidNoProperty ? 6 : 0;
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

void MainWindow::sortIndicatorChanged(unsigned index, int column, Qt::SortOrder order) {
    const bool active = index == projectedPanel_;
    withPanelProjection(index, [this, index, column, order, active] {
        auto *view = static_cast<FileTable *>(projectedUi().table);
        if (view->deferSourceColumnClick(
                [window = QPointer<MainWindow>(this), index, column, order] {
                    if (window)
                        window->sortIndicatorChanged(index, column, order);
                }))
            return;
        auto &panel = app_.GetFocusedPanel();
        {
            const QSignalBlocker blocked(projectedUi().table->horizontalHeader());
            if (view->takeSourceColumnClick()) {
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

QIcon MainWindow::toolbarIcon(int kind) {
    static const QtCommandIcon symbols[] = {
        QtCommandIcon::Add,  QtCommandIcon::Extract, QtCommandIcon::Test, QtCommandIcon::Copy,
        QtCommandIcon::Move, QtCommandIcon::Delete,  QtCommandIcon::Info};
    if (kind < 0 || kind >= int(std::size(symbols)))
        return {};
    return QtCommandSymbol(symbols[kind]);
}

QString MainWindow::toolbarSettingsPath() { return FmSettingsPath(); }

void MainWindow::recordVisitedPath(const QString &path) {
    app_.AppState.FolderHistory.AddString(ToUString(path));
    app_.AppState.FolderHistory.Save(toolbarSettingsPath());
}

void MainWindow::showFoldersHistory() {
    if (projectedUi().state == State::Loading || projectedUi().state == State::Operating)
        return;
    const unsigned owner = app_.GetFocusedPanelIndex();
    const QString selected =
        CPanel::FoldersHistory(app_.AppState.FolderHistory, toolbarSettingsPath(), this);
    if (!selected.isNull()) {
        activatePanelUi(owner);
        navigateTo(SavedSourcePath(selected), true);
    } else
        refreshActions();
}
void MainWindow::refreshBookmarks() { MyLoadBookmarks(openBookmarks_, saveBookmarks_, app_); }

void MainWindow::saveBookmark(int index) {
    if (index < 0 || index >= 10 || projectedUi().state == State::Loading ||
        projectedUi().state == State::Operating)
        return;
    auto &panel = app_.GetFocusedPanel();
    panel.LoadFullPath();
    CPanel::SetBookmark(app_.AppState.FastFolders, (unsigned)index, panel.GetCurrentFolderPrefix());
    app_.AppState.FastFolders.Save(toolbarSettingsPath());
    refreshActions();
}

void MainWindow::openBookmark(int index) {
    if (index < 0 || index >= 10 || projectedUi().state == State::Loading ||
        projectedUi().state == State::Operating)
        return;
    const QString path =
        ToQString(CPanel::OpenBookmark(app_.AppState.FastFolders, (unsigned)index));
    navigateTo(SavedSourcePath(path), true);
}

void MainWindow::updateToolbar() {
    auto *toolbar = findChild<QToolBar *>("archiveToolbar");
    const bool archive = app_.ShowArchiveToolbar;
    const bool standard = app_.ShowStandardToolbar;
    const QSignalBlocker archiveSignals(archiveToolbarOption_);
    const QSignalBlocker standardSignals(standardToolbarOption_);
    const QSignalBlocker largeSignals(largeButtonsOption_);
    const QSignalBlocker textSignals(buttonTextOption_);
    archiveToolbarOption_->setChecked(archive);
    standardToolbarOption_->setChecked(standard);
    largeButtonsOption_->setChecked(app_.LargeButtons);
    buttonTextOption_->setChecked(app_.ShowButtonsLables);
    for (QAction *action : {toolbarAdd_, extract_, test_})
        toolbar->widgetForAction(action)->setVisible(archive);
    for (QAction *action : {copy_, move_, remove_, infoAction_})
        toolbar->widgetForAction(action)->setVisible(standard);
    toolbar->widgetForAction(toolbarGroupSeparator_)->setVisible(archive && standard);
    toolbar->widgetForAction(toolbarGroupSeparator_)->setStyle(toolbar->style());
    toolbar->setVisible(archive || standard);
    toolbar->setIconSize(largeButtonsOption_->isChecked() ? QSize(24, 24) : QSize(16, 16));
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    for (QAction *action : {toolbarAdd_, extract_, test_, copy_, move_, remove_, infoAction_}) {
        if (auto *button = qobject_cast<QToolButton *>(toolbar->widgetForAction(action))) {
            QtConfigureMainToolbarButton(*button);
            const bool primary = action == toolbarAdd_ || action == extract_;
            button->setToolButtonStyle(primary && app_.ShowButtonsLables
                                           ? Qt::ToolButtonTextBesideIcon
                                           : Qt::ToolButtonIconOnly);
            button->setAutoRaise(true);
            button->setFocusPolicy(Qt::StrongFocus);
        }
    }
    refreshToolbarTexts();
}

void MainWindow::showSettings() {
    SettingsDialog dialog(toolbarSettingsPath(), this, folderManager_);
    connect(&dialog, &SettingsDialog::Applied, this, &MainWindow::applyFileListSettings);
    dialog.exec();
}

void MainWindow::applyFileListSettings() {
    app_.SetListSettings();
    app_.RefreshAllPanels();
    refreshActions();
}

void MainWindow::setViewMode(int mode) {
    if (mode < 0 || mode > 3 || !projectedUi().viewStack)
        return;
    auto &panel = app_.GetFocusedPanel();
    auto *stack = projectedUi().viewStack;
    auto *address = projectedUi().pathCombo;
    const QWidget *focused = QApplication::focusWidget();
    const QWidget *previousView = stack->currentWidget();
    const bool restoreFocus =
        previousView && focused && (focused == previousView || previousView->isAncestorOf(focused));
    viewActions_[mode]->setChecked(true);
    if (mode != 3) {
        static_cast<FileIconList *>(projectedUi().iconList)->SetListViewMode(mode);
        // Qt's Static movement resets dragEnabled. The shared file-list policy
        // still permits exporting entries through the native drag controller.
        projectedUi().iconList->setDragEnabled(projectedUi().table->dragEnabled());
    }
    // Upstream changes the style of the same list control. Qt replaces the
    // visible view, which can synchronously focus another panel while hiding
    // the old widget. Bind the captured source owner and retain its list focus.
    stack->setCurrentIndex(mode == 3 ? 0 : 1);
    panel.AttachPresentation(stack->currentWidget(), address);
    if (restoreFocus)
        stack->currentWidget()->setFocus();
    CListMode modes;
    for (unsigned i = 0; i < kNumPanelsMax; ++i)
        modes.Panels[i] = app_.Panels[i].GetListViewMode();
    modes.Save(toolbarSettingsPath());
}
