#include "7zip/UI/FileManager/resource.h"
#include "Windows/PropVariant.h"
#include "Windows/PropVariantConv.h"
#include "Windows/TimeUtils.h"
#include "filemanager/MyLoadMenu.h"
#include "filemanager/qtfilemanagercommandservices.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtmainwindowchrome.h"
#include "localization/qtlangcontrols.h"

QMenu *MainWindow::createMenusAndToolbar() {
    auto *menu = menuBar()->addMenu("&File");
    menu->setObjectName("fileMenu");
    auto *toolbar = addToolBar("Archive");
    toolbar->setObjectName("archiveToolbar");
    QtConfigureMainToolbar(*toolbar);
    QtBindLocalText(toolbar, "mainwindow_menus", QT_TRANSLATE_NOOP("mainwindow_menus", "Archive"));
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolbar->setIconSize(QSize(16, 16));
    createFileActions(menu, toolbar);
    createFileExtras(menu, toolbar);
    auto *editMenu = menuBar()->addMenu("&Edit");
    editMenu->setObjectName("editMenu");
    auto *viewMenu = menuBar()->addMenu("&View");
    viewMenu->setObjectName("viewMenu");
    createViewModes(viewMenu);
    auto *favoritesMenu = menuBar()->addMenu("F&avorites");
    favoritesMenu->setObjectName("favoritesMenu");
    auto *toolsMenu = menuBar()->addMenu("&Tools");
    toolsMenu->setObjectName("toolsMenu");
    settingsAction_ = new QAction("&Options...", toolsMenu);
    settingsAction_->setObjectName("settingsAction");
    createFavoritesMenu(favoritesMenu);
    createViewOptions(viewMenu);
    return editMenu;
}

void MainWindow::createFileActions(QMenu *menu, QToolBar *toolbar) {
    back_ = new QAction("Back", this);
    back_->setObjectName("backAction");
    QtBindLocalText(back_, "mainwindow_menus", QT_TRANSLATE_NOOP("mainwindow_menus", "Back"));
    up_ = new QAction("Up", this);
    up_->setObjectName("upAction");
    openItem_ = new QAction("&Open", this);
    openItem_->setObjectName("openItemAction");
    openInside_ = new QAction("Open &Inside", menu);
    openInside_->setObjectName("openInsideAction");
    openInsideOne_ = new QAction("Open Inside *", this);
    openInsideOne_->setObjectName("openInsideOneAction");
    openInsideParser_ = new QAction("Open Inside #", this);
    openInsideParser_->setObjectName("openInsideParserAction");
    openOutside_ = new QAction("Open O&utside", menu);
    openOutside_->setObjectName("openOutsideAction");
    toolbarAdd_ = toolbar->addAction("Add");
    toolbarAdd_->setObjectName("toolbarAddAction");
    extract_ = toolbar->addAction("Extract");
    extract_->setObjectName("extractArchiveAction");
    test_ = toolbar->addAction("Test");
    test_->setObjectName("testArchiveAction");
    copy_ = toolbar->addAction("Copy");
    copy_->setObjectName("copyAction");
    move_ = toolbar->addAction("Move");
    move_->setObjectName("moveAction");
    remove_ = toolbar->addAction("&Delete");
    remove_->setObjectName("deleteAction");
    split_ = new QAction("&Split file...", menu);
    split_->setObjectName("splitFileAction");
    combine_ = new QAction("Com&bine files...", menu);
    combine_->setObjectName("combineFilesAction");
    rename_ = new QAction("Rena&me", menu);
    rename_->setObjectName("renameAction");
    viewFile_ = new QAction("View", menu);
    viewFile_->setObjectName("viewFileAction");
    editFile_ = new QAction("&Edit", menu);
    editFile_->setObjectName("editFileAction");
    properties_ = new QAction("P&roperties", menu);
    properties_->setObjectName("selectedPropertiesAction");
    exitAction_ = new QAction("E&xit", this);
    exitAction_->setObjectName("exitAction");
    exitAction_->setMenuRole(QAction::QuitRole);
}

void MainWindow::createFileExtras(QMenu *menu, QToolBar *toolbar) {
    auto *hashMenu = new QMenu("CRC", menu);
    hashMenu->setObjectName("hashMenu");
    hashMenu->menuAction()->setObjectName("hashMenu");
    QtBindLocalText(hashMenu->menuAction(), "CommandMenus",
                    QT_TRANSLATE_NOOP("CommandMenus", "Checksums"));
    const struct {
        unsigned ID;
        const char *Label;
    } hashes[] = {{IDM_CRC32, "CRC-32"},      {IDM_CRC64, "CRC-64"},   {IDM_XXH64, "XXH64"},
                  {IDM_MD5, "MD5"},           {IDM_SHA1, "SHA-1"},     {IDM_SHA256, "SHA-256"},
                  {IDM_SHA384, "SHA-384"},    {IDM_SHA512, "SHA-512"}, {IDM_SHA3_256, "SHA3-256"},
                  {IDM_BLAKE2SP, "BLAKE2sp"}, {IDM_HASH_ALL, "*"}};
    for (const auto &hash : hashes) {
        auto *action = hashMenu->addAction(QString::fromLatin1(hash.Label));
        action->setObjectName(QStringLiteral("hash%1Action")
                                  .arg(QString::fromLatin1(GetHashMethodForCommand(hash.ID))));
        hashActions_.insert(hash.ID, action);
    }
    createFolder_ = new QAction("Create Folder", menu);
    createFolder_->setObjectName("createFolderAction");
    createFile_ = new QAction("Create File", menu);
    createFile_->setObjectName("createFileAction");
    infoAction_ = toolbar->addAction("Info");
    infoAction_->setObjectName("archiveInfoAction");
    toolbarAdd_->setIcon(toolbarIcon(0));
    extract_->setIcon(toolbarIcon(1));
    test_->setIcon(toolbarIcon(2));
    copy_->setIcon(toolbarIcon(3));
    move_->setIcon(toolbarIcon(4));
    remove_->setIcon(toolbarIcon(5));
    infoAction_->setIcon(toolbarIcon(6));
    toolbarGroupSeparator_ = toolbar->insertSeparator(copy_);
    toolbarGroupSeparator_->setObjectName("toolbarGroupSeparator");
    archiveToolbarActions_ = {toolbarAdd_, extract_, test_};
    standardToolbarActions_ = {copy_, move_, remove_, infoAction_};
}

void MainWindow::createViewModes(QMenu *viewMenu) {
    auto *viewGroup = new QActionGroup(this);
    const QStringList viewLabels = {"Lar&ge Icons", "S&mall Icons", "&List", "Details"};
    for (int index = 0; index < viewLabels.size(); ++index) {
        QAction *action = new QAction(viewLabels[index], viewMenu);
        action->setObjectName(QStringLiteral("viewMode%1Action").arg(index));
        action->setCheckable(true);
        action->setActionGroup(viewGroup);
        viewActions_[index] = action;
    }
}

void MainWindow::createFavoritesMenu(QMenu *favoritesMenu) {
    auto *addFavoriteMenu = new QMenu("&Add folder to Favorites as", favoritesMenu);
    addFavoriteMenu->setObjectName("addFavoriteMenu");
    addFavoriteMenu->menuAction()->setObjectName("addFavoriteMenu");
    for (int index = 0; index < 10; ++index) {
        auto *save = addFavoriteMenu->addAction(QString::number(index));
        save->setObjectName(QStringLiteral("saveBookmark%1Action").arg(index));
        auto *open = new QAction(QString::number(index), favoritesMenu);
        open->setObjectName(QStringLiteral("openBookmark%1Action").arg(index));
        saveBookmarks_[index] = save;
        openBookmarks_[index] = open;
    }
    refreshBookmarks();
}

void MainWindow::createViewOptions(QMenu *viewMenu) {
    auto *sortGroup = new QActionGroup(this);
    const QStringList sortLabels = {"By Name", "By Type", "By Date", "By Size", "Unsorted"};
    for (int index = 0; index < sortLabels.size(); ++index) {
        QAction *action = new QAction(sortLabels[index], viewMenu);
        action->setObjectName(QStringLiteral("sort%1Action").arg(index));
        action->setCheckable(true);
        action->setActionGroup(sortGroup);
        sortActions_[index] = action;
    }
    sortActions_[0]->setChecked(true);
    auto *timeMenu = new QMenu(QStringLiteral("Time"), viewMenu);
    timeMenu->setObjectName(QStringLiteral("timestampMenu"));
    timeMenu->menuAction()->setObjectName("timestampMenu");
    timeMenu->menuAction()->setProperty("nativeCommandID", IDM_VIEW_TIME_POPUP);
    QtBindLocalText(timeMenu->menuAction(), "CommandMenus",
                    QT_TRANSLATE_NOOP("CommandMenus", "Time format"));
    auto *timeGroup = new QActionGroup(timeMenu);
    for (unsigned i = 0; i < 5; ++i) {
        QAction *action = timeMenu->addAction(QString());
        action->setObjectName(QStringLiteral("timestampLevel%1Action").arg(i));
        action->setCheckable(true);
        action->setActionGroup(timeGroup);
        timeActions_[i] = action;
    }
    utcTimeAction_ = timeMenu->addAction(QStringLiteral("UTC"));
    utcTimeAction_->setObjectName(QStringLiteral("timestampUtcAction"));
    utcTimeAction_->setCheckable(true);
    connect(viewMenu, &QMenu::aboutToShow, this, [this, viewMenu, timeMenu] {
        OnMenuActivating(*menuBar(), *viewMenu, 2, app_);
        timeMenu->setTitle(QCoreApplication::translate("CommandMenus", "Time format"));
    });
    fileListSettings_.Load(toolbarSettingsPath());
    flatView_ = new QAction("Flat View", viewMenu);
    flatView_->setObjectName("flatViewAction");
    flatView_->setCheckable(true);
    twoPanelsAction_ = new QAction(QStringLiteral("Two Panels"), viewMenu);
    twoPanelsAction_->setObjectName("twoPanelsAction");
    twoPanelsAction_->setCheckable(true);
    twoPanelsAction_->setChecked(app_.NumPanels == 2);
    createToolbarOptions(viewMenu);
    root_ = new QAction("Open Root Folder", viewMenu);
    root_->setObjectName("openRootAction");
    refresh_ = new QAction("&Refresh", viewMenu);
    refresh_->setObjectName("refreshAction");
    autoRefreshAction_ = new QAction(QStringLiteral("Auto Refresh"), viewMenu);
    autoRefreshAction_->setObjectName(QStringLiteral("autoRefreshAction"));
    autoRefreshAction_->setCheckable(true);
    autoRefreshAction_->setChecked(app_.Get_AutoRefresh_Mode());
    foldersHistory_ = new QAction("Folders History...", viewMenu);
    foldersHistory_->setObjectName("foldersHistoryAction");
}

void MainWindow::applyFlatView(bool enabled) {
    // A user mode change supersedes the current scan. Join its folder loader
    // before source RefreshListCtrl reuses that same IFolderFolder instance.
    projectedUi().filesystemScanner->cancelAndWait();
    cancelListPublication();
    QSettings settings(toolbarSettingsPath(), QSettings::IniFormat);
    {
        const QSignalBlocker block(flatView_);
        flatView_->setChecked(enabled);
    }
    if (!app_.GetFocusedPanel().GetParentFolders().IsEmpty()) {
        SaveFlatView(app_.GetFocusedPanelIndex(), enabled, toolbarSettingsPath());
    } else {
        settings.setValue("view/flatDisk", enabled);
    }
}

void MainWindow::createToolbarOptions(QMenu *viewMenu) {
    auto *toolbarsMenu = new QMenu("&Toolbars", viewMenu);
    toolbarsMenu->setObjectName("toolbarsMenu");
    toolbarsMenu->menuAction()->setObjectName("toolbarsMenu");
    auto addToolbarOption = [this, toolbarsMenu](const QString &title, const char *name,
                                                 bool checked) {
        auto *option = toolbarsMenu->addAction(title);
        option->setObjectName(name);
        option->setCheckable(true);
        option->setChecked(checked);
        return option;
    };
    archiveToolbarOption_ =
        addToolbarOption("Archive Toolbar", "archiveToolbarOption", app_.ShowArchiveToolbar);
    standardToolbarOption_ =
        addToolbarOption("Standard Toolbar", "standardToolbarOption", app_.ShowStandardToolbar);
    largeButtonsOption_ = addToolbarOption("Lar&ge Icons", "largeButtonsOption", app_.LargeButtons);
    buttonTextOption_ =
        addToolbarOption("Show Buttons Text", "buttonTextOption", app_.ShowButtonsLables);
    updateToolbar();
}

void MainWindow::populateSourceMenus() {
    // resource.rc IDM_MENU is the order/grouping contract. Keep existing Qt
    // actions so the source command dispatcher and toolbar share one action.
    // Populate once after the first panel has created the selection actions.
    auto unavailable = [this](QMenu *menu, const QString &label, const char *name, unsigned id) {
        auto *action = new QAction(label, this);
        action->setObjectName(name);
        action->setProperty("nativeCommandID", id);
        action->setEnabled(false);
        menu->addAction(action);
    };

    auto *file = findChild<QMenu *>("fileMenu");
    auto *hash = findChild<QMenu *>("hashMenu");
    auto *openArchive = file->addAction(QString());
    openArchive->setObjectName("openArchiveAction");
    QtBindLocalText(openArchive, "mainwindow_menus",
                    QT_TRANSLATE_NOOP("mainwindow_menus", "Open archive…"));
    auto *openDevice = file->addAction(QString());
    openDevice->setObjectName("openDeviceAction");
    QtBindLocalText(openDevice, "mainwindow_menus",
                    QT_TRANSLATE_NOOP("mainwindow_menus", "Open device…"));
    file->addSeparator();
    for (QAction *action : {openItem_, openInside_, openInsideOne_, openInsideParser_, openOutside_,
                            viewFile_, editFile_})
        file->addAction(action);
    file->addSeparator();
    for (QAction *action : {rename_, copy_, move_, remove_})
        file->addAction(action);
    file->addSeparator();
    file->addAction(split_);
    file->addAction(combine_);
    file->addSeparator();
    file->addAction(properties_);
    unavailable(file, QStringLiteral("Comme&nt..."), "commentAction", IDM_COMMENT);
    file->addMenu(hash);
    diffFiles_ = file->addAction(QStringLiteral("&Diff"));
    diffFiles_->setObjectName("diffAction");
    file->addSeparator();
    file->addAction(createFolder_);
    file->addAction(createFile_);
    file->addSeparator();
    unavailable(file, QStringLiteral("Link"), "linkAction", IDM_LINK);
    unavailable(file, QStringLiteral("&Alternate Streams"), "altStreamsAction", IDM_ALT_STREAMS);
    file->addSeparator();
    file->addAction(exitAction_);
    auto *archives = file->addMenu(tr("Archive"));
    QtBindLocalText(archives->menuAction(), "QtWindowWorkspace",
                    QT_TRANSLATE_NOOP("QtWindowWorkspace", "Archive"));
    archives->menuAction()->setProperty("workspaceCommand", true);
    for (auto *action : {toolbarAdd_, extract_, test_, infoAction_})
        archives->addAction(action);
    const char *verCtrlNames[] = {"verEditAction", "verCommitAction", "verRevertAction",
                                  "verDiffAction"};
    for (unsigned i = 0; i < 4; ++i) {
        auto *action = file->addAction(QString());
        action->setObjectName(QString::fromLatin1(verCtrlNames[i]));
        action->setProperty("nativeCommandID", IDM_VER_EDIT + i);
        action->setVisible(false);
    }

    auto *edit = findChild<QMenu *>("editMenu");
    for (QAction *action : {selectAll_, deselectAll_, invertSelection_, selectMask_, deselectMask_})
        edit->addAction(action);
    edit->addSeparator();
    edit->addAction(selectType_);
    edit->addAction(deselectType_);

    auto *view = findChild<QMenu *>("viewMenu");
    auto *time = findChild<QMenu *>("timestampMenu");
    auto *toolbars = findChild<QMenu *>("toolbarsMenu");
    auto *appearance = view->addMenu(QString());
    appearance->setObjectName("appearanceMenu");
    QtBindLocalText(appearance->menuAction(), "CommandMenus",
                    QT_TRANSLATE_NOOP("CommandMenus", "Presentation"));
    for (QAction *action : viewActions_)
        appearance->addAction(action);
    auto *sort = view->addMenu(QString());
    sort->setObjectName("sortMenu");
    QtBindLocalText(sort->menuAction(), "CommandMenus",
                    QT_TRANSLATE_NOOP("CommandMenus", "Sort by"));
    for (QAction *action : sortActions_)
        sort->addAction(action);
    view->addSeparator();
    view->addAction(flatView_);
    view->addAction(twoPanelsAction_);
    view->addSeparator();
    view->addMenu(time);
    toolbars->menuAction()->setVisible(false);
    view->addSeparator();
    for (QAction *action : {root_, up_, foldersHistory_, refresh_, autoRefreshAction_})
        view->addAction(action);

    auto *favorites = findChild<QMenu *>("favoritesMenu");
    auto *addFavorite = findChild<QMenu *>("addFavoriteMenu");
    favorites->addMenu(addFavorite);
    favorites->addSeparator();
    for (QAction *action : openBookmarks_)
        favorites->addAction(action);

    auto *tools = findChild<QMenu *>("toolsMenu");
    tools->addAction(settingsAction_);
    tools->addSeparator();
    unavailable(tools, QStringLiteral("Benchmark"), "benchmarkAction", IDM_BENCHMARK);
    tools->addSeparator();
    unavailable(tools, QStringLiteral("Delete Temporary Files..."), "tempDirAction", IDM_TEMP_DIR);

    tools->addSeparator();
    unavailable(tools, QStringLiteral("About 7-Zip..."), "aboutAction", IDM_ABOUT);
}
