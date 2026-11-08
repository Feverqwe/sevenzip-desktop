#include "filemanager/panels/qtpaneladdresscombo.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtnativeplaces.h"
#include "filemanager/window/qtnavigationbar.h"
#include "filemanager/window/qtwindowworkspace.h"
#include "ui/controls/qtwindowstatusbar.h"
#include <QScopedValueRollback>
#include <QScrollBar>
#include <utility>

HRESULT QtWindowWorkspace::OpenFolderInPeer(unsigned source, std::optional<UInt32> item) {
    const auto &ui = window_.panelUi_[source];
    const auto &operations = window_.app_.QtOperations();
    if (changing_ || operations.closed() || operations.blocking() ||
        window_.sourceArchiveProgress_ || QApplication::activeModalWidget() || ui.scanningDisk ||
        ui.scanningArchive || ui.locationUnavailable)
        return E_ABORT;
    auto target = CaptureVisit(source);
    const auto &panel = window_.app_.Panels[source];
    if (item && panel.IsFSFolder()) {
        target.path += ToQString(panel.GetItemRelPath(*item));
        if (!QFileInfo(target.path).isDir()) {
            window_.showStatusMessage(tr("Location unavailable: %1").arg(target.path));
            return S_FALSE;
        }
    } else if (item) {
        auto &archive = target.archiveLocation;
        if (archive.physicalPath.isEmpty() || !QFileInfo::exists(archive.physicalPath)) {
            window_.showStatusMessage(tr("Location unavailable: %1").arg(target.path));
            return S_FALSE;
        }
        if (*item == UInt32(-1))
            target.path = archive.MoveToParent();
        else {
            UString path = ToUString(target.path);
            UString folder = ToUString(archive.folder);
            const auto relative =
                QDir::fromNativeSeparators(ToQString(panel.GetItemRelPath(*item)));
            for (const auto &component : relative.split(u'/', Qt::SkipEmptyParts)) {
                path += ToUString(component);
                path.Add_PathSepar();
                folder += ToUString(component);
                folder.Add_PathSepar();
                archive.directories.append({component, ToQString(path)});
            }
            target.path = ToQString(path);
            archive.folder = ToQString(folder);
        }
    }
    const unsigned peer = 1 - source;
    HRESULT outcome = E_ABORT;
    window_.withPanelProjection(peer, [&] {
        auto &destination = window_.panelUi_[peer];
        auto *address = static_cast<QtPanelAddressCombo *>(destination.pathCombo);
        const auto revision = address->UserEditRevision();
        const auto draft = destination.path->isModified()
                               ? std::optional(QtAddressDraft(*destination.path))
                               : std::nullopt;
        outcome = Navigate(
            [&] {
                return OpenRecordedLocation(peer, target.path, target.nativePlaceIdentity,
                                            target.archiveLocation);
            },
            [this, source, peer, path = target.path, revision, draft](HRESULT result) {
                if (window_.app_.QtOperations().closed())
                    return;
                if (result != S_OK) {
                    auto &owner = window_.panelUi_[peer];
                    auto *combo = static_cast<QtPanelAddressCombo *>(owner.pathCombo);
                    const auto restored =
                        combo->UserEditRevision() == revision ? draft : combo->LastUserDraft();
                    if (restored) {
                        restored->Restore(*owner.path);
                        owner.path->setModified(true);
                    }
                }
                if (result != E_ABORT)
                    window_.statusStrip_->SetMessage(
                        &window_.app_.Panels[source],
                        result == S_OK ? QString() : tr("Location unavailable: %1").arg(path));
            });
    });
    return outcome;
}

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
    const auto interaction = interactionRevision_;
    auto restore = [this, owner = QPointer<CPanel>(&source),
                    target = QPointer<QAbstractItemView>(&view), position, revision, interaction] {
        if (interactionRevision_ != interaction || !owner ||
            owner->FolderBindingRevision() != revision || !target || !target->isVisible())
            return;
        target->doItemsLayout();
        target->horizontalScrollBar()->setValue(position.x());
        target->verticalScrollBar()->setValue(position.y());
    };
    // Once the list is published, apply its viewport before another navigation
    // can capture this visit. A rollback can still have a reload in flight.
    if (view.isVisible() && view.isEnabled())
        restore();
    else
        WhenReady(std::move(restore));
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
        if (!locationState.nativePlaceIdentity.isEmpty()) {
            locationState.path.clear();
            locationState.nativePlaceIdentity = QStringLiteral("locations");
        } else {
            locationState.path = locationState.archiveLocation.physicalPath.isEmpty()
                                     ? QFileInfo(QDir::cleanPath(locationState.path)).absolutePath()
                                     : locationState.archiveLocation.MoveToParent();
        }
        locationState.archiveApplied = locationState.archiveRevision;
    }
    const QString requested = locationState.path;
    const auto interaction = interactionRevision_;
    window_.withPanelProjection(panel, [this, panel, &requested, interaction] {
        const HRESULT result = Navigate(
            [this, panel]() -> HRESULT {
                ProtectNavigation();
                const QScopedValueRollback<bool> changing(changing_, true);
                return OpenPanelLocation(panel);
            },
            [this, panel, interaction](HRESULT result) {
                if (result == S_OK)
                    FocusListAfterLocationOpen(panel, interaction);
            });
        if (result != S_OK)
            CompleteLocationOpen(panel, requested, result);
    });
    window_.refreshActions();
}
