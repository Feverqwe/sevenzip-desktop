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
#include "ui/dialogs/qtdialogscreenservices.h"
#include <QClipboard>
#include <QSet>
#include <algorithm>
#include <span>

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
                const PROPID previousSort = panel.GetSortID();
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
                    if (panel.GetSortID() != previousSort) {
                        panel.SetSortRawStatus();
                        table->sortItems(column, order);
                    }
                }
                window.saveColumns();
            });
    }
    table->appendPresentationColumnActions(menu);
    menu.exec(QPoint(x, y));
}
bool QtPanelContextMenu::GetItemRect(int index, QRect &rect) const {
    const QModelIndex item = impl_->view.currentIndex().siblingAtRow(index);
    if (!item.isValid())
        return false;
    const QRect viewport = impl_->view.viewport()->rect();
    rect = impl_->view.visualRect(item).intersected(viewport);
    if (rect.isEmpty())
        rect = QRect(viewport.topLeft(), QSize(1, 1));
    return true;
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

static QAction *CopyCommand(QMenu &menu, const QAction &source) {
    auto *action = new QAction(source.icon(), source.text(), &menu);
    action->setObjectName(source.objectName());
    action->setProperty("nativeCommandID", source.property("nativeCommandID"));
    action->setShortcuts(source.shortcuts());
    action->setShortcutVisibleInContextMenu(source.isShortcutVisibleInContextMenu());
    action->setShortcutContext(Qt::WidgetShortcut);
    action->setCheckable(source.isCheckable());
    action->setChecked(source.isChecked());
    return action;
}

static void MoveShellRows(QMenu &source, QMenu &archives, QMenu &more, QMenu &hashes,
                          const QSet<QString> &methods, bool writable) {
    for (auto *action : source.actions()) {
        source.removeAction(action);
        if (action->isSeparator() || !action->isEnabled() || !action->isVisible())
            continue;
        const auto verb = action->property("shellVerb").toString();
        if (verb == "SevenZip" || verb == "SevenZip.Checksum") {
            if (verb == "SevenZip")
                archives.setIcon(action->icon());
            MoveShellRows(*action->menu(), archives, more, hashes, methods, writable);
        } else if (verb.startsWith("SevenZip.Checksum.Calc.")) {
            if (!methods.contains(verb.sliced(sizeof("SevenZip.Checksum.Calc.") - 1)))
                hashes.addAction(action);
        } else if (verb.startsWith("SevenZip.Open.") || verb == "SevenZip.OpenWithType." ||
                   verb == "SevenZipOpen") {
            more.addAction(action);
        } else if (writable || !(verb.startsWith("SevenZipCompress") ||
                                 verb == "SevenZipExtractHere" || verb == "SevenZipExtractTo")) {
            if (verb == "SevenZipCompress")
                action->setText(QCoreApplication::translate("CommandMenus", "Add to archive…"));
            if (verb == "SevenZipExtract")
                action->setText(QCoreApplication::translate("CommandMenus", "Extract…"));
            archives.addAction(action);
        }
    }
}

static void AppendSection(QMenu &menu, const QList<QAction *> &rows) {
    if (rows.isEmpty())
        return;
    if (!menu.actions().isEmpty())
        menu.addSeparator();
    menu.addActions(rows);
}

static QList<QAction *> ClipboardRows(QMenu &menu, QtFileClipboard &clipboard, unsigned owner) {
    const auto available = clipboard.AvailabilityForPanel(owner);
    const auto create = [&](QAction *source) {
        auto *local = new QAction(source->icon(), source->text(), &menu);
        local->setObjectName(source->objectName());
        local->setProperty("fileClipboard", QVariant::fromValue(source));
        return local;
    };
    QList<QAction *> rows;
    if (available.cut)
        rows.append(create(clipboard.cut));
    if (available.copy)
        rows.append(create(clipboard.copy));
    // Keep the Paste action available for clipboard changes during QMenu::exec.
    auto *paste = create(clipboard.paste);
    paste->setVisible(available.paste);
    QObject::connect(QApplication::clipboard(), &QClipboard::dataChanged, paste,
                     [&clipboard, paste, owner] {
                         paste->setVisible(clipboard.AvailabilityForPanel(owner).paste);
                     });
    rows.append(paste);
    return rows;
}

void QtPanelContextMenu::Populate(unsigned owner) {
    auto &state = *impl_;
    auto &window = state.window;
    auto &panel = window.app_.Panels[owner];
    const auto availability = window.commands_->AvailabilityForPanel(owner);
    CUIntVector indices;
    panel.Get_ItemIndices_Operated(indices);
    const bool items = !indices.IsEmpty();
    const auto focus = panel.GetFocusState();
    const bool onItem = items || focus.Present;
    const bool filesystem = panel.IsFSFolder();
    const bool archive = panel.IsArcFolder();

    // Native TrackPopupMenu owns separate rows and returns only a command ID.
    // Use availability for this panel independently of the program menu.
    QMenu &root = state.popup;
    auto *archives = new QMenu(QCoreApplication::translate("QtWindowWorkspace", "Archive"), &root);
    archives->setObjectName("contextArchiveMenu");
    auto *more = new QMenu(&root);
    more->setObjectName("moreCommandsMenu");
    QtBindLocalText(more->menuAction(), "CommandMenus", QT_TRANSLATE_NOOP("CommandMenus", "More"));
    auto *hashes = new QMenu(more);
    hashes->setObjectName("contextHashMenu");
    QtBindLocalText(hashes->menuAction(), "CommandMenus",
                    QT_TRANSLATE_NOOP("CommandMenus", "Checksums"));
    QSet<QString> methods;
    if (state.projection.IsEnabled(0) && items) {
        for (const auto *source : window.findChild<QMenu *>("hashMenu")->actions()) {
            const unsigned id = source->property("nativeCommandID").toUInt();
            if (availability.value(id)) {
                hashes->addAction(CopyCommand(*hashes, *source));
                methods.insert(QString::fromLatin1(GetHashMethodForCommand(id)));
            }
        }
    }

    QAction *system = nullptr;
    QMenu shellRows;
    for (auto *action : root.actions()) {
        root.removeAction(action);
        if (action->objectName() == "systemContextGroup")
            system = action;
        else
            shellRows.addAction(action);
    }
    const bool writable = filesystem && availability.value(IDM_CREATE_FOLDER);
    MoveShellRows(shellRows, *archives, *more, *hashes, methods, writable);

    QList<QAction *> opening, changes;
    QAction *properties = nullptr;
    const bool folderSize = availability.value(QtFileManagerCommands::CalculateDirectorySize);
    for (const auto &entry : state.projection.items) {
        const unsigned id = entry.source.wID;
        if (entry.source.IsSeparator() || !entry.enabled || !availability.value(id))
            continue;
        const auto *source = state.actions.value(id);
        if (!source)
            continue;
        switch (id) {
        case IDM_OPEN:
            opening.append(CopyCommand(root, *source));
            break;
        case IDM_RENAME:
            if (indices.Size() == 1)
                changes.append(CopyCommand(root, *source));
            break;
        case IDM_DELETE:
            changes.append(CopyCommand(root, *source));
            changes.last()->setText(QCoreApplication::translate("DeleteRedesign", "Move to Trash"));
            break;
        case IDM_CREATE_FOLDER:
            if (!onItem) {
                auto *create = CopyCommand(root, *source);
                create->setText(QCoreApplication::translate("CommandMenus", "Create folder"));
                opening.append(create);
            }
            break;
        case IDM_PROPERTIES:
            properties = CopyCommand(root, *source);
            break;
        case IDM_COPY_TO:
        case IDM_MOVE_TO:
            if (items) {
                auto *action = CopyCommand(*more, *source);
                action->setText(id == IDM_COPY_TO
                                    ? QCoreApplication::translate("CommandMenus", "Copy to…")
                                    : QCoreApplication::translate("CommandMenus", "Move to…"));
                more->addAction(action);
            }
            break;
        case IDM_FILE_VIEW:
            if (onItem && !(filesystem && folderSize))
                more->addAction(CopyCommand(*more, *source));
            break;
        default:
            if (onItem)
                more->addAction(CopyCommand(*more, *source));
            break;
        }
    }
    QtClarifyOpenCommands(*more);
    if (!hashes->actions().isEmpty())
        more->addMenu(hashes);

    const auto appendArchive = [&](QAction *source, const QString &caption = {}) {
        if (!availability.value(source->property("nativeCommandID").toUInt()))
            return;
        auto *action = CopyCommand(*archives, *source);
        if (!caption.isEmpty())
            action->setText(caption);
        archives->addAction(action);
    };
    const auto shellArchiveRows = archives->actions();
    const auto hasShellVerb = [&shellArchiveRows](QLatin1String verb) {
        return std::any_of(
            shellArchiveRows.cbegin(), shellArchiveRows.cend(),
            [verb](QAction *row) { return row->property("shellVerb").toString() == verb; });
    };
    if (writable && items && !hasShellVerb(QLatin1String("SevenZipCompress")))
        appendArchive(window.toolbarAdd_,
                      QCoreApplication::translate("CommandMenus", "Add to archive…"));
    // The application actions remain available for archive candidates even
    // when the corresponding Explorer verbs were disabled in settings.
    const auto selected = std::span(indices.ConstData(), indices.Size());
    const bool archiveCandidates =
        filesystem && items &&
        std::all_of(selected.begin(), selected.end(), [&panel](UInt32 index) {
            return !panel.IsItem_Folder(index) &&
                   CZipContextMenu::IsArchiveCandidate(panel.GetItemName(index));
        });
    if (items && (archive || archiveCandidates) && !hasShellVerb(QLatin1String("SevenZipExtract")))
        appendArchive(window.extract_, QCoreApplication::translate("CommandMenus", "Extract…"));
    if ((archive || archiveCandidates) && !hasShellVerb(QLatin1String("SevenZipTest")))
        appendArchive(window.test_);
    if (archive)
        appendArchive(window.findChild<QAction *>("mountReadOnlyAction"));

    const auto transfer = ClipboardRows(root, *window.clipboard_, owner);
    if (onItem) {
        root.addActions(opening);
        AppendSection(root, transfer);
    } else {
        root.addActions(transfer);
        root.addActions(opening);
    }
    AppendSection(root, changes);
    QList<QAction *> groups;
    for (auto *group : {archives, more})
        if (!group->actions().isEmpty())
            groups.append(group->menuAction());
    if (system)
        groups.append(system);
    AppendSection(root, groups);
    if (properties)
        AppendSection(root, {properties});
}

unsigned QtPanelContextMenu::Track(int x, int y) {
    auto &state = *impl_;
    const unsigned owner = state.window.app_.GetFocusedPanelIndex();
    Populate(owner);
    const auto rows = state.popup.actions();
    if (std::none_of(rows.cbegin(), rows.cend(),
                     [](QAction *row) { return !row->isSeparator() && row->isVisible(); }))
        return 0;
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
    if (chosen) {
        auto &clipboard = *state.window.clipboard_;
        if (auto *source = chosen->property("fileClipboard").value<QAction *>()) {
            if (source == clipboard.paste)
                clipboard.Paste();
            else
                clipboard.Copy(source == clipboard.cut);
            return 0;
        }
    }
    return state.pluginID ? state.pluginID
                          : (chosen ? chosen->property("nativeCommandID").toUInt() : 0u);
}
void QtPanelContextMenu::InvokePluginCommand(unsigned id) { impl_->shell.InvokePluginCommand(id); }
bool QtPanelContextMenu::ExecuteFileCommand(unsigned id) {
    return impl_->window.app_.ExecuteCommand(id);
}
