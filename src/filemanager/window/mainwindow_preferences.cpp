#include "7zip/UI/FileManager/resource.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/panels/qtpanelcontextmenu.h"
#include "filemanager/providers/qtlocationfolder.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtmainwindowchrome.h"
#include "localization/qtlanguageservices.h"
#include "ui/theme/qtcommandicons.h"
#include <QSignalBlocker>
#include <QToolButton>

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

QIcon MainWindow::toolbarIcon(int kind) {
    static const QtCommandIcon symbols[] = {
        QtCommandIcon::Add,  QtCommandIcon::Extract, QtCommandIcon::Test, QtCommandIcon::Copy,
        QtCommandIcon::Move, QtCommandIcon::Delete,  QtCommandIcon::Info};
    if (kind < 0 || kind >= int(std::size(symbols)))
        return {};
    return QtCommandSymbol(symbols[kind]);
}

QString MainWindow::toolbarSettingsPath() { return FmSettingsPath(); }

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
    if (mode != 3)
        static_cast<FileIconList *>(projectedUi().iconList)->SetListViewMode(mode);
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
