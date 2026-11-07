#include "filemanager/qtprogramfilemenu.h"
#include "7zip/UI/FileManager/resource.h"
#include "filemanager/MyLoadMenu.h"
#include "filemanager/qtfilemanagershellmenu.h"
#include "filemanager/qtfilemenuprojection.h"
#include "filemanager/window/mainwindow.h"
#include "localization/qtlangcontrols.h"
#include "ui/dialogs/qtdialogscreenservices.h"
#include <algorithm>

QList<QAction *> QtGroupFileCommands(QMenu &menu, bool contextMenu) {
    QList<QAction *> prefix, picker, open, modes, editors, frequent, create, rare, hash, properties,
        exit;
    for (QAction *action : menu.actions()) {
        menu.removeAction(action);
        if (action->isSeparator())
            continue;
        const unsigned id = action->property("nativeCommandID").toUInt();
        if (action->objectName() == "openArchiveAction" ||
            action->objectName() == "openDeviceAction")
            picker.append(action);
        else if (action->menu() && (action->menu()->objectName() == "hashMenu" ||
                                    action->menu()->objectName() == "contextHashMenu"))
            hash.append(action);
        else {
            switch (id) {
            case IDM_OPEN:
                open.append(action);
                break;
            case IDM_OPEN_INSIDE:
            case IDM_OPEN_OUTSIDE:
                modes.append(action);
                break;
            case IDM_FILE_VIEW:
            case IDM_FILE_EDIT:
                editors.append(action);
                break;
            case IDM_RENAME:
            case IDM_COPY_TO:
            case IDM_MOVE_TO:
            case IDM_DELETE:
                frequent.append(action);
                break;
            case IDM_CREATE_FOLDER:
            case IDM_CREATE_FILE:
            case IDM_LINK:
                create.append(action);
                break;
            case IDM_SPLIT:
            case IDM_COMBINE:
            case IDM_COMMENT:
            case IDM_ALT_STREAMS:
            case IDM_DIFF:
            case IDM_VER_EDIT:
            case IDM_VER_COMMIT:
            case IDM_VER_REVERT:
            case IDM_VER_DIFF:
                rare.append(action);
                break;
            case IDM_PROPERTIES:
                properties.append(action);
                break;
            case 8:
                exit.append(action);
                break; // Source IDCLOSE.
            default:
                prefix.append(action);
                break; // Preserve plugin/extension rows.
            }
        }
    }
    QList<QAction *> disposable;
    const auto append = [](QMenu &target, const QList<QAction *> &actions) {
        for (QAction *action : actions)
            if (action)
                target.addAction(action);
            else
                target.addSeparator();
    };
    const auto separator = [&] {
        if (!menu.actions().isEmpty())
            disposable.append(menu.addSeparator());
    };
    const auto group = [&](const char *name, const char *text, const QList<QAction *> &actions) {
        const bool hasVisibleAction =
            std::any_of(actions.cbegin(), actions.cend(),
                        [](QAction *action) { return action && action->isVisible(); });
        if (!hasVisibleAction)
            return;
        auto *sub = new QMenu(&menu);
        sub->setObjectName(QString::fromLatin1(name));
        QtBindLocalText(sub->menuAction(), "CommandMenus", text);
        append(*sub, actions);
        menu.addMenu(sub);
        disposable.append(sub->menuAction());
    };
    if (contextMenu) {
        picker.append(modes);
        modes = picker;
    } else
        append(menu, picker);
    append(menu, open);
    group("openAsMenu", QT_TRANSLATE_NOOP("CommandMenus", "Open as"), modes);
    if (!contextMenu) {
        separator();
        append(menu, editors);
    }
    separator();
    append(menu, frequent);
    separator();
    if (!contextMenu)
        group("createMenu", QT_TRANSLATE_NOOP("CommandMenus", "Create"), create);
    append(menu, hash);
    if (contextMenu) {
        QList<QAction *> more = editors;
        more.append(nullptr);
        more.append(create);
        more.append(nullptr);
        more.append(rare);
        more.append(nullptr);
        more.append(prefix);
        group("moreCommandsMenu", QT_TRANSLATE_NOOP("CommandMenus", "More"), more);
    } else {
        rare.append(nullptr);
        rare.append(prefix);
        group("advancedCommandsMenu", QT_TRANSLATE_NOOP("CommandMenus", "Advanced"), rare);
    }
    separator();
    append(menu, properties);
    if (!exit.isEmpty()) {
        separator();
        append(menu, exit);
    }
    return disposable;
}

void QtClarifyOpenCommands(QMenu &menu) {
    for (QAction *action : menu.actions()) {
        const unsigned id = action->property("nativeCommandID").toUInt();
        switch (id) {
        case IDM_OPEN_INSIDE:
            QtBindLocalText(action, "qtprogramfilemenu",
                            QT_TRANSLATE_NOOP("qtprogramfilemenu", "Open as archive"));
            break;
        case IDM_OPEN_OUTSIDE:
            QtBindLocalText(action, "qtprogramfilemenu",
                            QT_TRANSLATE_NOOP("qtprogramfilemenu", "Open in external application"));
            break;
        }
    }
}

QtProgramFileMenu::QtProgramFileMenu(MainWindow &window)
    : _window(window), _menu(*window.findChild<QMenu *>("fileMenu")) {
    _source = _menu.actions();
    _shell = std::make_unique<QtFileManagerShellMenu>(window, _menu, [this](unsigned id) {
        _window.app_.ExecuteCommand(id);
        _window.refreshActions();
    });
    QObject::connect(&_menu, &QMenu::aboutToShow, &_window, [this] { Activate(); });
}

QtProgramFileMenu::~QtProgramFileMenu() = default;

void QtProgramFileMenu::ReloadLanguage() { MyReloadFileMenu(_source); }

void QtProgramFileMenu::ClearDynamicRows() {
    for (QAction *action : _dynamic) {
        _menu.removeAction(action);
        if (action->menu())
            action->menu()->deleteLater();
        else
            action->deleteLater();
    }
    _dynamic.clear();
}

void QtProgramFileMenu::Activate() {
    auto &panel = _window.app_.GetFocusedPanel();
    // No folder/property access during source worker replacement or scanning.
    if (_window.app_.QtOperations().callActive() || _window.sourceArchiveProgress_ ||
        _window.projectedUi().scanningDisk || !panel.ShouldProcessNotifications()) {
        ReleasePluginMenus();
        ClearDynamicRows();
        return;
    }
    ClearDynamicRows();
    _window.sourceOperatedIndices();
    QtFileMenuProjection projection;
    ReadRegDiff(projection.diffPath, MainWindow::toolbarSettingsPath());
    ReadReg_VerCtrlPath(projection.versionControlPath, MainWindow::toolbarSettingsPath());
    QtDialogScreenServices screen(&_window);
    projection.screenServices = &screen;
    QHash<unsigned, QAction *> actions;
    QList<QAction *> workspace;
    for (QAction *action : _source) {
        if (action->property("workspaceCommand").toBool()) {
            workspace.append(action);
            continue;
        }
        const unsigned id = action->menu() ? 0 : action->property("nativeCommandID").toUInt();
        if (id < IDM_VER_EDIT || id > IDM_VER_DIFF)
            projection.sourceItems.append({id, ToUString(action->text()), action->isSeparator()});
        if (!action->isSeparator())
            actions.insert(id, action);
    }
    _panel = &panel;
    OnMenuActivating(*_window.menuBar(), _menu, 0, _window.app_, projection, *_shell);
    _dynamic = _menu.actions(); // Source plugin prefix only; static rows follow.
    for (const auto &item : projection.items) {
        if (item.source.IsSeparator()) {
            _dynamic.append(_menu.addSeparator());
            continue;
        }
        if (QAction *action = actions.value(item.source.wID)) {
            _menu.addAction(action);
            action->setEnabled(action->isEnabled() && item.enabled);
        }
    }
    QtClarifyOpenCommands(_menu);
    _dynamic.append(QtGroupFileCommands(_menu, false));
    QAction *before = _menu.actions().isEmpty() ? nullptr : _menu.actions().first();
    for (auto *action : workspace)
        _menu.insertAction(before, action);
}

bool QtProgramFileMenu::CanInvoke(unsigned id) const {
    return _panel && _panel == &_window.app_.GetFocusedPanel() &&
           !_window.projectedUi().locationUnavailable &&
           _window.projectedUi().state == MainWindow::State::Filesystem &&
           !_window.backend_->busy() && !_window.sourceArchiveProgress_ &&
           !_window.projectedUi().scanningDisk && _panel->ShouldProcessNotifications() &&
           _shell->CanInvoke(id);
}

void QtProgramFileMenu::InvokePluginCommand(unsigned id) { _shell->InvokePluginCommand(id); }

void QtProgramFileMenu::ReleasePluginMenus() {
    _shell->ReleasePluginMenus();
    _panel = nullptr;
}
