#include "filemanager/qtfilemanagercommands.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtfavoritestore.h"
#include "filemanager/window/qtfileclipboard.h"
#include "filemanager/window/qtmainwindowchrome.h"
#include "filemanager/window/qtwindowworkspace.h"
#include "filemanager/window/qtworkspaceapplication.h"
#include "filemanager/window/qtworkspacetheme.h"
#include "localization/qtlangcontrols.h"
#include "ui/theme/qtcommandicons.h"
#include <QScopedValueRollback>

namespace {
void RefreshCommandCaption(QToolBar &toolbar, QAction &proxy, const QAction &source) {
    if (proxy.property("createMenu").toBool())
        return;
    proxy.setText(QString(source.text()).remove('&'));
    const QString shortcut = source.shortcut().toString(QKeySequence::NativeText);
    proxy.setToolTip(shortcut.isEmpty() ? proxy.text()
                                        : QStringLiteral("%1 (%2)").arg(proxy.text(), shortcut));
    if (auto *button = qobject_cast<QToolButton *>(toolbar.widgetForAction(&proxy))) {
        button->setAccessibleName(proxy.text());
        button->setToolTip(proxy.toolTip());
        button->setFocusPolicy(Qt::StrongFocus);
        button->setAutoRaise(true);
        QtConfigureMainToolbarButton(*button);
    }
}
} // namespace

void QtWindowWorkspace::CreateActions(unsigned panel, QVBoxLayout &layout) {
    auto &state = panels_[panel];
    state.actions = new QToolBar;
    QtConfigureMainToolbar(*state.actions);
    state.actions->setObjectName(QStringLiteral("panelFileActions%1").arg(panel));
    state.actions->setIconSize(QSize(16, 16));
    state.actions->setToolButtonStyle(Qt::ToolButtonIconOnly);
    state.actions->setMovable(false);
    const auto add = [this, panel, &state](QAction *source, const QIcon &icon = QIcon()) {
        auto *action =
            state.actions->addAction(icon.isNull() ? source->icon() : icon, source->text());
        action->setProperty("sourceAction", QVariant::fromValue(source));
        connect(source, &QAction::changed, action, [toolbar = state.actions, action, source] {
            RefreshCommandCaption(*toolbar, *action, *source);
        });
        connect(action, &QAction::triggered, this, [this, panel, source] {
            window_.activatePanelUi(panel);
            if (window_.projectedPanel_ == panel)
                source->trigger();
        });
    };
    add(window_.createFolder_, QtCommandSymbol(QtCommandIcon::NewFolder));
    auto *create = state.actions->actions().last();
    QtBindLocalText(create, "QtWindowWorkspace", QT_TRANSLATE_NOOP("QtWindowWorkspace", "Create"));
    create->setProperty("createMenu", true);
    auto *menu = new QMenu(state.actions);
    for (auto *source : {window_.createFolder_, window_.createFile_}) {
        auto *choice = menu->addAction(source->text());
        choice->setProperty("sourceAction", QVariant::fromValue(source));
        connect(choice, &QAction::triggered, this, [this, panel, source] {
            window_.activatePanelUi(panel);
            if (window_.projectedPanel_ == panel)
                source->trigger();
        });
    }
    create->setMenu(menu);
    if (auto *button = qobject_cast<QToolButton *>(state.actions->widgetForAction(create))) {
        button->setPopupMode(QToolButton::InstantPopup);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setFocusPolicy(Qt::StrongFocus);
        QtConfigureMainToolbarButton(*button);
        QtBindLocalText(button, "QtWindowWorkspace",
                        QT_TRANSLATE_NOOP("QtWindowWorkspace", "Create"), true);
        QtBindLocalToolTip(button, "QtWindowWorkspace",
                           QT_TRANSLATE_NOOP("QtWindowWorkspace", "Create"));
    }
    add(window_.clipboard_->cut, QtCommandSymbol(QtCommandIcon::Cut));
    add(window_.clipboard_->copy, QtCommandSymbol(QtCommandIcon::Copy));
    add(window_.clipboard_->paste, QtCommandSymbol(QtCommandIcon::Paste));
    add(window_.rename_, QtCommandSymbol(QtCommandIcon::Rename));
    add(window_.remove_);
    layout.addWidget(state.actions);
}

void QtWindowWorkspace::AttachContent(unsigned panel, QVBoxLayout &layout) {
    auto &ui = window_.panelUi_[panel];
    ui.navigationNotice = new QWidget;
    auto *notice = new QHBoxLayout(ui.navigationNotice);
    notice->setContentsMargins(12, 6, 12, 6);
    ui.navigationError = new QLabel(ui.navigationNotice);
    ui.navigationError->setTextFormat(Qt::PlainText);
    ui.navigationError->setWordWrap(true);
    notice->addWidget(ui.navigationError, 1);
    auto *retry = new QPushButton(ui.navigationNotice);
    QtBindLocalText(retry, "QtWindowWorkspace", QT_TRANSLATE_NOOP("QtWindowWorkspace", "Retry"));
    notice->addWidget(retry);
    auto *parent = new QPushButton(ui.navigationNotice);
    QtBindLocalText(parent, "QtWindowWorkspace",
                    QT_TRANSLATE_NOOP("QtWindowWorkspace", "Parent folder"));
    notice->addWidget(parent);
    connect(retry, &QPushButton::clicked, this, [this, panel] { RetryLocation(panel, false); });
    connect(parent, &QPushButton::clicked, this, [this, panel] { RetryLocation(panel, true); });
    layout.addWidget(ui.navigationNotice);
    ui.navigationNotice->hide();
    auto *body = new QWidget;
    auto *row = new QHBoxLayout(body);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    auto *sidebar = new QWidget(body);
    QtWorkspaceTheme::pane(*sidebar);
    sidebar->setObjectName(QStringLiteral("placesSidebar%1").arg(panel));
    sidebar->setFixedWidth(window_.width() <= 700 ? 140 : 168);
    auto *places = new QVBoxLayout(sidebar);
    places->setContentsMargins(8, 16, 8, 8);
    places->setSpacing(2);
    auto *heading = new QLabel(tr("Favorites"), sidebar);
    QtBindLocalText(heading, "QtWindowWorkspace",
                    QT_TRANSLATE_NOOP("QtWindowWorkspace", "Favorites"));
    auto font = heading->font();
    font.setPointSizeF(font.pointSizeF() * 11 / 13);
    font.setWeight(QFont::DemiBold);
    heading->setFont(font);
    heading->setContentsMargins(10, 4, 10, 8);
    auto headingPalette = heading->palette();
    headingPalette.setColor(QPalette::WindowText, QtWorkspaceTheme::colors().subtle);
    heading->setPalette(headingPalette);
    places->addWidget(heading);
    for (const auto &[caption, path] : QList<QPair<QString, QString>>{
             {QString::fromUtf8(QT_TRANSLATE_NOOP("QtWindowWorkspace", "Home")), QDir::homePath()},
             {QString::fromUtf8(QT_TRANSLATE_NOOP("QtWindowWorkspace", "Downloads")),
              QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)},
             {QString::fromUtf8(QT_TRANSLATE_NOOP("QtWindowWorkspace", "Documents")),
              QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)}}) {
        auto *button = new QToolButton(sidebar);
        button->setText(caption);
        QtBindLocalText(button, "QtWindowWorkspace", caption.toUtf8().constData());
        button->setToolTip(path);
        button->setAutoRaise(true);
        QtConfigureChromeToolButton(*button);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setIcon(window_.style()->standardIcon(QStyle::SP_DirIcon));
        places->addWidget(button, 0, Qt::AlignLeft);
        connect(button, &QToolButton::clicked, this, [this, path] { OpenFavorite(path, 0); });
    }
    auto *list = new QtFavoriteList(sidebar);
    list->setObjectName(QStringLiteral("favoritePlaces%1").arg(panel));
    list->setModel(favorites_);
    list->setFrameShape(QFrame::NoFrame);
    auto listPalette = list->palette();
    listPalette.setColor(QPalette::Base, QtWorkspaceTheme::colors().secondary);
    list->setPalette(listPalette);
    list->setDragDropMode(QAbstractItemView::DragDrop);
    list->setDefaultDropAction(Qt::MoveAction);
    list->setDropIndicatorShown(true);
    list->setEditTriggers(QAbstractItemView::EditKeyPressed);
    list->setContextMenuPolicy(Qt::CustomContextMenu);
    places->addWidget(list, 1);
    connect(list, &QListView::clicked, this, [this](const QModelIndex &index) {
        OpenFavorite(favorites_->Favorite(index.row()).path, 0);
    });
    connect(list, &QListView::customContextMenuRequested, this, [this, list](const QPoint &point) {
        const auto index = list->indexAt(point);
        const QString path = favorites_->Favorite(index.row()).path;
        QMenu menu;
        if (index.isValid()) {
            connect(menu.addAction(tr("Open in new tab")), &QAction::triggered, this,
                    [this, path] { OpenFavorite(path, 1); });
            connect(menu.addAction(tr("Open in new window")), &QAction::triggered, this,
                    [this, path] { OpenFavorite(path, 2); });
            menu.addSeparator();
            connect(menu.addAction(tr("Rename favorite")), &QAction::triggered, list,
                    [list, index] { list->edit(index); });
            connect(menu.addAction(tr("Remove favorite")), &QAction::triggered, this,
                    [this, index] { favorites_->removeRow(index.row()); });
        }
        connect(menu.addAction(tr("Add current location")), &QAction::triggered, this,
                &QtWindowWorkspace::AddFavorite);
        menu.exec(list->viewport()->mapToGlobal(point));
    });
    panels_[panel].sidebar = sidebar;
    row->addWidget(sidebar);
    row->addWidget(window_.panelUi_[panel].viewStack, 1);
    layout.addWidget(body, 1);
    if (!window_.findChild<QAction *>("sidebarAction")) {
        auto *view = window_.findChild<QMenu *>("viewMenu");
        auto *toggle = view->addAction(tr("Places sidebar"));
        toggle->setObjectName("sidebarAction");
        QtBindLocalText(toggle, "QtWindowWorkspace",
                        QT_TRANSLATE_NOOP("QtWindowWorkspace", "Places sidebar"));
        toggle->setCheckable(true);
        toggle->setChecked(sidebarVisible_);
        connect(toggle, &QAction::toggled, this, [this](bool shown) {
            sidebarVisible_ = shown;
            RefreshChrome();
        });
        auto *favorites = window_.findChild<QMenu *>("favoritesMenu");
        auto *add = new QAction(tr("Add current location"), favorites);
        QtBindLocalText(add, "QtWindowWorkspace",
                        QT_TRANSLATE_NOOP("QtWindowWorkspace", "Add current location"));
        favorites->insertAction(favorites->actions().first(), add);
        connect(add, &QAction::triggered, this, &QtWindowWorkspace::AddFavorite);
        auto *saved = favorites->addMenu(tr("Saved places"));
        saved->menuAction()->setProperty("nativeCommandID", 1 << 16);
        QtBindLocalText(saved->menuAction(), "QtWindowWorkspace",
                        QT_TRANSLATE_NOOP("QtWindowWorkspace", "Saved places"));
        connect(saved, &QMenu::aboutToShow, this, [this, saved] {
            saved->clear();
            for (int row = 0; row < favorites_->rowCount(); ++row) {
                const auto entry = favorites_->Favorite(row);
                auto *action = saved->addAction(entry.label);
                action->setToolTip(entry.path);
                connect(action, &QAction::triggered, this,
                        [this, path = entry.path] { OpenFavorite(path, 0); });
            }
        });
    }
}

void QtWindowWorkspace::RefreshChrome() {
    for (unsigned panel = 0; panel < 2; ++panel) {
        const auto &state = panels_[panel];
        if (!state.actions || !window_.commands_ || !window_.panelUi_[panel].table)
            continue;
        if (window_.panelUi_[panel].navigationNotice)
            window_.panelUi_[panel].navigationNotice->setVisible(
                window_.panelUi_[panel].locationUnavailable);
        if (state.sidebar) {
            state.sidebar->setFixedWidth(window_.width() <= 700 ? 140 : 168);
            state.sidebar->setVisible(sidebarVisible_ &&
                                      (window_.app_.NumPanels == 1 || panel == 0));
        }
        state.actions->setToolButtonStyle(window_.app_.NumPanels == 1 && window_.width() > 800
                                              ? Qt::ToolButtonTextBesideIcon
                                              : Qt::ToolButtonIconOnly);
        const auto native = window_.commands_->AvailabilityForPanel(panel);
        const auto clipboard = window_.clipboard_->AvailabilityForPanel(panel);
        auto *container = window_.panelUi_[panel].container;
        if (auto *button = container->findChild<QAbstractButton *>(
                QStringLiteral("panelParentButton%1").arg(panel)))
            button->setEnabled(native.value(window_.up_->property("nativeCommandID").toUInt()));
        if (auto *button =
                container->findChild<QToolButton *>(QStringLiteral("panelBackButton%1").arg(panel)))
            button->setEnabled(native.value(window_.back_->property("nativeCommandID").toUInt()));
        if (auto *button = container->findChild<QToolButton *>(
                QStringLiteral("panelSidebarButton%1").arg(panel)))
            button->setChecked(sidebarVisible_);
        for (auto *action : state.actions->actions()) {
            auto *source = action->property("sourceAction").value<QAction *>();
            if (!source)
                continue;
            const auto id = source->property("nativeCommandID");
            if (action->property("createMenu").toBool()) {
                if (auto *button =
                        qobject_cast<QToolButton *>(state.actions->widgetForAction(action)))
                    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
                bool any = false;
                for (auto *choice : action->menu()->actions()) {
                    auto *command = choice->property("sourceAction").value<QAction *>();
                    const bool available =
                        native.value(command->property("nativeCommandID").toUInt());
                    choice->setEnabled(available);
                    choice->setText(command->text());
                    any |= available;
                }
                action->setEnabled(any);
                continue;
            }
            const bool enabled = source == window_.clipboard_->cut    ? clipboard.cut
                                 : source == window_.clipboard_->copy ? clipboard.copy
                                 : source == window_.clipboard_->paste
                                     ? clipboard.paste
                                     : native.value(id.toUInt(), false);
            action->setEnabled(enabled);
            RefreshCommandCaption(*state.actions, *action, *source);
        }
    }
}

void QtWindowWorkspace::AddFavorite() {
    if (!Ready())
        return;
    const QString path = CurrentLocation();
    bool accepted = false;
    const QString label =
        QInputDialog::getText(&window_, tr("Add favorite"), tr("Name"), QLineEdit::Normal,
                              QFileInfo(QDir::cleanPath(path)).fileName(), &accepted);
    if (accepted && !label.trimmed().isEmpty())
        favorites_->Add(path, label.trimmed());
}

void QtWindowWorkspace::OpenFavorite(const QString &path, int target) {
    if (!Ready())
        return;
    if (target == 1)
        NewTab(window_.projectedPanel_, path);
    else if (target == 2) {
        if (auto *application = QtWorkspaceApplication::Find())
            application->NewWindow(path);
    } else {
        const unsigned panel = window_.projectedPanel_;
        {
            const QScopedValueRollback<bool> changing(changing_, true);
            panels_[panel].tabs[panels_[panel].current].path = path;
            const HRESULT result = window_.navigateTo(path, true);
            CompleteLocationOpen(panel, path, result);
        }
        window_.refreshActions();
    }
}
