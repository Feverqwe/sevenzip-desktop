#include "filemanager/window/qtwindowworkspace.h"
#include "dialogs/recentplaces/qtrecentplacesdialog.h"
#include "filemanager/panels/qtpaneladdresscombo.h"
#include "filemanager/qtfilemanagerdrag.h"
#include "filemanager/rename/qtrenamecontroller.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtapppresentation.h"
#include "filemanager/window/qtfavoritestore.h"
#include "filemanager/window/qtmainwindowchrome.h"
#include "filemanager/window/qtnativeplaces.h"
#include "filemanager/window/qtnavigationbar.h"
#include "filemanager/window/qtrecentplacesstore.h"
#include "filemanager/window/qtworkspaceruntime.h"
#include "filemanager/window/qtworkspacetabbar.h"
#include "filemanager/window/qtworkspacetheme.h"
#include "localization/qtlangcontrols.h"
#include "platform/associations/qtassociatedfileservices.h"
#include <QJsonArray>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QTabBar>

QtWindowWorkspace::QtWindowWorkspace(MainWindow &window) : QObject(&window), window_(window) {
    recentPlaces_ = QtRecentPlacesStore::ForSettings(window.toolbarSettingsPath());
    connect(recentPlaces_, &QtRecentPlacesStore::historyChanged, this,
            [this] { window_.app_.AppState.FolderHistory.Read(window_.toolbarSettingsPath()); });
    connect(recentPlaces_, &QAbstractItemModel::modelReset, this, [this] {
        if (window_.commands_ && window_.panelUi_[0].table &&
            !window_.app_.QtOperations().closed() && !window_.app_.QtOperations().active() &&
            !window_.app_.QtOperations().callActive() && !window_.sourceArchiveProgress_)
            window_.refreshActions();
    });
    qApp->installEventFilter(this);
    for (auto *object : qApp->children()) {
        if (auto *places = dynamic_cast<QtNativePlaces *>(object))
            nativePlaces_ = places;
        if (auto *store = dynamic_cast<QtFavoriteStore *>(object);
            store && store->property("settingsPath").toString() == window.toolbarSettingsPath())
            favorites_ = store;
    }
    if (!nativePlaces_)
        nativePlaces_ = new QtNativePlaces(qApp);
    if (!favorites_) {
        favorites_ = new QtFavoriteStore(window.toolbarSettingsPath(), qApp);
        favorites_->setProperty("settingsPath", window.toolbarSettingsPath());
    }
}

bool QtWindowWorkspace::Ready() const {
    return !changing_ && !window_.app_.QtOperations().closed() &&
           !window_.app_.QtOperations().active() && !window_.app_.QtOperations().callActive() &&
           !window_.sourceArchiveProgress_ &&
           (!QApplication::activeModalWidget() ||
            QApplication::activeModalWidget() == recentDialog_) &&
           !window_.backend_->busy() && (!window_.drag_ || !window_.drag_->Active()) &&
           (!window_.appPresentation_ || !window_.appPresentation_->IsCopyPending());
}

std::optional<QtWindowWorkspace::TabPosition> QtWindowWorkspace::FindTab(const QString &id) const {
    if (id.isEmpty())
        return {};
    for (unsigned panel = 0; panel < panels_.size(); ++panel)
        for (int index = 0; index < int(panels_[panel].tabs.size()); ++index)
            if (panels_[panel].tabs[index].id == id)
                return TabPosition{panel, index};
    return {};
}

void QtWindowWorkspace::AttachPanel(unsigned panel, QVBoxLayout &layout) {
    auto &state = panels_[panel];
    if (state.bar) {
        if (state.tabs.empty()) {
            Tab home;
            home.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            home.path = QDir::homePath();
            state.tabs.push_back(std::move(home));
            state.current = 0;
        }
        if (!state.bound) {
            auto &runtime = state.tabs[state.current].runtime;
            if (!runtime)
                runtime = std::make_shared<QtTabRuntime>();
            runtime->source = window_.app_.Panels.Share(panel);
            state.bound = runtime;
        }
        layout.insertWidget(0, state.bar->parentWidget());
        layout.addWidget(state.actions);
        state.bar->parentWidget()->show();
        state.actions->show();
        return;
    }
    auto *row = new QWidget;
    auto *tabs = new QHBoxLayout(row);
    QtWorkspaceTheme::surface(*row, QtWorkspaceTheme::colors().window);
    tabs->setContentsMargins(8, 6, 8, 0);
    tabs->setSpacing(0);
    state.bar = new QtWorkspaceTabBar(*this, panel, row);
    state.bar->setObjectName(QStringLiteral("panelTabs%1").arg(panel));
    state.bar->setDocumentMode(true);
    state.bar->setExpanding(false);
    state.bar->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    state.bar->setMovable(true);
    state.bar->setTabsClosable(true);
    state.bar->setElideMode(Qt::ElideRight);
    state.bar->setUsesScrollButtons(true);
    state.bar->setContextMenuPolicy(Qt::CustomContextMenu);
    tabs->addWidget(state.bar);
    auto *add = new QToolButton(row);
    add->setText(QStringLiteral("+"));
    add->setAccessibleName(tr("New tab"));
    QtBindLocalText(add, "QtWindowWorkspace", QT_TRANSLATE_NOOP("QtWindowWorkspace", "New tab"),
                    true);
    QtBindLocalToolTip(add, "QtWindowWorkspace", QT_TRANSLATE_NOOP("QtWindowWorkspace", "New tab"));
    add->setToolTip(tr("New tab"));
    add->setAutoRaise(true);
    QtConfigureChromeToolButton(*add);
    add->setFixedSize(30, 30);
    tabs->addWidget(add);
    tabs->addStretch();
    layout.insertWidget(0, row);
    CreateActions(panel, layout);
    Tab initial;
    initial.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    initial.runtime = std::make_shared<QtTabRuntime>();
    initial.runtime->source = window_.app_.Panels.Share(panel);
    state.bound = initial.runtime;
    state.tabs.push_back(initial);
    Rebuild(panel);
    connect(add, &QToolButton::clicked, this, [this, panel] { NewTab(panel); });
    connect(state.bar, &QTabBar::currentChanged, this, [this, panel](int) {
        QTimer::singleShot(0, this,
                           [this, panel] { Activate(panel, panels_[panel].bar->currentIndex()); });
    });
    connect(state.bar, &QTabBar::tabCloseRequested, this,
            [this, panel](int index) { CloseTab(panel, index); });
    connect(state.bar, &QTabBar::customContextMenuRequested, this,
            [this, panel](const QPoint &point) { TabMenu(panel, point); });
    connect(state.bar, &QTabBar::tabMoved, this, [this, panel](int from, int to) {
        auto &state = panels_[panel];
        auto tab = std::move(state.tabs[from]);
        state.tabs.erase(state.tabs.begin() + from);
        state.tabs.insert(state.tabs.begin() + to, std::move(tab));
        state.current = state.bar->currentIndex();
    });
}

void QtWindowWorkspace::Capture(unsigned panel, bool retainView) {
    auto &state = panels_[panel];
    if (!state.bar || state.tabs.empty() || !window_.panelUi_[panel].table)
        return;
    auto &tab = state.tabs[state.current];
    auto &source = window_.app_.Panels[panel];
    if (!window_.panelUi_[panel].locationUnavailable && source.ShouldProcessNotifications()) {
        tab.unavailable = false;
        source.LoadFullPath();
        tab.path = ToQString(source.GetCurrentFolderPrefix());
        tab.nativePlaceIdentity = QtNativePlaces::RootIdentity(source);
        if (source.IsArcFolder())
            tab.archiveLocation = QtArchiveLocation::Capture(
                source, static_cast<QtPanelAddressCombo *>(window_.panelUi_[panel].pathCombo)
                            ->LocationSegments());
        else
            tab.archiveLocation = {};
        tab.symbol = source.IsArcFolder() ? QtCommandIcon::Archive : QtCommandIcon::Folder;
        if (retainView)
            source.SaveSelectedState(tab.selection);
        tab.viewMode = int(source.GetListViewMode());
        tab.sort = source.GetSortID();
        tab.ascending = source._ascending;
        if (retainView)
            if (auto *view = qobject_cast<QAbstractItemView *>(
                    window_.panelUi_[panel].viewStack->currentWidget()))
                tab.scroll = {view->horizontalScrollBar()->value(),
                              view->verticalScrollBar()->value()};
    }
}

QString QtWindowWorkspace::TabTitle(const Tab &tab) const {
    if (tab.nativePlaceIdentity == QStringLiteral("locations"))
        return tr("Locations");
    if (const auto place = nativePlaces_->FindIdentity(tab.nativePlaceIdentity); place.isValid())
        return place.data().toString();
    const QString clean = QDir::cleanPath(tab.path);
    if (clean == QDir::homePath())
        return tr("Home");
    const QString name = QFileInfo(clean).fileName();
    return name.isEmpty() ? (tab.path.isEmpty() ? tr("Locations") : tab.path) : name;
}

void QtWindowWorkspace::Rebuild(unsigned panel) {
    auto &state = panels_[panel];
    if (!state.bar)
        return;
    const QSignalBlocker blocker(state.bar);
    while (state.bar->count())
        state.bar->removeTab(0);
    for (const auto &tab : state.tabs) {
        const int index = state.bar->addTab(QtCommandSymbol(tab.symbol), TabTitle(tab));
        state.bar->setTabToolTip(index, tab.path);
        state.bar->setTabData(index, tab.id);
        for (const auto side : {QTabBar::LeftSide, QTabBar::RightSide})
            if (auto *close = state.bar->tabButton(index, side))
                close->setStyle(state.bar->style());
    }
    state.bar->setCurrentIndex(state.current);
}

void QtWindowWorkspace::Refresh() {
    if (changing_)
        return;
    nativePlaces_->Refresh();
    for (unsigned panel = 0; panel < 2; ++panel) {
        auto &state = panels_[panel];
        if (!state.bar || !window_.panelUi_[panel].viewStack)
            continue;
        Capture(panel, false);
        auto &tab = state.tabs[state.current];
        for (int index = 0; index < int(state.tabs.size()); ++index)
            state.bar->setTabText(index, TabTitle(state.tabs[index]));
        state.bar->setTabIcon(state.current, QtCommandSymbol(tab.symbol));
        state.bar->setTabToolTip(state.current, tab.path);
        auto &ui = window_.panelUi_[panel];
        if (tab.unavailable)
            ui.navigation->SetLocation(tab.path);
        else if (window_.app_.Panels[panel].ShouldProcessNotifications()) {
            auto *address = static_cast<QtPanelAddressCombo *>(ui.pathCombo);
            ui.navigation->SetLocation(tab.path, address->LocationSegments());
        }
        state.bar->parentWidget()->setEnabled(Ready());
    }
    RefreshChrome();
}

void QtWindowWorkspace::NewTab(unsigned panel, const QString &path) {
    CreateTab(panel, path.isEmpty() ? QDir::homePath() : path, {});
}

void QtWindowWorkspace::CreateTab(unsigned panel, const QString &path,
                                  const QString &nativeIdentity, const QtArchiveLocation &archive) {
    if (!Ready() || panel >= 2 || !panels_[panel].bar)
        return;
    Capture(panel);
    Tab tab;
    tab.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    tab.path = path;
    tab.nativePlaceIdentity = nativeIdentity;
    tab.archiveLocation = archive;
    auto &state = panels_[panel];
    state.tabs.push_back(std::move(tab));
    Rebuild(panel);
    Activate(panel, int(state.tabs.size()) - 1);
}

void QtWindowWorkspace::Activate(unsigned panel, int index) {
    auto &state = panels_[panel];
    if (changing_ || index < 0 || index >= int(state.tabs.size()))
        return;
    if (state.current == index && state.bound == state.tabs[index].runtime &&
        window_.panelUi_[panel].container) {
        window_.activatePanelUi(panel);
        return;
    }
    if (!Ready()) {
        const QSignalBlocker blocker(state.bar);
        state.bar->setCurrentIndex(state.current);
        return;
    }
    const bool keepTabFocus = state.bar->hasFocus();
    const auto request = ++focusRequest_;
    if (index != state.current)
        Capture(panel);
    {
        const QScopedValueRollback<bool> changing(changing_, true);
        const auto peer = window_.app_.Panels.Share(1 - panel);
        const CPanel::CDisableNotify peerNotifications(*peer);
        const QScopedValueRollback<unsigned> projection(window_.panelProjectionDepth_,
                                                        window_.panelProjectionDepth_ + 1);
        if (window_.panelUi_[panel].container)
            window_.activatePanelUi(panel);
        else {
            window_.projectedPanel_ = panel;
            window_.app_.SetFocusedPanel(panel);
        }
        Suspend(panel);
        state.current = index;
        auto &tab = state.tabs[index];
        auto &ui = window_.panelUi_[panel];
        if (!tab.runtime) {
            tab.runtime = std::make_shared<QtTabRuntime>();
            tab.runtime->source = std::make_shared<CPanel>();
            auto &source = *tab.runtime->source;
            source.AttachFolderManager(window_.folderManager_, window_.toolbarSettingsPath());
            source.AttachExternalFileServices(window_.associatedFileServices_.get());
            window_.app_.Panels.Bind(panel, tab.runtime->source);
            window_.connectFilesystemScanner(panel);
            COpenResult opened;
            // Construct the normal presentation at Home, then bind the saved
            // descriptor. A failed reopen keeps that descriptor visibly failed.
            window_.app_.CreateOnePanel(panel, ToUString(QDir::homePath()), UString(), false,
                                        opened);
            source.SetListViewMode(tab.viewMode);
            if (tab.sort) {
                source._sortID = tab.sort;
                source._ascending = tab.ascending;
            }
            OpenTabLocation(panel);
        } else {
            window_.app_.Panels.Bind(panel, tab.runtime->source);
            ui = std::move(tab.runtime->ui);
            tab.runtime->ui = {};
            window_.panelSplitter_->insertWidget(int(panel), ui.container);
            ui.container->show();
            state.sidebar =
                ui.container->findChild<QWidget *>(QStringLiteral("placesSidebar%1").arg(panel));
            auto *layout = qobject_cast<QVBoxLayout *>(ui.container->layout());
            layout->insertWidget(0, state.bar->parentWidget());
            layout->insertWidget(layout->indexOf(ui.navigation->parentWidget()) + 1, state.actions);
            state.bar->parentWidget()->show();
            state.actions->show();
            if (tab.runtime->dirty) {
                const QSignalBlocker selection(ui.table->selectionModel());
                ui.table->clearRows();
            }
            window_.setState(ui.state);
            if (tab.runtime->dirty) {
                tab.runtime->dirty = false;
                window_.refreshCurrent(&tab.selection);
            }
        }
        state.bound = tab.runtime;
        window_.app_.SetPanels_AutoRefresh_Mode();
        if (window_.drag_)
            window_.drag_->AttachPanel(panel);
        Rebuild(panel);
    }
    window_.refreshActions();
    if (keepTabFocus)
        RestoreTabFocus(panel, state.tabs[state.current].id, request);
    else
        window_.app_.Panels[panel].SetFocusToList();
}

void QtWindowWorkspace::Suspend(unsigned panel) {
    auto &state = panels_[panel];
    auto &ui = window_.panelUi_[panel];
    if (!ui.container || !state.bound)
        return;
    window_.renameController_->Cancel(panel);
    ui.filesystemScanner->cancelAndWait();
    window_.cancelListPublication();
    window_.app_.QtOperations().End();
    ui.container->hide();
    ui.container->setParent(&window_);
    state.bound->ui = std::move(ui);
    ui = {};
}

void QtWindowWorkspace::CloseTab(unsigned panel, int index) {
    if (!Ready() || index < 0 || index >= int(panels_[panel].tabs.size()))
        return;
    auto &state = panels_[panel];
    if (state.tabs.size() == 1) {
        if (window_.app_.NumPanels == 1) {
            window_.close();
            return;
        }
        window_.activatePanelUi(1 - panel);
        window_.togglePanels();
        window_.withPanelProjection(panel, [this, panel] { Suspend(panel); });
        state.bar->parentWidget()->setParent(&window_);
        state.bar->parentWidget()->hide();
        state.actions->setParent(&window_);
        state.actions->hide();
        state.sidebar = nullptr;
        state.bound.reset();
        state.tabs.front() = Tab{};
        state.tabs.front().id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        state.tabs.front().path = QDir::homePath();
        auto empty = std::make_shared<CPanel>();
        empty->AttachFolderManager(window_.folderManager_, window_.toolbarSettingsPath());
        empty->AttachExternalFileServices(window_.associatedFileServices_.get());
        window_.app_.Panels.Bind(panel, std::move(empty));
        Rebuild(panel);
        return;
    }
    Capture(panel);
    const bool active = index == state.current;
    const auto closing = state.tabs[index].runtime;
    if (active)
        Suspend(panel);
    state.tabs.erase(state.tabs.begin() + index);
    if (index < state.current || state.current >= int(state.tabs.size()))
        --state.current;
    Rebuild(panel);
    if (active)
        Activate(panel, state.current);
}

void QtWindowWorkspace::EditLocation() { window_.projectedUi().navigation->Edit(); }

QString QtWindowWorkspace::CurrentLocation() {
    const unsigned panel = window_.projectedPanel_;
    if (!changing_)
        Capture(panel, false);
    const auto &state = panels_[panel];
    return state.tabs.empty() ? QString() : state.tabs[state.current].path;
}
