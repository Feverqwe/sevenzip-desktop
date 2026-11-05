#include "filemanager/window/qtworkspacesidebar.h"
#include "7zip/PropID.h"
#include "Windows/PropVariant.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/providers/RootFolder.h"
#include "filemanager/window/qtfavoritestore.h"
#include "filemanager/window/qtmainwindowchrome.h"
#include "filemanager/window/qtworkspacetheme.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include <QApplication>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QScrollArea>
#include <QShortcut>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QToolButton>

namespace {
class PlaceDelegate final : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const override {
        return {100, qMax(30, option.fontMetrics.height() + 12)};
    }
    void paint(QPainter *painter, const QStyleOptionViewItem &original,
               const QModelIndex &index) const override {
        auto option = original;
        initStyleOption(&option, index);
        const auto c = QtWorkspaceTheme::colors();
        const bool selected = option.state & QStyle::State_Selected;
        const bool focused = option.state & QStyle::State_HasFocus;
        const QRect frame = option.rect.adjusted(2, 1, -2, -1);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        if (selected || (option.state & QStyle::State_MouseOver)) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(selected ? c.selection : c.hover);
            painter->drawRoundedRect(frame, 5, 5);
        }
        if (option.icon.isNull()) {
            const QString path = index.data(Qt::UserRole + 1).toString();
            option.icon = QApplication::style()->standardIcon(
                QDir::cleanPath(path) == QDir::homePath() ? QStyle::SP_DirHomeIcon
                                                          : QStyle::SP_DirIcon);
        }
        const QRect icon(frame.left() + 8, frame.center().y() - 8, 16, 16);
        option.icon.paint(painter, icon);
        const QRect text = frame.adjusted(32, 0, -8, 0);
        painter->setFont(option.font);
        painter->setPen(c.text);
        painter->drawText(text, Qt::AlignLeft | Qt::AlignVCenter,
                          option.fontMetrics.elidedText(option.text, Qt::ElideRight, text.width()));
        if (focused) {
            painter->setPen(c.accent);
            painter->setBrush(Qt::NoBrush);
            painter->drawRoundedRect(frame, 5, 5);
        }
        painter->restore();
    }
};

void ConfigurePlaces(QListView &list) {
    QtWorkspaceTheme::fileView(list);
    auto palette = list.palette();
    palette.setColor(QPalette::Base, QtWorkspaceTheme::colors().secondary);
    list.setPalette(palette);
    list.setItemDelegate(new PlaceDelegate(&list));
    list.setMouseTracking(true);
    list.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list.setTextElideMode(Qt::ElideRight);
    list.setIconSize(QSize(16, 16));
}

QLabel *Heading(QWidget &parent, const char *text) {
    auto *label = new QLabel(&parent);
    QtBindLocalText(label, "QtWindowWorkspace", text);
    auto font = label->font();
    font.setPointSizeF(font.pointSizeF() * 11 / 13);
    font.setWeight(QFont::DemiBold);
    label->setFont(font);
    label->setContentsMargins(10, 4, 0, 8);
    auto palette = label->palette();
    palette.setColor(QPalette::WindowText, QtWorkspaceTheme::colors().subtle);
    label->setPalette(palette);
    return label;
}
} // namespace

QtPlacesSidebar::QtPlacesSidebar(QtFavoriteStore &favorites, QtPlaceActions actions, unsigned panel,
                                 QWidget *parent)
    : QScrollArea(parent), favorites_(favorites), actions_(std::move(actions)) {
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *contents = new QWidget;
    QtWorkspaceTheme::pane(*contents);
    setWidget(contents);
    QtWorkspaceTheme::pane(*this);
    setObjectName(QStringLiteral("placesSidebar%1").arg(panel));
    auto *places = new QVBoxLayout(contents);
    places->setSpacing(2);
    CreateFavorites(*places, panel);
    places->addSpacing(20);
    places->addWidget(Heading(*this, QT_TRANSLATE_NOOP("QtWindowWorkspace", "Locations")));
    locations_ = new QListWidget(this);
    locations_->setObjectName("nativePlaces");
    ConfigurePlaces(*locations_);
    places->addWidget(locations_);
    places->addStretch();
    connect(locations_, &QListWidget::clicked, this, [this](const QModelIndex &index) {
        actions_.open(index.data(Qt::UserRole + 1).toString(), QtPlaceTarget::Current);
    });
    auto *open = new QShortcut(QKeySequence(Qt::Key_Return), locations_);
    open->setContext(Qt::WidgetShortcut);
    connect(open, &QShortcut::activated, this, [this] {
        if (locations_->currentIndex().isValid())
            actions_.open(locations_->currentIndex().data(Qt::UserRole + 1).toString(),
                          QtPlaceTarget::Current);
    });
    SetCompact(false);
}

void QtPlacesSidebar::CreateFavorites(QVBoxLayout &places, unsigned panel) {
    auto *heading = new QHBoxLayout;
    heading->addWidget(Heading(*this, QT_TRANSLATE_NOOP("QtWindowWorkspace", "Favorites")), 1);
    auto *add = new QToolButton(this);
    add->setObjectName("addFavoriteButton");
    add->setText(QStringLiteral("+"));
    add->setAutoRaise(true);
    QtConfigureChromeToolButton(*add);
    add->setFixedSize(24, 24);
    QtBindLocalText(add, "QtWindowWorkspace",
                    QT_TRANSLATE_NOOP("QtWindowWorkspace", "Add current location"), true);
    QtBindLocalToolTip(add, "QtWindowWorkspace",
                       QT_TRANSLATE_NOOP("QtWindowWorkspace", "Add current location"));
    connect(add, &QToolButton::clicked, this, [this] { actions_.add(); });
    heading->addWidget(add);
    places.addLayout(heading);
    auto *list = new QtFavoriteList(this);
    favoritesView_ = list;
    list->setObjectName(QStringLiteral("favoritePlaces%1").arg(panel));
    list->setModel(&favorites_);
    ConfigurePlaces(*list);
    list->setDragDropMode(QAbstractItemView::DragDrop);
    list->setDefaultDropAction(Qt::MoveAction);
    list->setDropIndicatorShown(true);
    list->setEditTriggers(QAbstractItemView::EditKeyPressed);
    list->setContextMenuPolicy(Qt::CustomContextMenu);
    places.addWidget(list);
    connect(list, &QListView::clicked, this, [this](const QModelIndex &index) {
        actions_.open(favorites_.Favorite(index.row()).path, QtPlaceTarget::Current);
    });
    auto *open = new QShortcut(QKeySequence(Qt::Key_Return), list);
    open->setContext(Qt::WidgetShortcut);
    connect(open, &QShortcut::activated, this, [this] {
        if (favoritesView_->currentIndex().isValid())
            actions_.open(favorites_.Favorite(favoritesView_->currentIndex().row()).path,
                          QtPlaceTarget::Current);
    });
    connect(list, &QListView::customContextMenuRequested, this, &QtPlacesSidebar::ShowFavoriteMenu);
    const auto resize = [list] {
        const int rowHeight = qMax(30, list->fontMetrics().height() + 12);
        list->setFixedHeight(qMax(1, list->model()->rowCount()) * rowHeight);
    };
    connect(&favorites_, &QAbstractItemModel::rowsInserted, list, resize);
    connect(&favorites_, &QAbstractItemModel::rowsRemoved, list, resize);
    resize();
}

void QtPlacesSidebar::ShowFavoriteMenu(const QPoint &point) {
    const auto index = favoritesView_->indexAt(point);
    const QString path = favorites_.Favorite(index.row()).path;
    QMenu menu;
    if (index.isValid()) {
        connect(menu.addAction(tr("Open in new tab")), &QAction::triggered, this,
                [this, path] { actions_.open(path, QtPlaceTarget::NewTab); });
        connect(menu.addAction(tr("Open in new window")), &QAction::triggered, this,
                [this, path] { actions_.open(path, QtPlaceTarget::NewWindow); });
        menu.addSeparator();
        connect(menu.addAction(tr("Rename favorite")), &QAction::triggered, this,
                [this, index] { favoritesView_->edit(index); });
        connect(menu.addAction(tr("Remove favorite")), &QAction::triggered, this,
                [this, index] { favorites_.removeRow(index.row()); });
    }
    connect(menu.addAction(tr("Add current location")), &QAction::triggered, this,
            [this] { actions_.add(); });
    menu.exec(favoritesView_->viewport()->mapToGlobal(point));
}

void QtPlacesSidebar::SetCompact(bool compact) {
    setFixedWidth(QtWorkspaceTheme::metric(*this, compact ? 140 : 168));
    const int padding = compact ? 4 : 8;
    widget()->layout()->setContentsMargins(padding, 16, padding, 8);
}

void QtPlacesSidebar::RefreshLocations() {
    const QString language = ToQString(g_LangID) + ':' + tr("Locations");
    if (language_ == language)
        return;
    language_ = language;
    favorites_.RefreshLabels();
    locations_->clear();
    CRootFolder root;
    root.Init();
    IFolderFolder &source = root;
    UInt32 count = 0;
    source.GetNumberOfItems(&count);
    int iconIndex = 0;
    for (UInt32 i = 0; i < count; ++i) {
        NWindows::NCOM::CPropVariant name;
        if (source.GetProperty(i, kpidName, &name) != S_OK || name.vt != VT_BSTR)
            continue;
        if (UString(name.bstrVal) == RootFolder_GetName_Documents(iconIndex))
            continue;
        CMyComPtr<IFolderFolder> folder;
        if (source.BindToFolder(i, &folder) != S_OK || !folder)
            continue;
        QString label = ToQString(UString(name.bstrVal));
        if (label == QStringLiteral("\\\\."))
            label = QCoreApplication::translate("RootFolder", "Volumes");
        const auto icon = UString(name.bstrVal) == RootFolder_GetName_Computer(iconIndex)
                              ? QStyle::SP_ComputerIcon
                          : UString(name.bstrVal) == RootFolder_GetName_Network(iconIndex)
                              ? QStyle::SP_DriveNetIcon
                              : QStyle::SP_DriveHDIcon;
        auto *item = new QListWidgetItem(style()->standardIcon(icon), label, locations_);
        item->setData(Qt::UserRole + 1, ToQString(GetFolderPath(folder)));
    }
    locations_->setFixedHeight(locations_->count() *
                               qMax(30, locations_->fontMetrics().height() + 12));
}

void QtPlacesSidebar::SetCurrentLocation(const QString &path) {
    RefreshLocations();
    const QString current = QDir::cleanPath(path);
    for (auto *view :
         {static_cast<QListView *>(favoritesView_), static_cast<QListView *>(locations_)}) {
        QModelIndex matching;
        for (int row = 0; row < view->model()->rowCount(); ++row) {
            const auto index = view->model()->index(row, 0);
            if (QDir::cleanPath(index.data(Qt::UserRole + 1).toString()) == current)
                matching = index;
        }
        if (!view->hasFocus() && !view->isAncestorOf(QApplication::focusWidget()))
            view->setCurrentIndex(matching);
    }
}
