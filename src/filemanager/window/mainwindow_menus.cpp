#include "7zip/UI/FileManager/resource.h"
#include "Windows/PropVariant.h"
#include "Windows/PropVariantConv.h"
#include "Windows/TimeUtils.h"
#include "filemanager/MyLoadMenu.h"
#include "filemanager/qtfilemanagercommandservices.h"
#include "filemanager/window/mainwindow.h"
#include "localization/qtlangcontrols.h"
#include "ui/theme/qtcommandicons.h"

QMenu *MainWindow::createMenus() {
    auto *menu = menuBar()->addMenu("&File");
    menu->setObjectName("fileMenu");
    createFileActions(menu);
    createFileExtras(menu);
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

void MainWindow::createFileActions(QMenu *menu) {
    back_ = new QAction("Back", this);
    back_->setObjectName("backAction");
    QtBindLocalText(back_, "mainwindow_menus", QT_TRANSLATE_NOOP("mainwindow_menus", "Back"));
    forward_ = new QAction("Forward", this);
    forward_->setObjectName("forwardAction");
    QtBindLocalText(forward_, "mainwindow_menus", QT_TRANSLATE_NOOP("mainwindow_menus", "Forward"));
    up_ = new QAction("Up", this);
    up_->setObjectName("upAction");
    QtBindLocalText(up_, "CommandMenus", QT_TRANSLATE_NOOP("CommandMenus", "Up one level"));
    openItem_ = new QAction("&Open", this);
    openItem_->setObjectName("openItemAction");
    openInside_ = new QAction("Open &Inside", menu);
    openInside_->setObjectName("openInsideAction");
    openOutside_ = new QAction("Open O&utside", menu);
    openOutside_->setObjectName("openOutsideAction");
    toolbarAdd_ = new QAction("Add", menu);
    toolbarAdd_->setObjectName("toolbarAddAction");
    extract_ = new QAction("Extract", menu);
    extract_->setObjectName("extractArchiveAction");
    test_ = new QAction("Test", menu);
    test_->setObjectName("testArchiveAction");
    copy_ = new QAction("Copy", menu);
    copy_->setObjectName("copyAction");
    move_ = new QAction("Move", menu);
    move_->setObjectName("moveAction");
    remove_ = new QAction("&Delete", menu);
    remove_->setObjectName("deleteAction");
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

void MainWindow::createFileExtras(QMenu *menu) {
    auto *hashMenu = new QMenu("CRC", menu);
    hashMenu->setObjectName("hashMenu");
    hashMenu->menuAction()->setObjectName("hashMenu");
    QtBindLocalText(hashMenu->menuAction(), "CommandMenus",
                    QT_TRANSLATE_NOOP("CommandMenus", "Checksums"));
    const struct {
        unsigned ID;
        const char *Label;
    } hashes[] = {{IDM_CRC32, "CRC-32"},     {IDM_CRC64, "CRC-64"},   {IDM_XXH64, "XXH64"},
                  {IDM_MD5, "MD5"},          {IDM_SHA1, "SHA-1"},     {IDM_SHA256, "SHA-256"},
                  {IDM_SHA384, "SHA-384"},   {IDM_SHA512, "SHA-512"}, {IDM_SHA3_256, "SHA3-256"},
                  {IDM_BLAKE2SP, "BLAKE2sp"}};
    for (const auto &hash : hashes) {
        auto *action = hashMenu->addAction(QString::fromLatin1(hash.Label));
        action->setObjectName(QStringLiteral("hash%1Action")
                                  .arg(QString::fromLatin1(GetHashMethodForCommand(hash.ID))));
        hashActions_.insert(hash.ID, action);
    }
    createFolder_ = new QAction("Create Folder", menu);
    createFolder_->setObjectName("createFolderAction");
    infoAction_ = new QAction("Info", menu);
    infoAction_->setObjectName("archiveInfoAction");
    toolbarAdd_->setIcon(QtCommandSymbol(QtCommandIcon::Add));
    extract_->setIcon(QtCommandSymbol(QtCommandIcon::Extract));
    test_->setIcon(QtCommandSymbol(QtCommandIcon::Test));
    copy_->setIcon(QtCommandSymbol(QtCommandIcon::Copy));
    move_->setIcon(QtCommandSymbol(QtCommandIcon::Move));
    remove_->setIcon(QtCommandSymbol(QtCommandIcon::Delete));
    infoAction_->setIcon(QtCommandSymbol(QtCommandIcon::Info));
}

void MainWindow::createViewModes(QMenu *viewMenu) {
    auto *viewGroup = new QActionGroup(this);
    for (const auto &[index, label] : {std::pair{0, "Lar&ge Icons"}, std::pair{3, "Details"}}) {
        QAction *action = new QAction(label, viewMenu);
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
    const QStringList sortLabels = {"By Name", "By Type", "By Date", "By Size"};
    for (int index = 0; index < sortLabels.size(); ++index) {
        QAction *action = new QAction(sortLabels[index], viewMenu);
        action->setObjectName(QStringLiteral("sort%1Action").arg(index));
        action->setCheckable(true);
        action->setActionGroup(sortGroup);
        sortActions_[index] = action;
    }
    sortActions_[0]->setChecked(true);
    connect(viewMenu, &QMenu::aboutToShow, this,
            [this, viewMenu] { OnMenuActivating(*menuBar(), *viewMenu, 2, app_); });
    fileListSettings_.Load(toolbarSettingsPath());
    twoPanelsAction_ = new QAction(QStringLiteral("Two Panels"), viewMenu);
    twoPanelsAction_->setObjectName("twoPanelsAction");
    QtBindLocalText(twoPanelsAction_, "CommandMenus",
                    QT_TRANSLATE_NOOP("CommandMenus", "Two panels"));
    twoPanelsAction_->setCheckable(true);
    twoPanelsAction_->setChecked(app_.NumPanels == 2);
    root_ = new QAction("Open Root Folder", viewMenu);
    root_->setObjectName("openRootAction");
    QtBindLocalText(root_, "CommandMenus", QT_TRANSLATE_NOOP("CommandMenus", "Computer"));
    refresh_ = new QAction("&Refresh", viewMenu);
    refresh_->setObjectName("refreshAction");
    foldersHistory_ = new QAction("Folders History...", viewMenu);
    foldersHistory_->setObjectName("foldersHistoryAction");
    QtBindLocalText(foldersHistory_, "CommandMenus",
                    QT_TRANSLATE_NOOP("CommandMenus", "Recent places…"));
}

void MainWindow::populateSourceMenus() {
    // resource.rc IDM_MENU is the order/grouping contract. Keep existing Qt
    // actions so the source command dispatcher can reuse the menu actions.
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
    for (QAction *action : {openItem_, openInside_, openOutside_, viewFile_, editFile_})
        file->addAction(action);
    file->addSeparator();
    for (QAction *action : {rename_, copy_, move_, remove_})
        file->addAction(action);
    file->addSeparator();
    file->addSeparator();
    file->addAction(properties_);
    file->addMenu(hash);
    file->addSeparator();
    file->addAction(createFolder_);
    file->addSeparator();
    unavailable(file, QStringLiteral("Link"), "linkAction", IDM_LINK);
    file->addSeparator();
    file->addAction(exitAction_);
    auto *archives = file->addMenu(tr("Archive"));
    QtBindLocalText(archives->menuAction(), "QtWindowWorkspace",
                    QT_TRANSLATE_NOOP("QtWindowWorkspace", "Archive"));
    archives->menuAction()->setProperty("workspaceCommand", true);
    for (auto *action : {toolbarAdd_, extract_, test_, infoAction_})
        archives->addAction(action);
    auto *edit = findChild<QMenu *>("editMenu");
    edit->addAction(selectAll_);
    edit->addAction(deselectAll_);

    auto *view = findChild<QMenu *>("viewMenu");
    auto *appearance = view->addMenu(QString());
    appearance->setObjectName("appearanceMenu");
    QtBindLocalText(appearance->menuAction(), "CommandMenus",
                    QT_TRANSLATE_NOOP("CommandMenus", "Presentation"));
    appearance->addAction(viewActions_[0]);
    appearance->addAction(viewActions_[3]);
    auto *sort = view->addMenu(QString());
    sort->setObjectName("sortMenu");
    QtBindLocalText(sort->menuAction(), "CommandMenus",
                    QT_TRANSLATE_NOOP("CommandMenus", "Sort by"));
    for (QAction *action : sortActions_)
        sort->addAction(action);
    sort->addSeparator();
    auto *direction = new QActionGroup(sort);
    for (const auto &[name, text] :
         {std::pair{"sortAscendingAction", QT_TRANSLATE_NOOP("CommandMenus", "Ascending")},
          std::pair{"sortDescendingAction", QT_TRANSLATE_NOOP("CommandMenus", "Descending")}}) {
        auto *action = sort->addAction(QString());
        action->setObjectName(QString::fromLatin1(name));
        QtBindLocalText(action, "CommandMenus", text);
        action->setCheckable(true);
        direction->addAction(action);
    }
    view->addSeparator();
    view->addAction(twoPanelsAction_);
    view->addSeparator();
    for (QAction *action : {back_, forward_, up_, root_, foldersHistory_})
        view->addAction(action);
    view->addSeparator();
    view->addAction(refresh_);

    auto *favorites = findChild<QMenu *>("favoritesMenu");
    auto *addFavorite = findChild<QMenu *>("addFavoriteMenu");
    favorites->addMenu(addFavorite);
    favorites->addSeparator();
    for (QAction *action : openBookmarks_)
        favorites->addAction(action);

    auto *tools = findChild<QMenu *>("toolsMenu");
    tools->addAction(settingsAction_);
    tools->addSeparator();
    unavailable(tools, QStringLiteral("Delete Temporary Files..."), "tempDirAction", IDM_TEMP_DIR);

    tools->addSeparator();
    unavailable(tools, QStringLiteral("About 7-Zip..."), "aboutAction", IDM_ABOUT);
}
