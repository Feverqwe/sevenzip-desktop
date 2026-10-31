#include "filemanager/PropertyName.h"
#include "filemanager/panels/PanelSort.h"
#include "filemanager/panels/qtpaneladdresscombo.h"
#include "filemanager/panels/qtpanelcontextmenu.h"
#include "filemanager/qtfilemanagerdrag.h"
#include "filemanager/views/qtfilelisthover.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtapppresentation.h"
#include "filemanager/window/qtmainwindowchrome.h"
#include "localization/qtlangcontrols.h"
#include "ui/controls/qtstatuslabel.h"
#include "ui/theme/qtcommandicons.h"

void MainWindow::createFilePanel(QMenu *editMenu, unsigned index) {
    QSettings settings(toolbarSettingsPath(), QSettings::IniFormat);
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto *navigation = new QHBoxLayout;
    navigation->setContentsMargins(16, 12, 16, 12);
    navigation->setSpacing(8);
    {
        auto *button = new QPushButton(central);
        // Keep the source parent command without its longer View menu caption.
        const auto updateButton = [this, button] {
            button->setIcon(QtCommandSymbol(QtCommandIcon::Parent));
            button->setEnabled(up_->isEnabled());
            button->setAccessibleName(up_->toolTip());
            button->setToolTip(up_->toolTip());
        };
        updateButton();
        // Project the existing command state and caption; the button itself
        // does not own navigation or command availability.
        connect(up_, &QAction::changed, button, updateButton);
        connect(button, &QPushButton::clicked, up_, &QAction::trigger);
        QtConfigurePanelParentButton(*button);
        button->setObjectName(QString("panelParentButton%1").arg(index));
        navigation->addWidget(button);
    }
    pathCombo_ = new QtPanelAddressCombo(
        app_.Panels[index],
        [this, index](const QString &path, bool sourcePath) {
            if (app_.QtOperations().closed())
                return E_ABORT;
            activatePanelUi(index);
            return navigateTo(path, sourcePath);
        },
        central, [this, index] { appPresentation_->RequestListFocus(index); });
    pathCombo_->setObjectName("pathCombo");
    pathCombo_->setEditable(true);
    pathCombo_->setInsertPolicy(QComboBox::NoInsert);
    pathCombo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    pathCombo_->setMinimumContentsLength(16);
    path_ = pathCombo_->lineEdit();
    path_->setObjectName("archivePath");
    path_->installEventFilter(this);
    pathCombo_->installEventFilter(this);
    path_->setProperty("languagePlaceholder", true);
    QtBindLocalText(path_, "mainwindow_panel",
                    QT_TRANSLATE_NOOP("mainwindow_panel", "Path to folder or archive"), true);
    navigation->addWidget(pathCombo_);
    layout->addLayout(navigation);
    createFileTable(central);
    if (!panelSplitter_)
        createPanelActions(editMenu);
    createIconList(central);
    viewStack_ = new QStackedWidget(central);
    viewStack_->setObjectName("fileViewStack");
    viewStack_->addWidget(table_);
    viewStack_->addWidget(iconList_);
    layout->addWidget(viewStack_);
    auto *panelStatus = new QStatusBar(central);
    panelStatus->setObjectName("panelStatusBar");
    panelStatus->setSizeGripEnabled(false);
    const char *statusNames[] = {"selectedItemsStatus", "selectedSizeStatus", "focusedSizeStatus",
                                 "focusedDateStatus"};
    for (unsigned part = 0; part < 4; ++part) {
        auto *label = new QtStatusLabel(panelStatus);
        label->setObjectName(statusNames[part]);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        panelStatus->addPermanentWidget(label, part == 0 ? 1 : 0);
        panelUi_[index].statusLabels[part] = label;
    }
    layout->addWidget(panelStatus);
    connect(table_->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this, index] {
        if (projectedPanel_ == index && app_.Panels[index].ShouldProcessNotifications())
            refreshActions();
    });
    connect(table_->selectionModel(), &QItemSelectionModel::currentChanged, this, [this, index] {
        if (projectedPanel_ == index && app_.Panels[index].ShouldProcessNotifications())
            refreshActions();
    });
    if (!panelSplitter_) {
        panelSplitter_ = new QtFileManagerSplitter(this);
        panelSplitter_->setObjectName("filePanelSplitter");
        setCentralWidget(panelSplitter_);
    }
    panelUi_[index].container = central;
    central->setObjectName(QStringLiteral("filePanel%1").arg(index));
    panelSplitter_->insertWidget(static_cast<int>(index), central);
    table_->addActions(panelShortcutActions_);
    iconList_->addActions(panelShortcutActions_);
    app_.GetFocusedPanel().SetListViewMode(app_.GetFocusedPanel().GetListViewMode());
    statusBar()->setSizeGripEnabled(false);
    if (!deviceReadOnly_) {
        auto *feedback = statusBar();
        feedback->setObjectName("operationStatusBar");
        deviceReadOnly_ = new QLabel(feedback);
        deviceReadOnly_->setObjectName("deviceReadOnlyLabel");
        QtBindLocalText(deviceReadOnly_, "mainwindow_panel",
                        QT_TRANSLATE_NOOP("mainwindow_panel", "Read only"));
        feedback->addPermanentWidget(deviceReadOnly_);
        deviceReadOnly_->hide();
        feedback->setVisible(!feedback->currentMessage().isEmpty());
        connect(feedback, &QStatusBar::messageChanged, feedback,
                [this, feedback](const QString &message) {
                    feedback->setVisible(!message.isEmpty() || !deviceReadOnly_->isHidden());
                });
    }
    connect(table_, &QTableView::doubleClicked, this, [this](const QModelIndex &index) {
        const int row = index.row();
        if (!fileListSettings_.SingleClick)
            activateRow(row, selectionClickModifiers_);
    });
    connect(table_, &QTableView::clicked, this, [this](const QModelIndex &index) {
        const int row = index.row();
        activateSingleClickedRow(row);
    });
}

void MainWindow::createFileTable(QWidget *central) {
    table_ = new FileTable(7, central);
    auto *fileTable = static_cast<FileTable *>(table_);
    fileTable->setFullRowHighlight(fileListSettings_.FullRow);
    fileTable->sourcePanel = &app_.GetFocusedPanel();
    fileTable->setColumnProperties(
        {kpidName, kpidPath, kpidSize, kpidPackSize, kpidMTime, kpidExtension, kpidNoProperty});
    table_->setObjectName("archiveTable");
    QtBindLocalText(table_, "mainwindow_panel", QT_TRANSLATE_NOOP("mainwindow_panel", "File list"),
                    true);
    table_->verticalHeader()->hide();
    table_->panelModel()->setColumns(
        {"Name", "Path", "Size", "Packed Size", "Modified", "Type", "Order"});
    table_->setColumnHidden(1, true);
    table_->setColumnHidden(5, true);
    table_->setColumnHidden(6, true);
    table_->horizontalHeader()->setSectionsMovable(true);
    table_->setColumnWidth(0, 160);
    table_->setColumnWidth(2, 100);
    table_->setColumnWidth(3, 100);
    table_->setColumnWidth(4, 150);
    table_->setShowGrid(fileListSettings_.ShowGrid);
    table_->setWordWrap(false);
    table_->setTextElideMode(Qt::ElideMiddle);
    table_->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    const unsigned columnOwner = projectedPanel_;
    connect(table_->horizontalHeader(), &QWidget::customContextMenuRequested, this,
            [this, columnOwner](const QPoint &pos) {
                const QPointer<QWidget> focused = QApplication::focusWidget();
                withPanelProjection(columnOwner, [this, pos] {
                    QtPanelContextMenu menu(*this, *table_);
                    auto *header = table_->horizontalHeader();
                    const QPoint point = header->mapToGlobal(pos);
                    app_.GetFocusedPanel().OnContextMenu(header, point.x(), point.y(), menu);
                });
                // Popup destruction can restore window focus while another
                // panel is projected. Restore the caller after that scope ends.
                if (focused)
                    focused->setFocus(Qt::PopupFocusReason);
            });
    connect(table_->horizontalHeader(), &QHeaderView::sectionResized, this,
            [this, columnOwner] { withPanelProjection(columnOwner, [this] { saveColumns(); }); });
    connect(table_->horizontalHeader(), &QHeaderView::sectionMoved, this,
            [this, columnOwner] { withPanelProjection(columnOwner, [this] { saveColumns(); }); });
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tableHover_ = new QtFileListHover(*table_);
    tableHover_->SetEnabled(fileListSettings_.SingleClick);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connectRenameEditor(*table_);
    table_->setSortingEnabled(true);
    table_->setDragEnabled(true);
    table_->setAcceptDrops(true);
    table_->viewport()->setAcceptDrops(true);
    table_->sortItems(0, Qt::AscendingOrder);
    connect(table_->horizontalHeader(), &QHeaderView::sortIndicatorChanged, this,
            [this, owner = projectedPanel_](int column, Qt::SortOrder order) {
                sortIndicatorChanged(owner, column, order);
            });
    table_->installEventFilter(this);
    table_->viewport()->installEventFilter(this);
    connect(table_, &QTableView::clicked, this, [this](const QModelIndex &index) {
        const int row = index.row();
        handleSelectionClick(row);
    });
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table_, &QWidget::customContextMenuRequested, this,
            [this, owner = projectedPanel_, view = table_](const QPoint &pos) {
                showItemContextMenu(owner, view, pos);
            });
    app_.GetFocusedPanel().AttachSelectionView(
        fileTable,
        [fileTable](PROPID property, Int32) { return fileTable->columnForProperty(property); },
        [this, owner = projectedPanel_](int row) {
            withPanelProjection(owner, [this, row] { beginRename(row); });
        });
}

void MainWindow::createPanelActions(QMenu *editMenu) {
    table_->addAction(openItem_);
    parent_ = new QAction(table_);
    parent_->setObjectName("parentFolderAction");
    table_->addAction(parent_);
    selectAll_ = new QAction(table_);
    selectAll_->setObjectName("selectAllAction");
    table_->addAction(selectAll_);
    selectAll_->setText("Select All");
    editMenu->addAction(selectAll_);
    deselectAll_ = editMenu->addAction("Deselect All");
    deselectAll_->setObjectName("deselectAllAction");
    invertSelection_ = editMenu->addAction("Invert Selection");
    invertSelection_->setObjectName("invertSelectionAction");
    selectMask_ = editMenu->addAction("Select...");
    selectMask_->setObjectName("selectMaskAction");
    deselectMask_ = editMenu->addAction("Deselect...");
    deselectMask_->setObjectName("deselectMaskAction");
    selectType_ = editMenu->addAction("Select by Type");
    selectType_->setObjectName("selectTypeAction");
    deselectType_ = editMenu->addAction("Deselect by Type");
    deselectType_->setObjectName("deselectTypeAction");
    rebuildSourceMenus();
}

void MainWindow::createIconList(QWidget *central) {
    iconList_ = new FileIconList(central);
    iconList_->setObjectName("iconList");
    QtBindLocalText(iconList_, "mainwindow_panel",
                    QT_TRANSLATE_NOOP("mainwindow_panel", "File list"), true);
    iconList_->setModel(table_->model());
    iconList_->setItemDelegate(CreateFileRowDelegate(iconList_));
    iconList_->setModelColumn(0);
    iconList_->setSelectionModel(table_->selectionModel());
    iconList_->setSelectionMode(fileListSettings_.AlternativeSelection
                                    ? QAbstractItemView::SingleSelection
                                    : QAbstractItemView::ExtendedSelection);
    iconList_->setSelectionBehavior(QAbstractItemView::SelectRows);
    iconHover_ = new QtFileListHover(*iconList_);
    iconHover_->SetEnabled(fileListSettings_.SingleClick);
    iconList_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connectRenameEditor(*iconList_);
    iconList_->setUniformItemSizes(true);
    iconList_->setWordWrap(false);
    iconList_->setDragEnabled(true);
    iconList_->setAcceptDrops(true);
    iconList_->viewport()->setAcceptDrops(true);
    iconList_->setContextMenuPolicy(Qt::CustomContextMenu);
    iconList_->installEventFilter(this);
    iconList_->viewport()->installEventFilter(this);
    connect(iconList_, &QListView::clicked, this,
            [this](const QModelIndex &index) { handleSelectionClick(index.row()); });
    iconList_->addAction(selectAll_);
    iconList_->addAction(openItem_);
    iconList_->addAction(parent_);
    connect(iconList_, &QListView::doubleClicked, this, [this](const QModelIndex &index) {
        if (!fileListSettings_.SingleClick)
            activateRow(index.row(), selectionClickModifiers_);
    });
    connect(iconList_, &QListView::clicked, this,
            [this](const QModelIndex &index) { activateSingleClickedRow(index.row()); });
    connect(iconList_, &QWidget::customContextMenuRequested, this,
            [this, owner = projectedPanel_, view = iconList_](const QPoint &pos) {
                showItemContextMenu(owner, view, pos);
            });
}

void MainWindow::appendParentEntry() {
    if (!app_.GetFocusedPanel()._showDots || app_.GetFocusedPanel().IsRootFolder())
        return;
    const bool sorting = table_->isSortingEnabled();
    table_->setSortingEnabled(false);
    FilePanelModel::Row row;
    row.name = QStringLiteral("..");
    row.directory = true;
    row.icon = QApplication::style()->standardIcon(QStyle::SP_DirIcon);
    row.toolTip = QCoreApplication::translate("mainwindow_panel", "Parent folder");
    table_->panelModel()->append(std::move(row));
    table_->setSortingEnabled(sorting);
}

void MainWindow::updateParentEntry() {
    if (!table_)
        return;
    for (int row = 0; row < table_->rowCount(); ++row) {
        if (table_->model()->index(row, 0).data(Qt::UserRole + 6).toUInt() == UInt32(-1)) {
            table_->model()->removeRow(row);
            break;
        }
    }
    if (state_ != State::Namespace && state_ != State::Loading && state_ != State::Operating)
        appendParentEntry();
    applySortMode();
    refreshActions();
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
