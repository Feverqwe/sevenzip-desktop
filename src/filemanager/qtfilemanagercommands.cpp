#include "filemanager/qtfilemanagercommands.h"
#include "7zip/UI/FileManager/resource.h"
#include "filemanager/MyLoadMenu.h"
#include "filemanager/window/mainwindow.h"
#include <QSignalBlocker>

QtFileManagerCommands::QtFileManagerCommands(MainWindow &window) : window_(window) {}

void QtFileManagerCommands::Bind(QAction *action, unsigned id) {
    actions_[id].append(action);
    QObject::disconnect(action, &QAction::triggered, &window_, nullptr);
    QObject::connect(action, &QAction::triggered, &window_, [this, id] {
        if (!window_.app_.ExecuteCommand(id))
            window_.refreshActions();
    });
    action->setProperty("nativeCommandID", id);
}

void QtFileManagerCommands::BindToggle(QAction *action, unsigned id, bool (*readState)(CApp &)) {
    actions_[id].append(action);
    QObject::disconnect(action, &QAction::toggled, &window_, nullptr);
    QObject::connect(action, &QAction::toggled, &window_,
                     [this, action, id, readState](bool checked) {
                         const bool previous = readState(window_.app_);
                         if (checked == previous)
                             return;
                         // QAction toggles before dispatch; native CApp owns the actual flag.
                         // Roll presentation back before the command and mirror it on reload.
                         {
                             QSignalBlocker block(action);
                             action->setChecked(previous);
                         }
                         window_.app_.ExecuteCommand(id);
                     });
    action->setProperty("nativeCommandID", id);
}

void QtFileManagerCommands::AttachActions() {
    AttachFileActions();
    AttachPanelActions();
    AttachViewActions();
    AttachMountActions();
    window_.app_.AttachQtCommandServices(this);
    for (QAction *action : {window_.toolbarAdd_, window_.extract_, window_.test_, window_.copy_,
                            window_.move_, window_.remove_, window_.infoAction_})
        QObject::connect(action, &QAction::changed, &window_, &MainWindow::refreshToolbarTexts);
}

void QtFileManagerCommands::AttachFileActions() {
    auto &w = window_;
    Bind(w.infoAction_, IDM_PROPERTIES);
    Bind(w.back_, Back);
    Bind(w.toolbarAdd_, kMenuCmdID_Toolbar_Add);
    Bind(w.extract_, kMenuCmdID_Toolbar_Extract);
    Bind(w.test_, kMenuCmdID_Toolbar_Test);
    Bind(w.openItem_, IDM_OPEN);
    Bind(w.findChild<QAction *>("openArchiveAction"), OpenArchivePicker);
    Bind(w.findChild<QAction *>("openDeviceAction"), OpenDevicePicker);
    Bind(w.openInside_, IDM_OPEN_INSIDE);
    Bind(w.openInsideOne_, IDM_OPEN_INSIDE_ONE);
    Bind(w.openInsideParser_, IDM_OPEN_INSIDE_PARSER);
    Bind(w.openOutside_, IDM_OPEN_OUTSIDE);
    Bind(w.viewFile_, IDM_FILE_VIEW);
    Bind(w.editFile_, IDM_FILE_EDIT);
    Bind(w.diffFiles_, IDM_DIFF);
    const char *verCtrlNames[] = {"verEditAction", "verCommitAction", "verRevertAction",
                                  "verDiffAction"};
    for (unsigned i = 0; i < 4; ++i)
        Bind(w.findChild<QAction *>(QString::fromLatin1(verCtrlNames[i])), IDM_VER_EDIT + i);
    Bind(w.rename_, IDM_RENAME);
    Bind(w.copy_, IDM_COPY_TO);
    Bind(w.move_, IDM_MOVE_TO);
    Bind(w.remove_, IDM_DELETE);
    Bind(w.split_, IDM_SPLIT);
    Bind(w.combine_, IDM_COMBINE);
    Bind(w.properties_, IDM_PROPERTIES);
    Bind(w.findChild<QAction *>("commentAction"), IDM_COMMENT);
    Bind(w.findChild<QAction *>("linkAction"), IDM_LINK);
    Bind(w.findChild<QAction *>("altStreamsAction"), IDM_ALT_STREAMS);
    Bind(w.createFolder_, IDM_CREATE_FOLDER);
    Bind(w.createFile_, IDM_CREATE_FILE);
    Bind(w.exitAction_, 8);
    for (auto it = w.hashActions_.cbegin(); it != w.hashActions_.cend(); ++it)
        Bind(it.value(), it.key());
}

void QtFileManagerCommands::AttachPanelActions() {
    auto &w = window_;
    Bind(w.up_, IDM_OPEN_PARENT_FOLDER);
    Bind(w.parent_, IDM_OPEN_PARENT_FOLDER);
    Bind(w.selectAll_, IDM_SELECT_ALL);
    Bind(w.deselectAll_, IDM_DESELECT_ALL);
    Bind(w.invertSelection_, IDM_INVERT_SELECTION);
    Bind(w.selectMask_, IDM_SELECT);
    Bind(w.deselectMask_, IDM_DESELECT);
    Bind(w.selectType_, IDM_SELECT_BY_TYPE);
    Bind(w.deselectType_, IDM_DESELECT_BY_TYPE);
}

void QtFileManagerCommands::AttachViewActions() {
    auto &w = window_;
    Bind(w.twoPanelsAction_, IDM_VIEW_TWO_PANELS);
    Bind(w.refresh_, IDM_VIEW_REFRESH);
    Bind(w.autoRefreshAction_, IDM_VIEW_AUTO_REFRESH);
    Bind(w.foldersHistory_, IDM_FOLDERS_HISTORY);
    Bind(w.root_, IDM_OPEN_ROOT_FOLDER);
    Bind(w.settingsAction_, IDM_OPTIONS);
    Bind(w.findChild<QAction *>("helpContentsAction"), IDM_HELP_CONTENTS);
    Bind(w.findChild<QAction *>("aboutAction"), IDM_ABOUT);
    Bind(w.findChild<QAction *>("tempDirAction"), IDM_TEMP_DIR);
    Bind(w.findChild<QAction *>("benchmarkAction"), IDM_BENCHMARK);
    BindToggle(w.flatView_, IDM_VIEW_FLAT_VIEW, [](CApp &app) { return app.GetFlatMode(); });
    BindToggle(w.standardToolbarOption_, IDM_VIEW_STANDARD_TOOLBAR,
               [](CApp &app) { return app.ShowStandardToolbar; });
    BindToggle(w.archiveToolbarOption_, IDM_VIEW_ARCHIVE_TOOLBAR,
               [](CApp &app) { return app.ShowArchiveToolbar; });
    BindToggle(w.buttonTextOption_, IDM_VIEW_TOOLBARS_SHOW_BUTTONS_TEXT,
               [](CApp &app) { return app.ShowButtonsLables; });
    BindToggle(w.largeButtonsOption_, IDM_VIEW_TOOLBARS_LARGE_BUTTONS,
               [](CApp &app) { return app.LargeButtons; });
    for (unsigned i = 0; i < 4; ++i)
        Bind(w.viewActions_[i], IDM_VIEW_LARGE_ICONS + i);
    const unsigned sortIDs[] = {IDM_VIEW_ARANGE_BY_NAME, IDM_VIEW_ARANGE_BY_TYPE,
                                IDM_VIEW_ARANGE_BY_DATE, IDM_VIEW_ARANGE_BY_SIZE,
                                IDM_VIEW_ARANGE_NO_SORT};
    for (unsigned i = 0; i < 5; ++i)
        Bind(w.sortActions_[i], sortIDs[i]);
    for (unsigned i = 0; i < 5; ++i)
        Bind(w.timeActions_[i], IDM_VIEW_TIME + i);
    Bind(w.utcTimeAction_, IDM_VIEW_TIME_UTC);
    for (unsigned i = 0; i < 10; ++i) {
        Bind(w.openBookmarks_[i], 830 + i);
        Bind(w.saveBookmarks_[i], 810 + i);
    }
}
