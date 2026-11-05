#include "7zip/PropID.h"
#include "filemanager/PropertyName.h"
#include "filemanager/panels/qtpaneladdresscombo.h"
#include "filemanager/panels/qtpanelcontextmenu.h"
#include "filemanager/qtfilemanagerdrag.h"
#include "filemanager/rename/qtrenamecontroller.h"
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
    navigation->addWidget(projectedUi().pathCombo);
    layout->addLayout(navigation);
    createFileTable(central);
    if (!panelSplitter_)
        createPanelActions(editMenu);
    createIconList(central);
    projectedUi().viewStack = new QStackedWidget(central);
    projectedUi().viewStack->setObjectName("fileViewStack");
    projectedUi().viewStack->addWidget(projectedUi().table);
    projectedUi().viewStack->addWidget(projectedUi().iconList);
    layout->addWidget(projectedUi().viewStack);
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
        panelSplitter_->setObjectName("filePanelSplitter");
        setCentralWidget(panelSplitter_);
    }
    panelUi_[index].container = central;
    central->setObjectName(QStringLiteral("filePanel%1").arg(index));
    panelSplitter_->insertWidget(static_cast<int>(index), central);
    projectedUi().table->addActions(panelShortcutActions_);
    projectedUi().iconList->addActions(panelShortcutActions_);
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
}

void MainWindow::createFileTable(QWidget *central) {
    projectedUi().table = new FileTable(7, central);
    auto *fileTable = static_cast<FileTable *>(projectedUi().table);
    fileTable->setFullRowHighlight(fileListSettings_.FullRow);
    fileTable->sourcePanel = &app_.GetFocusedPanel();
    fileTable->setColumnProperties(
        {kpidName, kpidPath, kpidSize, kpidPackSize, kpidMTime, kpidExtension, kpidNoProperty});
    projectedUi().table->setObjectName("archiveTable");
    QtBindLocalText(projectedUi().table, "mainwindow_panel",
                    QT_TRANSLATE_NOOP("mainwindow_panel", "File list"), true);
    projectedUi().table->verticalHeader()->hide();
    projectedUi().table->panelModel()->setColumns(
        {"Name", "Path", "Size", "Packed Size", "Modified", "Type", "Order"});
    projectedUi().table->setColumnHidden(1, true);
    projectedUi().table->setColumnHidden(5, true);
    projectedUi().table->setColumnHidden(6, true);
    projectedUi().table->horizontalHeader()->setSectionsMovable(true);
    projectedUi().table->setColumnWidth(0, 160);
    projectedUi().table->setColumnWidth(2, 100);
    projectedUi().table->setColumnWidth(3, 100);
    projectedUi().table->setColumnWidth(4, 150);
    projectedUi().table->setShowGrid(fileListSettings_.ShowGrid);
    projectedUi().table->setWordWrap(false);
    projectedUi().table->setTextElideMode(Qt::ElideMiddle);
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
    parent_ = new QAction(projectedUi().table);
    parent_->setObjectName("parentFolderAction");
    projectedUi().table->addAction(parent_);
    selectAll_ = new QAction(projectedUi().table);
    selectAll_->setObjectName("selectAllAction");
    projectedUi().table->addAction(selectAll_);
    selectAll_->setText("Select All");
    deselectAll_ = new QAction("Deselect All", editMenu);
    deselectAll_->setObjectName("deselectAllAction");
    invertSelection_ = new QAction("Invert Selection", editMenu);
    invertSelection_->setObjectName("invertSelectionAction");
    selectMask_ = new QAction("Select...", editMenu);
    selectMask_->setObjectName("selectMaskAction");
    deselectMask_ = new QAction("Deselect...", editMenu);
    deselectMask_->setObjectName("deselectMaskAction");
    selectType_ = new QAction("Select by Type", editMenu);
    selectType_->setObjectName("selectTypeAction");
    deselectType_ = new QAction("Deselect by Type", editMenu);
    deselectType_->setObjectName("deselectTypeAction");
    populateSourceMenus();
}

void MainWindow::createIconList(QWidget *central) {
    projectedUi().iconList = new FileIconList(central);
    projectedUi().iconList->setObjectName("iconList");
    QtBindLocalText(projectedUi().iconList, "mainwindow_panel",
                    QT_TRANSLATE_NOOP("mainwindow_panel", "File list"), true);
    projectedUi().iconList->setModel(projectedUi().table->model());
    projectedUi().iconList->setItemDelegate(CreateFileRowDelegate(projectedUi().iconList));
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
