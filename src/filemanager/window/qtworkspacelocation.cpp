#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtnativeplaces.h"
#include "filemanager/window/qtnavigationbar.h"
#include "filemanager/window/qtwindowworkspace.h"
#include <QScopedValueRollback>
#include <QScrollBar>

HRESULT QtWindowWorkspace::OpenRecordedLocation(unsigned panel, const QString &path,
                                                const QString &nativeIdentity,
                                                const QtArchiveLocation &archive) {
    if (!archive.physicalPath.isEmpty()) {
        ProtectNavigation();
        const HRESULT result = OpenArchiveLocation(panel, archive);
        if (result == S_OK)
            window_.panelUi_[panel].locationUnavailable = false;
        return result;
    }
    QString target = window_.sourcePathFromDisplayPath(path);
    if (!nativeIdentity.isEmpty()) {
        nativePlaces_->Refresh();
        const auto place = nativePlaces_->FindIdentity(nativeIdentity);
        if (nativeIdentity == "locations")
            target.clear();
        else if (place.isValid())
            target = place.data(QtPlaceRoles::Path).toString();
        else
            return E_FAIL;
    }
    const HRESULT result = window_.navigateTo(target, true);
    if (result != S_OK)
        return result;
    if (!nativeIdentity.isEmpty())
        return QtNativePlaces::RootIdentity(window_.app_.Panels[panel]) == nativeIdentity ? S_OK
                                                                                          : S_FALSE;
    return LocationMatches(panel, target) ? S_OK : S_FALSE;
}

HRESULT QtWindowWorkspace::OpenPanelLocation(unsigned panel) {
    auto &locationState = panels_[panel].location;
    const QString identity = locationState.nativePlaceIdentity;
    const bool recorded =
        !identity.isEmpty() || !locationState.archiveLocation.physicalPath.isEmpty();
    const HRESULT result = recorded ? OpenRecordedLocation(panel, locationState.path, identity,
                                                           locationState.archiveLocation)
                                    : window_.navigateTo(locationState.path, true);
    if (!identity.isEmpty() && result == S_OK) {
        auto &source = window_.app_.Panels[panel];
        source.LoadFullPath();
        locationState.path = ToQString(source.GetCurrentFolderPrefix());
    }
    CompleteLocationOpen(panel, locationState.path, result);
    return locationState.unavailable && result == S_OK ? S_FALSE : result;
}

void QtWindowWorkspace::RestoreViewPosition(CPanel &source, QAbstractItemView &view,
                                            const QPoint &position) {
    const UInt64 revision = source.FolderBindingRevision();
    const auto request = focusRequest_;
    WhenReady([this, owner = QPointer<CPanel>(&source), target = QPointer<QAbstractItemView>(&view),
               position, revision, request] {
        if (focusRequest_ != request || !owner || owner->FolderBindingRevision() != revision ||
            !target || !target->isVisible())
            return;
        target->horizontalScrollBar()->setValue(position.x());
        target->verticalScrollBar()->setValue(position.y());
    });
}

bool QtWindowWorkspace::LocationMatches(unsigned panel, const QString &path) {
    auto &source = window_.app_.Panels[panel];
    source.LoadFullPath();
    const QString actual = ToQString(source.GetCurrentFolderPrefix());
    const auto normalized = [](const QString &value) {
        return value.isEmpty() ? QString() : QDir::cleanPath(QDir::fromNativeSeparators(value));
    };
    bool matched = normalized(actual) == normalized(path);
    if (!matched && QFileInfo(path).isDir())
        matched = QFileInfo(path).canonicalFilePath() == QFileInfo(actual).canonicalFilePath();
    return matched;
}

void QtWindowWorkspace::CompleteLocationOpen(unsigned panel, const QString &path, HRESULT result) {
    auto &ui = window_.panelUi_[panel];
    auto &locationState = panels_[panel].location;
    locationState.unavailable = result != S_OK || !LocationMatches(panel, path);
    ui.locationUnavailable = locationState.unavailable;
    if (!locationState.unavailable)
        return;
    locationState.path = path;
    ui.filesystemScanner->cancelAndWait();
    window_.cancelListPublication();
    window_.setState(ui.state);
    ui.table->clearRows();
    window_.showPanelStatus(panel, {});
    ui.path->setText(path);
    ui.navigation->SetLocation(path);
    const QString message = result == E_ABORT ? tr("Opening canceled: %1").arg(path)
                                              : tr("Location unavailable: %1").arg(path);
    ui.navigationError->setText(message);
    ui.navigationNotice->show();
    window_.showStatusMessage(message);
}

void QtWindowWorkspace::RetryLocation(unsigned panel, bool parent) {
    if (!Ready())
        return;
    window_.activatePanelUi(panel);
    auto &locationState = panels_[panel].location;
    if (parent && locationState.archiveRevision != locationState.archiveApplied) {
        window_.goParent();
        return;
    }
    if (!parent && locationState.archiveRevision != locationState.archiveApplied) {
        RefreshPublishedArchive(panel, true);
        return;
    }
    if (parent) {
        locationState.path = locationState.archiveLocation.physicalPath.isEmpty()
                                 ? QFileInfo(QDir::cleanPath(locationState.path)).absolutePath()
                                 : locationState.archiveLocation.MoveToParent();
        locationState.nativePlaceIdentity.clear();
        locationState.archiveApplied = locationState.archiveRevision;
    }
    const QString requested = locationState.path;
    window_.withPanelProjection(panel, [this, panel, &requested] {
        const HRESULT result = Navigate([this, panel]() -> HRESULT {
            ProtectNavigation();
            const QScopedValueRollback<bool> changing(changing_, true);
            return OpenPanelLocation(panel);
        });
        if (result != S_OK)
            CompleteLocationOpen(panel, requested, result);
    });
    window_.refreshActions();
}
