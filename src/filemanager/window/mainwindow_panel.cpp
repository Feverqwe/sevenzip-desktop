#include "7zip/PropID.h"
#include "filemanager/PropertyName.h"
#include "filemanager/panels/qtpaneladdresscombo.h"
#include "filemanager/panels/qtpanelcontextmenu.h"
#include "filemanager/qtfilemanagerdrag.h"
#include "filemanager/rename/qtrenamecontroller.h"
#include "filemanager/views/qtfilelisthover.h"
#include "filemanager/views/qtworkspacefiletable.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtapppresentation.h"
#include "filemanager/window/qtmainwindowchrome.h"
#include "filemanager/window/qtnavigationbar.h"
#include "filemanager/window/qtwindowworkspace.h"
#include "filemanager/window/qtworkspacetheme.h"
#include "localization/qtlangcontrols.h"
#include "ui/controls/qtstatuslabel.h"
#include "ui/theme/qtcommandicons.h"

void MainWindow::createFilePanel(QMenu *editMenu, unsigned index) {
    QSettings settings(toolbarSettingsPath(), QSettings::IniFormat);
    auto *central = new QWidget(this);
    QtWorkspaceTheme::pane(*central);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto *navigationRow = new QtNavigationRow(central);
    auto *navigation = new QHBoxLayout(navigationRow);
    navigation->setContentsMargins(8, 8, 8, 8);
    navigation->setSpacing(4);
    auto *back = new QToolButton(central);
    back->setObjectName(QStringLiteral("panelBackButton%1").arg(index));
    back->setAutoRaise(true);
    QtConfigureChromeToolButton(*back);
    back->setIcon(QtCommandSymbol(QtCommandIcon::Back));
    QtBindLocalText(back, "mainwindow_menus", QT_TRANSLATE_NOOP("mainwindow_menus", "Back"), true);
    QtBindLocalToolTip(back, "mainwindow_menus", QT_TRANSLATE_NOOP("mainwindow_menus", "Back"));
    connect(back, &QToolButton::clicked, this, [this, index] {
        activatePanelUi(index);
        if (projectedPanel_ == index)
            back_->trigger();
    });
    navigation->addWidget(back);
    auto *forward = new QToolButton(central);
    forward->setObjectName(QStringLiteral("panelForwardButton%1").arg(index));
    forward->setAutoRaise(true);
    QtConfigureChromeToolButton(*forward);
    forward->setIcon(QtCommandSymbol(QtCommandIcon::Forward));
    QtBindLocalText(forward, "mainwindow_menus", QT_TRANSLATE_NOOP("mainwindow_menus", "Forward"),
                    true);
    QtBindLocalToolTip(forward, "mainwindow_menus",
                       QT_TRANSLATE_NOOP("mainwindow_menus", "Forward"));
    connect(forward, &QToolButton::clicked, this, [this, index] {
        activatePanelUi(index);
        if (projectedPanel_ == index)
            forward_->trigger();
    });
    navigation->addWidget(forward);
    {
        auto *button = new QPushButton(central);
        // Keep the source parent command without its longer View menu caption.
        const auto updateButton = [this, button] {
            button->setIcon(QtCommandSymbol(QtCommandIcon::Parent));
            button->setAccessibleName(up_->toolTip());
            button->setToolTip(up_->toolTip());
        };
        updateButton();
        // Native panel toolbar buttons belong to their panel, independently of
        // the focused panel's View menu. Reuse command gating after activation.
        connect(up_, &QAction::changed, button, updateButton);
        connect(button, &QPushButton::clicked, this, [this, index] {
            appPresentation_->CancelInitialFocus();
            activatePanelUi(index);
            if (projectedPanel_ == index)
                up_->trigger();
        });
        QtConfigurePanelParentButton(*button);
        button->setObjectName(QString("panelParentButton%1").arg(index));
        navigation->addWidget(button);
    }
    projectedUi().pathCombo = new QtPanelAddressCombo(
        app_.Panels[index],
        [this, index](const QString &path, bool sourcePath) {
            if (app_.QtOperations().closed())
                return E_ABORT;
            activatePanelUi(index);
            return navigateTo(path, sourcePath);
        },
        central, [this, index] { appPresentation_->RequestListFocus(index); });
    projectedUi().pathCombo->setObjectName("pathCombo");
    projectedUi().pathCombo->setEditable(true);
    projectedUi().pathCombo->setInsertPolicy(QComboBox::NoInsert);
    projectedUi().pathCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    projectedUi().pathCombo->setMinimumContentsLength(16);
    projectedUi().path = projectedUi().pathCombo->lineEdit();
    projectedUi().path->setObjectName("archivePath");
    projectedUi().path->installEventFilter(this);
    projectedUi().pathCombo->installEventFilter(this);
    projectedUi().path->setProperty("languagePlaceholder", true);
    QtBindLocalText(projectedUi().path, "mainwindow_panel",
                    QT_TRANSLATE_NOOP("mainwindow_panel", "Path to folder or archive"), true);
    projectedUi().navigation = new QtNavigationBar(
        *projectedUi().pathCombo,
        [this, index](const QString &path) {
            activatePanelUi(index);
            navigateTo(path, true);
        },
        navigationRow,
        [this, index] {
            static_cast<QtPanelAddressCombo *>(panelUi_[index].pathCombo)->CancelPendingListFocus();
            appPresentation_->CancelInitialFocus();
            activatePanelUi(index);
        });
    projectedUi().navigation->setObjectName(QStringLiteral("panelNavigation%1").arg(index));
    navigation->addWidget(projectedUi().navigation);
    auto *recent = new QToolButton(navigationRow);
    projectedUi().recentPlaces = recent;
    recent->setObjectName(QStringLiteral("panelRecentPlacesButton%1").arg(index));
    recent->setAutoRaise(true);
    QtConfigureChromeToolButton(*recent);
    recent->setIcon(QtCommandSymbol(QtCommandIcon::History));
    QtBindLocalText(recent, "CommandMenus", QT_TRANSLATE_NOOP("CommandMenus", "Recent places…"),
                    true);
    QtBindLocalToolTip(recent, "CommandMenus", QT_TRANSLATE_NOOP("CommandMenus", "Recent places…"));
    connect(recent, &QToolButton::clicked, this,
            [this, index, recent] { workspace_->ShowRecentPlaces(index, *recent); });
    navigation->addWidget(recent);
    projectedUi().readOnlyIndicator = new QLabel(navigationRow);
    projectedUi().readOnlyIndicator->setObjectName("deviceReadOnlyLabel");
    projectedUi().readOnlyIndicator->hide();
    navigation->addWidget(projectedUi().readOnlyIndicator);
    layout->addWidget(navigationRow);
    workspace_->AttachPanel(index, *layout);
    createFileTable(central);
    if (!panelSplitter_)
        createPanelActions(editMenu);
    createIconList(central);
    projectedUi().viewStack = new QStackedWidget(central);
    projectedUi().viewStack->setObjectName("fileViewStack");
    projectedUi().viewStack->addWidget(projectedUi().table);
    projectedUi().viewStack->addWidget(projectedUi().iconList);
    workspace_->AttachContent(index, *layout);
    connect(projectedUi().table->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this, index] {
                if (projectedPanel_ == index && app_.Panels[index].ShouldProcessNotifications())
                    refreshActions();
            });
    connect(projectedUi().table->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this, index] {
                if (projectedPanel_ == index && app_.Panels[index].ShouldProcessNotifications())
                    refreshActions();
            });
    if (!panelSplitter_) {
        panelSplitter_ = new QtFileManagerSplitter(this);
        panelSplitter_->setHandleWidth(8);
        panelSplitter_->setObjectName("filePanelSplitter");
        setCentralWidget(panelSplitter_);
    }
    panelUi_[index].container = central;
    central->setObjectName(QStringLiteral("filePanel%1").arg(index));
    panelSplitter_->insertWidget(static_cast<int>(index), central);
    projectedUi().table->addActions(panelShortcutActions_);
    projectedUi().iconList->addActions(panelShortcutActions_);
    app_.GetFocusedPanel().SetListViewMode(app_.GetFocusedPanel().GetListViewMode());
}

void MainWindow::createFileTable(QWidget *central) {
    projectedUi().table = new QtWorkspaceFileTable(toolbarSettingsPath(), central);
    auto *fileTable = static_cast<FileTable *>(projectedUi().table);
    fileTable->setFullRowHighlight(fileListSettings_.FullRow);
    fileTable->sourcePanel = &app_.GetFocusedPanel();
    fileTable->setColumnProperties(
        {kpidName, kpidPath, kpidSize, kpidPackSize, kpidMTime, kpidExtension});
    projectedUi().table->setObjectName("archiveTable");
    QtBindLocalText(projectedUi().table, "mainwindow_panel",
                    QT_TRANSLATE_NOOP("mainwindow_panel", "File list"), true);
    projectedUi().table->verticalHeader()->hide();
    projectedUi().table->panelModel()->setColumns(
        {"Name", "Path", "Size", "Packed Size", "Modified", "Type"});
    projectedUi().table->setColumnHidden(1, true);
    projectedUi().table->setColumnHidden(5, true);
    projectedUi().table->horizontalHeader()->setSectionsMovable(true);
    projectedUi().table->setColumnWidth(0, 160);
    projectedUi().table->setColumnWidth(2, 100);
    projectedUi().table->setColumnWidth(3, 100);
    projectedUi().table->setColumnWidth(4, 150);
    projectedUi().table->setShowGrid(fileListSettings_.ShowGrid);
    projectedUi().table->setWordWrap(false);
    projectedUi().table->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    const unsigned columnOwner = projectedPanel_;
    connect(projectedUi().table->horizontalHeader(), &QWidget::customContextMenuRequested, this,
            [this, columnOwner](const QPoint &pos) {
                const QPointer<QWidget> focused = QApplication::focusWidget();
                withPanelProjection(columnOwner, [this, pos] {
                    QtPanelContextMenu menu(*this, *projectedUi().table);
                    auto *header = projectedUi().table->horizontalHeader();
                    const QPoint point = header->mapToGlobal(pos);
                    app_.GetFocusedPanel().OnContextMenu(header, point.x(), point.y(), menu);
                });
                // Popup destruction can restore window focus while another
                // panel is projected. Restore the caller after that scope ends.
                if (focused)
                    focused->setFocus(Qt::PopupFocusReason);
            });
    connect(projectedUi().table->horizontalHeader(), &QHeaderView::sectionResized, this,
            [this, columnOwner] { withPanelProjection(columnOwner, [this] { saveColumns(); }); });
    connect(projectedUi().table->horizontalHeader(), &QHeaderView::sectionMoved, this,
            [this, columnOwner] { withPanelProjection(columnOwner, [this] { saveColumns(); }); });
    projectedUi().table->setSelectionBehavior(QAbstractItemView::SelectRows);
    projectedUi().table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    projectedUi().tableHover = new QtFileListHover(*projectedUi().table);
    projectedUi().tableHover->SetEnabled(fileListSettings_.SingleClick);
    projectedUi().table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    renameController_->Attach(*projectedUi().table, projectedPanel_);
    projectedUi().table->setSortingEnabled(true);
    projectedUi().table->setDragEnabled(true);
    projectedUi().table->setAcceptDrops(true);
    projectedUi().table->viewport()->setAcceptDrops(true);
    projectedUi().table->sortItems(0, Qt::AscendingOrder);
    connect(projectedUi().table->horizontalHeader(), &QHeaderView::sortIndicatorChanged, this,
            [this, owner = projectedPanel_](int column, Qt::SortOrder order) {
                sortIndicatorChanged(owner, column, order);
            });
    connectFileView(*projectedUi().table);
    app_.GetFocusedPanel().AttachSelectionView(
        fileTable,
        [fileTable](PROPID property, Int32) { return fileTable->columnForProperty(property); },
        [this, owner = projectedPanel_](int row) {
            withPanelProjection(owner, [this, row] { renameController_->Begin(row); });
        });
}

void MainWindow::createPanelActions(QMenu *editMenu) {
    projectedUi().table->addAction(openItem_);
    parent_ = new QAction(this);
    parent_->setObjectName("parentFolderAction");
    projectedUi().table->addAction(parent_);
    selectAll_ = new QAction(this);
    selectAll_->setObjectName("selectAllAction");
    projectedUi().table->addAction(selectAll_);
    selectAll_->setText("Select All");
    deselectAll_ = new QAction("Deselect All", editMenu);
    deselectAll_->setObjectName("deselectAllAction");
    populateSourceMenus();
}

void MainWindow::createIconList(QWidget *central) {
    projectedUi().iconList = new FileIconList(central);
    QtWorkspaceTheme::fileView(*projectedUi().iconList);
    projectedUi().iconList->setObjectName("iconList");
    QtBindLocalText(projectedUi().iconList, "mainwindow_panel",
                    QT_TRANSLATE_NOOP("mainwindow_panel", "File list"), true);
    projectedUi().iconList->setModel(projectedUi().table->model());
    projectedUi().iconList->setItemDelegate(CreateFileRowDelegate(projectedUi().iconList, true));
    projectedUi().iconList->setModelColumn(0);
    projectedUi().iconList->setSelectionModel(projectedUi().table->selectionModel());
    projectedUi().iconList->setSelectionMode(fileListSettings_.AlternativeSelection
                                                 ? QAbstractItemView::SingleSelection
                                                 : QAbstractItemView::ExtendedSelection);
    projectedUi().iconList->setSelectionBehavior(QAbstractItemView::SelectRows);
    projectedUi().iconHover = new QtFileListHover(*projectedUi().iconList);
    projectedUi().iconHover->SetEnabled(fileListSettings_.SingleClick);
    projectedUi().iconList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    renameController_->Attach(*projectedUi().iconList, projectedPanel_);
    projectedUi().iconList->setUniformItemSizes(true);
    projectedUi().iconList->setSelectionRectVisible(true);
    projectedUi().iconList->setWordWrap(false);
    projectedUi().iconList->setDragEnabled(true);
    projectedUi().iconList->setAcceptDrops(true);
    projectedUi().iconList->viewport()->setAcceptDrops(true);
    projectedUi().iconList->addAction(selectAll_);
    projectedUi().iconList->addAction(openItem_);
    projectedUi().iconList->addAction(parent_);
    connectFileView(*projectedUi().iconList);
}

void MainWindow::connectFileView(QAbstractItemView &view) {
    view.setContextMenuPolicy(Qt::CustomContextMenu);
    view.installEventFilter(this);
    view.viewport()->installEventFilter(this);
    connect(&view, &QAbstractItemView::clicked, this,
            [this](const QModelIndex &index) { handleSelectionClick(index.row()); });
    connect(&view, &QAbstractItemView::doubleClicked, this, [this](const QModelIndex &index) {
        if (!fileListSettings_.SingleClick)
            activateRow(index.row(), selectionClickModifiers_);
    });
    connect(&view, &QAbstractItemView::clicked, this,
            [this](const QModelIndex &index) { activateSingleClickedRow(index.row()); });
    connect(&view, &QWidget::customContextMenuRequested, this,
            [this, owner = projectedPanel_, view = &view](const QPoint &pos) {
                showItemContextMenu(owner, view, pos);
            });
    const QPointer<MainWindow> host(this);
    const unsigned owner = projectedPanel_;
    const auto activate = [host, owner] {
        if (!host || !host->isVisible() || host->isPanelEditorFocused())
            return false;
        host->activatePanelUi(owner);
        if (host->projectedPanel_ != owner)
            return false;
        host->renameController_->Cancel(owner);
        return true;
    };
    const QPointer<QtFileListHover> hover(&view == projectedUi().table ? projectedUi().tableHover
                                                                       : projectedUi().iconHover);
    const auto activity = [hover](bool active) {
        if (hover)
            hover->SetSuspended(active);
    };
    if (auto *table = dynamic_cast<FileTable *>(&view))
        table->InstallSelectionGesture(activate, activity);
    else if (auto *icons = dynamic_cast<FileIconList *>(&view))
        icons->InstallSelectionGesture(activate, activity);
}

void MainWindow::appendParentEntry() {
    if (!app_.GetFocusedPanel()._showDots || app_.GetFocusedPanel().IsRootFolder())
        return;
    const bool sorting = projectedUi().table->isSortingEnabled();
    projectedUi().table->setSortingEnabled(false);
    FilePanelModel::Row row;
    row.name = QStringLiteral("..");
    row.directory = true;
    row.icon = QApplication::style()->standardIcon(QStyle::SP_DirIcon);
    row.toolTip = QCoreApplication::translate("mainwindow_panel", "Parent folder");
    projectedUi().table->panelModel()->append(std::move(row));
    projectedUi().table->setSortingEnabled(sorting);
}

void MainWindow::activateSingleClickedRow(int row) {
    if (drag_ && drag_->Active())
        return;
    // Selection modifiers must keep the Qt multi-selection gesture intact.
    const auto modifiers = selectionClickModifiers_;
    if (fileListSettings_.SingleClick &&
        !(modifiers & (Qt::ControlModifier | Qt::MetaModifier | Qt::ShiftModifier)))
        activateRow(row, modifiers);
}
