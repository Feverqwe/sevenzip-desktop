#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtnativeplaces.h"
#include "filemanager/window/qtwindowworkspace.h"
#include <QScopeGuard>
#include <QScopedValueRollback>
#include <QScrollBar>

bool QtWindowWorkspace::Visit::SameLocation(const Visit &other) const {
    if (!nativePlaceIdentity.isEmpty() || !other.nativePlaceIdentity.isEmpty())
        return nativePlaceIdentity == other.nativePlaceIdentity;
    return path == other.path && archiveLocation.Save() == other.archiveLocation.Save();
}

QtWindowWorkspace::Visit QtWindowWorkspace::CaptureVisit(unsigned panel) {
    const auto &state = panels_[panel];
    const auto &current = state.location;
    if (window_.panelUi_[panel].locationUnavailable && current.cursor >= 0)
        return current.visits[current.cursor];
    Capture(panel);
    const auto &locationState = panels_[panel].location;
    return {locationState.path, locationState.nativePlaceIdentity, locationState.archiveLocation,
            locationState.selection, locationState.scroll};
}

HRESULT QtWindowWorkspace::Navigate(const std::function<HRESULT()> &body,
                                    std::function<void(HRESULT)> finished) {
    if (window_.app_.QtOperations().closed())
        return E_ABORT;
    const unsigned panel = window_.projectedPanel_;
    if (changing_ || restoringNavigation_ || !window_.panelUi_[panel].table)
        return body();
    if (navigation_[panel] && navigation_[panel]->depth == 0)
        window_.cancelListPublication();
    if (!navigation_[panel]) {
        auto request = std::make_unique<Navigation>();
        request->source = window_.app_.Panels.Share(panel);
        request->origin = CaptureVisit(panel);
        request->finished = std::move(finished);
        navigation_[panel] = std::move(request);
    }
    HRESULT result = E_FAIL;
    try {
        auto &request = *navigation_[panel];
        const QScopedValueRollback<unsigned> depth(request.depth, request.depth + 1);
        result = body();
    } catch (...) {
        if (navigation_[panel] && navigation_[panel]->depth == 0)
            FinishNavigation(panel, E_FAIL);
        throw;
    }
    auto &request = navigation_[panel];
    if (request && request->depth == 0) {
        if (request->canceled)
            result = E_ABORT;
        const auto &ui = window_.panelUi_[panel];
        if (result != S_OK || (!ui.scanningDisk && !ui.scanningArchive))
            FinishNavigation(panel, result == S_OK ? request->completed.value_or(S_OK) : result);
    }
    return result;
}

void QtWindowWorkspace::QuiesceNavigation() {
    const QScopedValueRollback<bool> progress(window_.sourceArchiveProgress_, true);
    for (auto &pending : navigation_) {
        auto request = std::move(pending);
        if (!request)
            continue;
        if (request->transaction)
            request->transaction->Rollback();
        else if (request->previousFolder) {
            while (request->source->GetParentFolders().Size() > request->previousDepth)
                request->source->CloseOneLevel();
            request->source->SetNewFolder(request->previousFolder);
        }
    }
}

void QtWindowWorkspace::NavigationFinished(HRESULT result) {
    const unsigned panel = window_.projectedPanel_;
    if (auto &request = navigation_[panel]) {
        request->completed = result;
        if (request->depth == 0)
            FinishNavigation(panel, result);
    }
}

void QtWindowWorkspace::ProtectNavigation(const QString &path) {
    auto &request = navigation_[window_.projectedPanel_];
    if (!request || request->transaction || request->previousFolder)
        return;
    auto &source = *request->source;
    const auto &parents = source.GetParentFolders();
    if (source.IsArcFolder() && !parents.IsEmpty()) {
        // Match the existing native reader boundary, just as BindToPath does.
        // Parking this chain would lose native virtual-to-materialized bindings.
        const UString target = ToUString(path);
        const UString &prefix = parents.Back().VirtualPath;
        if (target.IsPrefixedBy(prefix) &&
            (target.Len() == prefix.Len() || IS_PATH_SEPAR(target[prefix.Len()]))) {
            request->previousFolder = source.GetFolder();
            request->previousDepth = parents.Size();
            return;
        }
    }
    request->transaction = std::make_unique<QtPanelLocationTransaction>(source);
}

void QtWindowWorkspace::RetainNavigationFolder() {
    auto &request = navigation_[window_.projectedPanel_];
    if (request && !request->transaction && !request->previousFolder) {
        request->previousFolder = request->source->GetFolder();
        request->previousDepth = request->source->GetParentFolders().Size();
    }
}

std::optional<HRESULT> QtWindowWorkspace::NavigationPublication() const {
    const auto &request = navigation_[window_.projectedPanel_];
    return request ? request->completed : std::nullopt;
}

void QtWindowWorkspace::NavigationCanceled() {
    const unsigned panel = window_.projectedPanel_;
    if (navigation_[panel] && navigation_[panel]->depth == 0)
        FinishNavigation(panel, E_ABORT);
}

void QtWindowWorkspace::FinishNavigation(unsigned panel, HRESULT result) {
    auto request = std::move(navigation_[panel]);
    if (!request)
        return;
    if (request->canceled)
        result = E_ABORT;
    const auto complete = qScopeGuard([&] {
        if (request->finished)
            request->finished(result);
    });
    if (window_.app_.Panels.Share(panel) != request->source) {
        result = E_ABORT;
        request->transaction.reset();
        return;
    }
    if (result == S_FALSE && !request->completed && request->previousFolder &&
        request->previousFolder == request->source->GetFolder())
        request->previousFolder.Release();
    if (request->destination >= 0) {
        const auto &target = panels_[panel].location.visits[request->destination];
        request->source->LoadFullPath();
        if (result == S_OK &&
            (target.nativePlaceIdentity.isEmpty()
                 ? ToQString(request->source->GetCurrentFolderPrefix()) != target.path
                 : QtNativePlaces::RootIdentity(*request->source) != target.nativePlaceIdentity))
            result = S_FALSE;
    }
    if (request->transaction || request->previousFolder) {
        const QScopedValueRollback<bool> restoring(restoringNavigation_, true);
        const QScopedValueRollback<bool> progress(window_.sourceArchiveProgress_, true);
        if (result == S_OK) {
            if (request->transaction)
                request->transaction->Commit();
        } else {
            window_.cancelListPublication();
            // Release retained scan guards before destroying the checkpoint's
            // notification guard, which holds the original idle state.
            window_.setState(window_.panelUi_[panel].state);
            if (request->transaction)
                request->transaction->Rollback();
            else if (request->previousFolder) {
                while (request->source->GetParentFolders().Size() > request->previousDepth)
                    request->source->CloseOneLevel();
                request->source->SetNewFolder(request->previousFolder);
            }
        }
        request->transaction.reset();
        if (result != S_OK) {
            window_.renderBoundFolder();
            window_.restoreListSelection(request->origin.selection);
            if (auto *view = qobject_cast<QAbstractItemView *>(
                    window_.panelUi_[panel].viewStack->currentWidget()))
                RestoreViewPosition(*request->source, *view, request->origin.scroll);
        }
    }
    if (result != S_OK) {
        window_.refreshActions();
        return;
    }
    const Visit accepted = CaptureVisit(panel);
    auto &locationState = panels_[panel].location;
    if (locationState.cursor < 0) {
        locationState.visits.push_back(request->origin);
        locationState.cursor = 0;
    } else if (locationState.visits[locationState.cursor].SameLocation(request->origin))
        locationState.visits[locationState.cursor] = request->origin;
    if (request->destination >= 0) {
        locationState.cursor = request->destination;
        const auto target = locationState.visits[locationState.cursor];
        window_.restoreListSelection(target.selection);
        if (auto *view = qobject_cast<QAbstractItemView *>(
                window_.panelUi_[panel].viewStack->currentWidget()))
            RestoreViewPosition(*request->source, *view, target.scroll);
    } else if (!locationState.visits[locationState.cursor].SameLocation(accepted)) {
        locationState.visits.resize(locationState.cursor + 1);
        locationState.visits.push_back(accepted);
        ++locationState.cursor;
    }
    window_.recordVisitedPath(accepted.path);
    window_.refreshActions();
    if (locationState.archiveRevision != locationState.archiveApplied) {
        locationState.archiveAttempted = 0;
        RefreshPublishedArchive(panel);
    }
}

bool QtWindowWorkspace::CanTravel(unsigned panel, int direction) const {
    const auto &state = panels_[panel];
    if (!window_.panelUi_[panel].table || navigation_[panel])
        return false;
    const auto &locationState = state.location;
    const auto &ui = window_.panelUi_[panel];
    const int destination = locationState.cursor + direction;
    return locationState.cursor >= 0 && destination >= 0 &&
           destination < int(locationState.visits.size()) && !ui.scanningDisk &&
           !ui.scanningArchive && !ui.locationUnavailable;
}

void QtWindowWorkspace::Travel(int direction) {
    const unsigned panel = window_.projectedPanel_;
    // The native command dispatcher owns a CallScope while invoking Back/Forward.
    if (changing_ || window_.app_.QtOperations().closed() ||
        window_.app_.QtOperations().blocking() || window_.sourceArchiveProgress_ ||
        QApplication::activeModalWidget() || !CanTravel(panel, direction))
        return;
    const auto &locationState = panels_[panel].location;
    const int destination = locationState.cursor + direction;
    const Visit target = locationState.visits[destination];
    Navigate([&] {
        auto &request = *navigation_[panel];
        request.destination = destination;
        auto &source = *request.source;
        // An existing reader can bind another directory in its own chain.
        // Different chains are probed while the previous native source is parked.
        auto currentChain = request.origin.archiveLocation;
        auto targetChain = target.archiveLocation;
        currentChain.folder.clear();
        currentChain.directories.clear();
        targetChain.folder.clear();
        targetChain.directories.clear();
        const bool sameChain =
            !targetChain.physicalPath.isEmpty() && currentChain.Save() == targetChain.Save();
        if (sameChain) {
            request.previousFolder = source.GetFolder();
            request.previousDepth = source.GetParentFolders().Size();
        } else
            request.transaction = std::make_unique<QtPanelLocationTransaction>(source);
        if (sameChain)
            return OpenArchiveDirectory(panel, target.archiveLocation);
        if (!target.archiveLocation.physicalPath.isEmpty() && !sameChain)
            return OpenArchiveLocation(panel, target.archiveLocation);
        QString path = target.path;
        if (!target.nativePlaceIdentity.isEmpty()) {
            nativePlaces_->Refresh();
            const auto place = nativePlaces_->FindIdentity(target.nativePlaceIdentity);
            if (target.nativePlaceIdentity == QStringLiteral("locations"))
                path.clear();
            else if (place.isValid())
                path = place.data(QtPlaceRoles::Path).toString();
            else
                return HRESULT(E_FAIL);
        }
        return window_.navigateTo(path, true);
    });
}
