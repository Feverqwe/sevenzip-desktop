#include "filemanager/qtfilemanagercommands.h"
#include "7zip/UI/FileManager/resource.h"
#include "filemanager/MyLoadMenu.h"
#include "filemanager/window/mainwindow.h"
#include <QTimer>

QtFileManagerCommands::QtFileManagerCommands(MainWindow &window) : window_(window) {}

void QtFileManagerCommands::Bind(QAction *action, unsigned id, bool viewContext) {
    actions_[id].append(action);
    QObject::disconnect(action, &QAction::triggered, &window_, nullptr);
    QObject::connect(action, &QAction::triggered, &window_, [this, id, viewContext] {
        if (viewContext && viewMenuSource_) {
            InvokeViewCommand(id);
            return;
        }
        if (!window_.app_.ExecuteCommand(id))
            window_.refreshActions();
    });
    action->setProperty("nativeCommandID", id);
    action->setProperty("workspaceViewContext", viewContext);
}

std::optional<unsigned> QtFileManagerCommands::CapturedViewPanel() const {
    if (!viewMenuSource_ || !*viewMenuSource_)
        return {};
    for (unsigned panel = 0; panel < kNumPanelsMax; ++panel)
        if (window_.app_.IsPanelVisible(panel) &&
            &window_.app_.Panels[panel] == viewMenuSource_->data()) {
            return panel;
        }
    return {};
}

void QtFileManagerCommands::InvokeViewCommand(unsigned id) {
    if (const auto panel = CapturedViewPanel())
        window_.withPanelProjection(*panel, [this, id] {
            if (!window_.app_.ExecuteCommand(id))
                window_.refreshActions();
        });
}

void QtFileManagerCommands::AttachActions() {
    AttachFileActions();
    AttachPanelActions();
    AttachViewActions();
    AttachMountActions();
    AttachDirectoryActions();
    window_.app_.AttachQtCommandServices(this);
}

void QtFileManagerCommands::AttachFileActions() {
    auto &w = window_;
    Bind(w.infoAction_, IDM_PROPERTIES);
    Bind(w.back_, Back, true);
    Bind(w.forward_, Forward, true);
    Bind(w.toolbarAdd_, kMenuCmdID_Toolbar_Add);
    Bind(w.extract_, kMenuCmdID_Toolbar_Extract);
    Bind(w.test_, kMenuCmdID_Toolbar_Test);
    Bind(w.openItem_, IDM_OPEN);
    Bind(w.findChild<QAction *>("openArchiveAction"), OpenArchivePicker);
    Bind(w.findChild<QAction *>("openDeviceAction"), OpenDevicePicker);
    Bind(w.openInside_, IDM_OPEN_INSIDE);
    Bind(w.openOutside_, IDM_OPEN_OUTSIDE);
    Bind(w.viewFile_, IDM_FILE_VIEW);
    Bind(w.editFile_, IDM_FILE_EDIT);
    Bind(w.rename_, IDM_RENAME);
    Bind(w.copy_, IDM_COPY_TO);
    Bind(w.move_, IDM_MOVE_TO);
    Bind(w.remove_, IDM_DELETE);
    Bind(w.properties_, IDM_PROPERTIES);
    Bind(w.createFolder_, IDM_CREATE_FOLDER);
    Bind(w.exitAction_, 8);
    for (auto it = w.hashActions_.cbegin(); it != w.hashActions_.cend(); ++it)
        Bind(it.value(), it.key());
}

void QtFileManagerCommands::AttachPanelActions() {
    auto &w = window_;
    Bind(w.up_, IDM_OPEN_PARENT_FOLDER, true);
    Bind(w.parent_, IDM_OPEN_PARENT_FOLDER);
    Bind(w.selectAll_, IDM_SELECT_ALL);
    Bind(w.deselectAll_, IDM_DESELECT_ALL);
}

void QtFileManagerCommands::AttachViewActions() {
    auto &w = window_;
    auto *menu = w.findChild<QMenu *>("viewMenu");
    QObject::connect(menu, &QMenu::aboutToShow, &w, [this] {
        ++viewMenuRequest_;
        viewMenuSource_ = &window_.app_.GetFocusedPanel();
    });
    QObject::connect(menu, &QMenu::aboutToHide, &w, [this] {
        const auto request = viewMenuRequest_;
        // QMenu hides before delivering the selected QAction.
        QTimer::singleShot(0, &window_, [this, request] {
            if (request == viewMenuRequest_) {
                viewMenuSource_.reset();
                window_.refreshActions();
            }
        });
    });
    Bind(w.twoPanelsAction_, IDM_VIEW_TWO_PANELS);
    Bind(w.refresh_, IDM_VIEW_REFRESH, true);
    Bind(w.foldersHistory_, IDM_FOLDERS_HISTORY);
    Bind(w.root_, IDM_OPEN_ROOT_FOLDER, true);
    Bind(w.findChild<QAction *>("sortAscendingAction"), SortAscending, true);
    Bind(w.findChild<QAction *>("sortDescendingAction"), SortDescending, true);
    Bind(w.settingsAction_, IDM_OPTIONS);
    Bind(w.findChild<QAction *>("aboutAction"), IDM_ABOUT);
    Bind(w.findChild<QAction *>("tempDirAction"), IDM_TEMP_DIR);
    for (unsigned i : {0u, 3u})
        Bind(w.viewActions_[i], IDM_VIEW_LARGE_ICONS + i, true);
    const unsigned sortIDs[] = {IDM_VIEW_ARANGE_BY_NAME, IDM_VIEW_ARANGE_BY_TYPE,
                                IDM_VIEW_ARANGE_BY_DATE, IDM_VIEW_ARANGE_BY_SIZE};
    for (unsigned i = 0; i < 4; ++i)
        Bind(w.sortActions_[i], sortIDs[i], true);
    for (unsigned i = 0; i < 10; ++i) {
        Bind(w.openBookmarks_[i], 830 + i);
        Bind(w.saveBookmarks_[i], 810 + i);
    }
}
