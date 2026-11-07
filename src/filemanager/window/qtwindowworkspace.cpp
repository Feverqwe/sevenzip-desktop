#include "filemanager/window/qtwindowworkspace.h"
#include "dialogs/recentplaces/qtrecentplacesdialog.h"
#include "filemanager/panels/qtpaneladdresscombo.h"
#include "filemanager/qtfilemanagerdrag.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtapppresentation.h"
#include "filemanager/window/qtfastfoldersstore.h"
#include "filemanager/window/qtfavoritestore.h"
#include "filemanager/window/qtnativeplaces.h"
#include "filemanager/window/qtnavigationbar.h"
#include "filemanager/window/qtrecentplacesstore.h"
#include <QScrollBar>

QtWindowWorkspace::QtWindowWorkspace(MainWindow &window) : QObject(&window), window_(window) {
    fastFolders_ = QtFastFoldersStore::ForSettings(window.toolbarSettingsPath());
    connect(fastFolders_, &QtFastFoldersStore::changed, this,
            [this] { fastFolders_->CopyTo(window_.app_.AppState.FastFolders); });
    connect(fastFolders_, &QtFastFoldersStore::published, this, [this] {
        if (!window_.app_.QtOperations().closed() && window_.openBookmarks_[0] &&
            window_.saveBookmarks_[0])
            window_.refreshBookmarks();
    });
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

void QtWindowWorkspace::SetBookmark(unsigned index, const UString &path) {
    fastFolders_->Set(index, path);
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

void QtWindowWorkspace::AttachPanel(unsigned panel, QVBoxLayout &layout) {
    CreateActions(panel, layout);
}

void QtWindowWorkspace::Capture(unsigned panel, bool retainView) {
    if (!window_.panelUi_[panel].table)
        return;
    auto &locationState = panels_[panel].location;
    auto &source = window_.app_.Panels[panel];
    if (source.ShouldProcessNotifications() &&
        locationState.archiveRevision != locationState.archiveApplied &&
        !source.ArchiveContextOutdated() &&
        (!source.IsArcFolder() || source.GetParentFolders().IsEmpty() ||
         ToQString(source.GetParentFolders().Front().VirtualPath) !=
             locationState.archiveLocation.physicalPath))
        locationState.archiveApplied = locationState.archiveRevision;
    if (!window_.panelUi_[panel].locationUnavailable && source.ShouldProcessNotifications()) {
        locationState.unavailable = false;
        source.LoadFullPath();
        locationState.path = ToQString(source.GetCurrentFolderPrefix());
        locationState.nativePlaceIdentity = QtNativePlaces::RootIdentity(source);
        if (source.IsArcFolder())
            locationState.archiveLocation = QtArchiveLocation::Capture(
                source, static_cast<QtPanelAddressCombo *>(window_.panelUi_[panel].pathCombo)
                            ->LocationSegments());
        else
            locationState.archiveLocation = {};
        if (retainView)
            source.SaveSelectedState(locationState.selection);
        locationState.viewMode = int(source.GetListViewMode());
        locationState.sort = source.GetSortID();
        locationState.ascending = source._ascending;
        if (retainView)
            if (auto *view = qobject_cast<QAbstractItemView *>(
                    window_.panelUi_[panel].viewStack->currentWidget()))
                locationState.scroll = {view->horizontalScrollBar()->value(),
                                        view->verticalScrollBar()->value()};
    }
}

void QtWindowWorkspace::Refresh() {
    if (changing_)
        return;
    nativePlaces_->Refresh();
    for (unsigned panel = 0; panel < 2; ++panel) {
        if (!window_.panelUi_[panel].viewStack)
            continue;
        Capture(panel, false);
        auto &locationState = panels_[panel].location;
        auto &ui = window_.panelUi_[panel];
        if (locationState.unavailable)
            ui.navigation->SetLocation(locationState.path);
        else if (window_.app_.Panels[panel].ShouldProcessNotifications()) {
            auto *address = static_cast<QtPanelAddressCombo *>(ui.pathCombo);
            ui.navigation->SetLocation(locationState.path, address->LocationSegments());
        }
    }
    RefreshChrome();
}

void QtWindowWorkspace::EditLocation() { window_.projectedUi().navigation->Edit(); }

QString QtWindowWorkspace::CurrentLocation() {
    const unsigned panel = window_.projectedPanel_;
    if (!changing_)
        Capture(panel, false);
    return panels_[panel].location.path;
}
