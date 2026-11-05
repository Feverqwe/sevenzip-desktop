#include "filemanager/qtfilemanagercommands.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtfavoritestore.h"
#include "filemanager/window/qtfileclipboard.h"
#include "filemanager/window/qtmainwindowchrome.h"
#include "filemanager/window/qtnavigationbar.h"
#include "filemanager/window/qtwindowworkspace.h"
#include "filemanager/window/qtworkspaceapplication.h"
#include "filemanager/window/qtworkspacesidebar.h"
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

void QtWindowWorkspace::AttachContent(unsigned panel, QVBoxLayout &layout, QWidget &status) {
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
    auto *sidebar = CreateSidebar(panel);
    panels_[panel].sidebar = sidebar;
    row->addWidget(sidebar);
    auto *files = new QWidget(body);
    auto *fileLayout = new QVBoxLayout(files);
    fileLayout->setContentsMargins(0, 0, 0, 0);
    fileLayout->setSpacing(0);
    fileLayout->addWidget(window_.panelUi_[panel].viewStack, 1);
    fileLayout->addWidget(&status);
    row->addWidget(files, 1);
    layout.addWidget(body, 1);
    if (!window_.findChild<QAction *>("sidebarAction")) {
        auto *view = window_.findChild<QMenu *>("viewMenu");
        auto *toggle = new QAction(tr("Places sidebar"), view);
        view->insertAction(window_.twoPanelsAction_, toggle);
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
        saved->setObjectName("savedPlacesMenu");
        saved->menuAction()->setProperty("nativeCommandID", 1 << 16);
        QtBindLocalText(saved->menuAction(), "QtWindowWorkspace",
                        QT_TRANSLATE_NOOP("QtWindowWorkspace", "Saved places"));
        connect(saved, &QMenu::aboutToShow, this, [this, saved] {
            saved->clear();
            for (int row = 0; row < favorites_->rowCount(); ++row) {
                const auto entry = favorites_->Favorite(row);
                auto *action = saved->addAction(entry.label);
                action->setToolTip(entry.path);
                connect(action, &QAction::triggered, this, [this, id = entry.id] {
                    const auto index = favorites_->IndexForId(id);
                    if (index.isValid())
                        OpenFavorite(favorites_->Favorite(index.row()).path, 0);
                });
            }
        });
    }
}

QWidget *QtWindowWorkspace::CreateSidebar(unsigned panel) {
    return new QtPlacesSidebar(*favorites_, *nativePlaces_,
                               {[this] { AddFavorite(); },
                                [this](const QString &path, QtPlaceTarget target) {
                                    OpenFavorite(path, static_cast<int>(target));
                                }},
                               panel);
}

void QtWindowWorkspace::RefreshSidebar(unsigned panel) {
    auto *sidebar = dynamic_cast<QtPlacesSidebar *>(panels_[panel].sidebar);
    if (!sidebar)
        return;
    sidebar->SetCompact(window_.width() <= 700);
    sidebar->setVisible(sidebarVisible_ && (window_.app_.NumPanels == 1 || panel == 0));
    const auto &active = panels_[window_.app_.GetFocusedPanelIndex()];
    const bool available = !active.tabs.empty() && !active.tabs[active.current].unavailable;
    sidebar->SetCurrentLocation(available ? active.tabs[active.current].path : QString(),
                                available ? active.tabs[active.current].nativePlaceIdentity
                                          : QString());
}

void QtWindowWorkspace::RefreshChrome() {
    for (unsigned panel = 0; panel < 2; ++panel) {
        const auto &state = panels_[panel];
        if (!state.actions || !window_.commands_ || !window_.panelUi_[panel].table)
            continue;
        if (window_.panelUi_[panel].navigationNotice)
            window_.panelUi_[panel].navigationNotice->setVisible(
                window_.panelUi_[panel].locationUnavailable);
        RefreshSidebar(panel);
        if (auto *row =
                dynamic_cast<QtNavigationRow *>(window_.panelUi_[panel].navigation->parentWidget()))
            row->SetActivePane(window_.app_.NumPanels == 2 && window_.projectedPanel_ == panel);
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
                QStringLiteral("panelForwardButton%1").arg(panel)))
            button->setEnabled(
                native.value(window_.forward_->property("nativeCommandID").toUInt()));
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
                    button->setToolButtonStyle(window_.app_.NumPanels == 2
                                                   ? Qt::ToolButtonIconOnly
                                                   : Qt::ToolButtonTextBesideIcon);
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
    int placesWidth = 0;
    for (const auto &state : panels_)
        if (state.sidebar && !state.sidebar->isHidden())
            placesWidth = state.sidebar->width();
    if (window_.panelSplitter_)
        window_.panelSplitter_->SetLeadingInset(placesWidth);
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
        window_.withPanelProjection(panel, [this, panel, &path] {
            const HRESULT result = Navigate([&]() -> HRESULT {
                const HRESULT opened = window_.navigateTo(path, true);
                return opened == S_OK && !LocationMatches(panel, path) ? S_FALSE : opened;
            });
            CompleteLocationOpen(panel, path, result);
            if (panels_[panel].tabs[panels_[panel].current].unavailable) {
                auto &tab = panels_[panel].tabs[panels_[panel].current];
                tab.nativePlaceIdentity.clear();
                tab.archiveLocation = {};
            }
        });
        window_.refreshActions();
    }
}
