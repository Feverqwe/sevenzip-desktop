#include "filemanager/window/qtworkspacesidebar.h"
#include "filemanager/window/qtfavoritestore.h"
#include "filemanager/window/qtmainwindowchrome.h"
#include "filemanager/window/qtnativeplaces.h"
#include "filemanager/window/qtplaceprojection.h"
#include "filemanager/window/qtworkspacetheme.h"
#include "localization/qtlangcontrols.h"
#include "ui/theme/qtcommandicons.h"
#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
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
        const bool current = index.data(QtPlaceRoles::CurrentLocation).toBool();
        const bool focused = option.state & QStyle::State_HasFocus;
        const QRect frame = option.rect.adjusted(2, 1, -2, -1);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        if (current || (option.state & QStyle::State_MouseOver)) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(current ? c.selection : c.hover);
            painter->drawRoundedRect(frame, 5, 5);
        }
        if (option.icon.isNull())
            option.icon = QtCommandSymbol(QtCommandIcon::Folder);
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

QtPlacesSidebar::QtPlacesSidebar(QtFavoriteStore &favorites, QtNativePlaces &locations,
                                 QtPlaceActions actions, unsigned panel, QWidget *parent)
    : QScrollArea(parent), favorites_(favorites), nativePlaces_(locations),
      actions_(std::move(actions)) {
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
    locations_ = new QListView(this);
    locationPlaces_ = new QtPlaceProjection(nativePlaces_, locations_);
    locations_->setModel(locationPlaces_);
    locations_->setObjectName("nativePlaces");
    ConfigurePlaces(*locations_);
    const auto labelsChanged = [this] { favorites_.RefreshLabels(); };
    connect(&nativePlaces_, &QAbstractItemModel::dataChanged, this, labelsChanged);
    connect(&nativePlaces_, &QAbstractItemModel::modelReset, this, labelsChanged);
    places->addWidget(locations_);
    places->addStretch();
    connect(locations_, &QListView::clicked, this, [this](const QModelIndex &index) {
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
    favoritePlaces_ = new QtPlaceProjection(favorites_, list);
    list->setModel(favoritePlaces_);
    ConfigurePlaces(*list);
    list->setDragDropMode(QAbstractItemView::DragDrop);
    list->setDefaultDropAction(Qt::MoveAction);
    list->setDropIndicatorShown(true);
    list->setEditTriggers(QAbstractItemView::EditKeyPressed);
    list->setContextMenuPolicy(Qt::CustomContextMenu);
    places.addWidget(list);
    connect(list, &QListView::clicked, this, [this](const QModelIndex &index) {
        actions_.open(index.data(QtPlaceRoles::Path).toString(), QtPlaceTarget::Current);
    });
    auto *open = new QShortcut(QKeySequence(Qt::Key_Return), list);
    open->setContext(Qt::WidgetShortcut);
    connect(open, &QShortcut::activated, this, [this] {
        if (favoritesView_->currentIndex().isValid())
            actions_.open(favoritesView_->currentIndex().data(QtPlaceRoles::Path).toString(),
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
    const QString id = index.data(QtPlaceRoles::Identity).toString();
    const auto open = [this, id](QtPlaceTarget target) {
        const auto current = favorites_.IndexForId(id);
        if (current.isValid())
            actions_.open(favorites_.Favorite(current.row()).path, target);
    };
    QMenu menu;
    QAction *rename = nullptr;
    if (index.isValid()) {
        connect(menu.addAction(tr("Open in new tab")), &QAction::triggered, this,
                [open] { open(QtPlaceTarget::NewTab); });
        connect(menu.addAction(tr("Open in new window")), &QAction::triggered, this,
                [open] { open(QtPlaceTarget::NewWindow); });
        menu.addSeparator();
        rename = menu.addAction(tr("Rename favorite"));
        connect(menu.addAction(tr("Remove favorite")), &QAction::triggered, this, [this, id] {
            const auto current = favorites_.IndexForId(id);
            if (current.isValid())
                favorites_.removeRow(current.row());
        });
    }
    connect(menu.addAction(tr("Add current location")), &QAction::triggered, this,
            [this] { actions_.add(); });
    const auto *chosen = menu.exec(favoritesView_->viewport()->mapToGlobal(point));
    if (rename && chosen == rename) {
        const auto current = favorites_.IndexForId(id);
        if (current.isValid()) {
            const auto index = favoritePlaces_->mapFromSource(current);
            favoritesView_->setCurrentIndex(index);
            favoritesView_->edit(index);
        }
    }
}

void QtPlacesSidebar::SetCompact(bool compact) {
    setFixedWidth(QtWorkspaceTheme::metric(*this, compact ? 140 : 168));
    const int padding = compact ? 4 : 8;
    widget()->layout()->setContentsMargins(padding, 16, padding, 8);
}

void QtPlacesSidebar::RefreshLocations() {
    nativePlaces_.Refresh();
    locations_->setFixedHeight(locationPlaces_->rowCount() *
                               qMax(30, locations_->fontMetrics().height() + 12));
}

void QtPlacesSidebar::SetCurrentLocation(const QString &path, const QString &nativeIdentity) {
    RefreshLocations();
    favoritePlaces_->SetCurrentLocation(path);
    locationPlaces_->SetCurrentLocation(path, nativeIdentity);
}
