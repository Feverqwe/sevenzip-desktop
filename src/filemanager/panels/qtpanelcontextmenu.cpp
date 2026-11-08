#include "filemanager/panels/qtpanelcontextmenu.h"
#include "7zip/UI/FileManager/resource.h"
#include "filemanager/qtfilemanagercommands.h"
#include "filemanager/qtfilemanagershellmenu.h"
#include "filemanager/qtfilemenuprojection.h"
#include "filemanager/qtprogramfilemenu.h"
#include "filemanager/views/filetable.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtfileclipboard.h"
#include "localization/qtlangcontrols.h"
#include "localization/qtlanguageservices.h"
#include "ui/dialogs/qtdialogscreenservices.h"
#include <QClipboard>

struct QtPanelContextMenu::Impl {
    MainWindow &window;
    QAbstractItemView &view;
    QtAppOperationState::CallScope call;
    QMenu popup;
    QtDialogScreenServices screen;
    QtFileMenuProjection projection;
    QtFileManagerShellMenu shell;
    QHash<unsigned, QAction *> actions;
    unsigned pluginID = 0;
    Impl(MainWindow &owner, QAbstractItemView &control)
        : window(owner), view(control), call(owner.app_.QtOperations()), popup(&owner),
          screen(&owner), shell(owner, popup, [this](unsigned id) { pluginID = id; }) {
        popup.setObjectName("panelContextMenu");
    }
};

QtPanelContextMenu::QtPanelContextMenu(MainWindow &window, QAbstractItemView &view)
    : impl_(std::make_unique<Impl>(window, view)) {}
QtPanelContextMenu::~QtPanelContextMenu() {
    auto &window = impl_->window;
    // Native popup focus events can project disabled actions while source
    // notification guards are held. Recompute after popup and CallScope end.
    impl_.reset();
    window.refreshActions();
}
bool QtPanelContextMenu::IsHeader(QWidget *handle) const {
    auto *table = qobject_cast<QTableView *>(&impl_->view);
    return table && handle == table->horizontalHeader();
}
bool QtPanelContextMenu::IsList(QWidget *handle) const { return handle == &impl_->view; }
void QtPanelContextMenu::ShowColumnsContextMenu(int x, int y) {
    auto &window = impl_->window;
    auto *table = qobject_cast<FileTable *>(&impl_->view);
    auto &panel = *table->sourcePanel;
    // Some Qt platforms restore peer focus before QAction::toggled. Keep
    // the source column mutation on the header's captured owner until return.
    CPanel::CDisableNotify left(window.app_.Panels[0]);
    CPanel::CDisableNotify right(window.app_.Panels[1]);
    const CPropColumns &columns = panel.GetColumns();
    QMenu menu(&window);
    FOR_VECTOR(i, columns) {
        const CPropColumn &prop = columns[i];
        const int column = table->columnForCatalogProperty(i);
        if (column < 0)
            continue;
        QAction *option = menu.addAction(ToQString(prop.Name));
        option->setCheckable(true);
        option->setChecked(prop.IsVisible);
        option->setEnabled(i != 0);
        QObject::connect(
            option, &QAction::toggled, &window, [&window, table, &panel, index = i](bool) {
                const int removed = panel.GetColumns()[index].IsVisible
                                        ? panel.GetVisibleColumns().FindItem_for_PropID(
                                              panel.GetColumns()[index].ID)
                                        : -1;
                panel.ShowColumnsContextMenu(index);
                const bool visible = panel.GetColumns()[index].IsVisible;
                {
                    const QSignalBlocker blocker(table->horizontalHeader());
                    if (visible)
                        table->appendSourceColumn(index);
                    else if (removed >= 0)
                        table->removeSourceColumn(unsigned(removed));
                }
                table->sourceColumnVisibilityChanged();
                window.updateIconColumn();
                const int column = table->columnForProperty(panel._sortID);
                const Qt::SortOrder order =
                    panel._ascending ? Qt::AscendingOrder : Qt::DescendingOrder;
                {
                    const QSignalBlocker blocker(table->horizontalHeader());
                    table->horizontalHeader()->setSortIndicator(column, order);
                }
                window.saveColumns();
            });
    }
    table->appendPresentationColumnActions(menu);
    menu.exec(QPoint(x, y));
}
bool QtPanelContextMenu::GetItemRect(int index, QRect &rect) const {
    const QModelIndex item = impl_->view.model()->index(index, 0);
    if (!item.isValid())
        return false;
    rect = impl_->view.visualRect(item);
    return rect.isValid();
}
void QtPanelContextMenu::ClientToScreen(int &x, int &y) const {
    const QPoint point = impl_->view.viewport()->mapToGlobal(QPoint(x, y));
    x = point.x();
    y = point.y();
}
void QtPanelContextMenu::CreatePopup() {
    auto &state = *impl_;
    state.projection.screenServices = &state.screen;
    for (QAction *action : state.window.programFileMenu_->SourceActions()) {
        if (action->property("workspaceCommand").toBool())
            continue;
        const bool separator = action->isSeparator();
        const unsigned id = action->menu() ? 0u : action->property("nativeCommandID").toUInt();
        state.projection.sourceItems.append({id, ToUString(action->text()), separator});
        if (!separator && !action->menu())
            state.actions.insert(id, action);
    }
}
IQtFileMenuServices &QtPanelContextMenu::Menu() { return impl_->projection; }
IQtPanelPluginMenuServices &QtPanelContextMenu::Plugins() { return impl_->shell; }

static QAction *AppendCommand(QMenu &menu, const QAction &source, bool enabled) {
    auto *action = menu.addAction(source.icon(), source.text());
    action->setObjectName(source.objectName());
    action->setProperty("nativeCommandID", source.property("nativeCommandID"));
    action->setShortcuts(source.shortcuts());
    action->setShortcutVisibleInContextMenu(source.isShortcutVisibleInContextMenu());
    action->setShortcutContext(Qt::WidgetShortcut);
    action->setCheckable(source.isCheckable());
    action->setChecked(source.isChecked());
    action->setVisible(source.isVisible());
    action->setEnabled(enabled);
    return action;
}

unsigned QtPanelContextMenu::Track(int x, int y) {
    auto &state = *impl_;
    const unsigned owner = state.window.app_.GetFocusedPanelIndex();
    const auto availability = state.window.commands_->AvailabilityForPanel(owner);
    const auto enabled = [&availability](const QAction &action) {
        return availability.value(action.property("nativeCommandID").toUInt());
    };
    // Native TrackPopupMenu owns separate rows and returns only a command ID.
    // Keep their state independent of the main-menu projection during popup
    // focus events, while source notification guards are held.
    for (const auto &entry : state.projection.items) {
        if (entry.source.IsSeparator()) {
            state.popup.addSeparator();
            continue;
        }
        if (entry.source.wID == 0) {
            auto *hash = state.popup.addMenu(QStringLiteral("CRC"));
            hash->setObjectName("contextHashMenu");
            QtBindLocalText(hash->menuAction(), "CommandMenus",
                            QT_TRANSLATE_NOOP("CommandMenus", "Checksums"));
            for (QAction *action : state.window.findChild<QMenu *>("hashMenu")->actions())
                AppendCommand(*hash, *action, enabled(*action));
            continue;
        }
        if (QAction *action = state.actions.value(entry.source.wID)) {
            AppendCommand(state.popup, *action, enabled(*action) && entry.enabled);
        }
    }
    QtClarifyOpenCommands(state.popup);
    QtGroupFileCommands(state.popup, true);
    auto &clipboard = *state.window.clipboard_;
    const auto clipboardAvailability = clipboard.AvailabilityForPanel(owner);
    auto *before = state.popup.actions().isEmpty() ? nullptr : state.popup.actions().first();
    for (auto *source : {clipboard.cut, clipboard.copy, clipboard.paste}) {
        auto *local = new QAction(source->text(), &state.popup);
        local->setEnabled(source == clipboard.cut    ? clipboardAvailability.cut
                          : source == clipboard.copy ? clipboardAvailability.copy
                                                     : clipboardAvailability.paste);
        local->setProperty("fileClipboard", QVariant::fromValue(source));
        state.popup.insertAction(before, local);
        if (source == clipboard.paste)
            QObject::connect(QApplication::clipboard(), &QClipboard::dataChanged, local,
                             [clipboard = &clipboard, local, owner] {
                                 local->setEnabled(clipboard->AvailabilityForPanel(owner).paste);
                             });
    }
    auto *archives =
        state.popup.addMenu(QCoreApplication::translate("QtWindowWorkspace", "Archive"));
    archives->setObjectName("contextArchiveMenu");
    for (auto *action : {state.window.toolbarAdd_, state.window.extract_, state.window.test_})
        AppendCommand(*archives, *action, enabled(*action));
    // OnContextMenu dispatches the chosen ID after the popup guards restore.
    QAction *chosen;
    {
        // Closing a Qt popup restores the previously focused peer widget.
        // Its synchronous FocusIn must not retarget this source command.
        CPanel::CDisableNotify left(state.window.app_.Panels[0]);
        CPanel::CDisableNotify right(state.window.app_.Panels[1]);
        chosen = state.popup.exec(QPoint(x, y));
    }
    state.window.app_.SetFocusedPanel(owner);
    if (chosen)
        if (auto *source = chosen->property("fileClipboard").value<QAction *>()) {
            if (source == clipboard.paste)
                clipboard.Paste();
            else
                clipboard.Copy(source == clipboard.cut);
            return 0;
        }
    return state.pluginID ? state.pluginID
                          : (chosen ? chosen->property("nativeCommandID").toUInt() : 0u);
}
void QtPanelContextMenu::InvokePluginCommand(unsigned id) { impl_->shell.InvokePluginCommand(id); }
bool QtPanelContextMenu::ExecuteFileCommand(unsigned id) {
    return impl_->window.app_.ExecuteCommand(id);
}
