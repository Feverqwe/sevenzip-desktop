#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtnativeplaces.h"
#include "filemanager/window/qtnavigationbar.h"
#include "filemanager/window/qtwindowworkspace.h"
#include <QScopedValueRollback>
#include <QScrollBar>

HRESULT QtWindowWorkspace::OpenTabLocation(unsigned panel) {
    auto &tab = panels_[panel].tabs[panels_[panel].current];
    const QString identity = tab.nativePlaceIdentity;
    nativePlaces_->Refresh();
    const auto place = nativePlaces_->FindIdentity(identity);
    const bool nativeRoot = identity == QStringLiteral("locations") || place.isValid();
    const QString path = place.isValid() ? place.data(QtPlaceRoles::Path).toString()
                         : nativeRoot    ? QString()
                                         : tab.path;
    HRESULT result = tab.archiveLocation.physicalPath.isEmpty()
                         ? window_.navigateTo(path, true)
                         : OpenArchiveLocation(panel, tab.archiveLocation);
    if (nativeRoot && result == S_OK) {
        auto &source = window_.app_.Panels[panel];
        if (QtNativePlaces::RootIdentity(source) != identity)
            result = E_FAIL;
        else {
            source.LoadFullPath();
            tab.path = ToQString(source.GetCurrentFolderPrefix());
        }
    }
    CompleteLocationOpen(panel, tab.path, result);
    return tab.unavailable && result == S_OK ? S_FALSE : result;
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
    auto &tab = panels_[panel].tabs[panels_[panel].current];
    tab.unavailable = result != S_OK || !LocationMatches(panel, path);
    ui.locationUnavailable = tab.unavailable;
    if (!tab.unavailable)
        return;
    tab.path = path;
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
    auto &tab = panels_[panel].tabs[panels_[panel].current];
    if (parent && tab.archiveRevision != tab.archiveApplied) {
        window_.goParent();
        return;
    }
    if (!parent && tab.archiveRevision != tab.archiveApplied) {
        RefreshPublishedArchive(panel, true);
        return;
    }
    if (parent) {
        tab.path = QFileInfo(QDir::cleanPath(tab.path)).absolutePath();
        tab.nativePlaceIdentity.clear();
        tab.archiveLocation = {};
        tab.archiveApplied = tab.archiveRevision;
    }
    const QString requested = tab.path;
    window_.withPanelProjection(panel, [this, panel, &requested] {
        const HRESULT result = Navigate([this, panel]() -> HRESULT {
            ProtectNavigation();
            const QScopedValueRollback<bool> changing(changing_, true);
            return OpenTabLocation(panel);
        });
        if (result != S_OK)
            CompleteLocationOpen(panel, requested, result);
    });
    window_.refreshActions();
}
