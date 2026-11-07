#include "Windows/ErrorMsg.h"
#include "dialogs/recentplaces/qtrecentplacesdialog.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtnativeplaces.h"
#include "filemanager/window/qtrecentplacesstore.h"
#include "filemanager/window/qtwindowworkspace.h"
#include "ui/progress/ProgressDialog2.h"
#include <QScopeGuard>

void QtWindowWorkspace::RecordVisit(const QString &path) {
    const auto visit = CaptureVisit(window_.projectedPanel_);
    recentPlaces_->Visit({path, visit.nativePlaceIdentity, visit.archiveLocation});
}

void QtWindowWorkspace::ShowRecentPlaces() {
    if (recentDialog_ && recentDialog_->isVisible()) {
        recentDialog_->raise();
        recentDialog_->activateWindow();
        return;
    }
    const unsigned panel = window_.projectedPanel_;
    if (!window_.panelUi_[panel].table)
        return;
    recentPlaces_->Reload();
    auto *dialog = new QtRecentPlacesDialog(*recentPlaces_, &window_);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    recentDialog_ = dialog;
    const QPointer<QtRecentPlacesDialog> target(dialog);
    dialog->SetOpenHandler([this, panel, target](const QtRecentPlace &place) {
        OpenRecentPlace(panel, place, target);
    });
    connect(dialog, &QDialog::finished, this, [this, target](int) {
        for (unsigned panel = 0; panel < navigation_.size(); ++panel) {
            auto &request = navigation_[panel];
            if (!request || request->requester != target)
                continue;
            request->canceled = true;
            if (request->depth == 0 && request->source == window_.app_.Panels.Share(panel))
                window_.withPanelProjection(panel, [this] { window_.cancelListPublication(); });
        }
        window_.refreshActions();
    });
    dialog->show();
}

void QtWindowWorkspace::OpenRecentPlace(unsigned panel, const QtRecentPlace &place,
                                        QPointer<QtRecentPlacesDialog> target) {
    if (!target || !target->isVisible())
        return;
    if (!window_.panelUi_[panel].table || window_.app_.QtOperations().closed()) {
        target->Complete(false, tr("The original panel is closed."));
        return;
    }
    if (!Ready()) {
        target->Complete(false, tr("The original panel is busy."));
        return;
    }
    window_.withPanelProjection(panel, [&] {
        Navigate(
            [&]() -> HRESULT {
                auto &request = *navigation_[panel];
                request.requester = target;
                ProtectNavigation();
                if (!place.archive.physicalPath.isEmpty())
                    return OpenArchiveLocation(panel, place.archive);
                QString path = window_.sourcePathFromDisplayPath(place.path);
                if (!place.nativeIdentity.isEmpty()) {
                    nativePlaces_->Refresh();
                    const auto native = nativePlaces_->FindIdentity(place.nativeIdentity);
                    if (place.nativeIdentity == "locations")
                        path.clear();
                    else if (native.isValid())
                        path = native.data(QtPlaceRoles::Path).toString();
                    else
                        return E_FAIL;
                }
                const HRESULT opened = window_.navigateTo(path, true);
                return opened == S_OK && !LocationMatches(panel, path) ? S_FALSE : opened;
            },
            [target](HRESULT result) {
                if (!target)
                    return;
                const QString error = result == E_ABORT ? tr("Opening canceled.")
                                      : result == S_FALSE
                                          ? tr("The location is unavailable or disconnected.")
                                          : tr("Could not open this location. %1")
                                                .arg(ToQString(HResultToMessage(result)));
                target->Complete(result == S_OK, error);
            });
    });
}
