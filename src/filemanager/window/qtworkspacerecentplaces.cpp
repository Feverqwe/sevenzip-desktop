#include "filemanager/providers/RootFolder.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtrecentplacesstore.h"
#include "filemanager/window/qtwindowworkspace.h"
#include "ui/controls/qtmenutext.h"
#include "ui/controls/qtwindowstatusbar.h"
#include "ui/progress/ProgressDialog2.h"
#include <QDir>
#include <QFontMetrics>
#include <QMenu>
#include <QTimer>

namespace {
QString PlaceLabel(const QtRecentPlace &place) {
    int iconIndex = 0;
    if (place.nativeIdentity == "computer")
        return ToQString(RootFolder_GetName_Computer(iconIndex));
    if (place.nativeIdentity == "network")
        return ToQString(RootFolder_GetName_Network(iconIndex));
    if (place.path.isEmpty() || place.nativeIdentity == "locations")
        return QCoreApplication::translate("QtWindowWorkspace", "Locations");
    return QDir::toNativeSeparators(place.path);
}
} // namespace

void QtWindowWorkspace::RecordVisit(const QString &path) {
    const auto visit = CaptureVisit(window_.projectedPanel_);
    recentPlaces_->Visit({path, visit.nativePlaceIdentity, visit.archiveLocation, visit.diskFolder});
}

void QtWindowWorkspace::ShowRecentPlaces(unsigned panel, QWidget &anchor) {
    if (!Ready() || !anchor.isVisible())
        return;
    recentPlaces_->Reload();
    auto *menu = new QMenu(&anchor);
    menu->setObjectName("recentPlacesMenu");
    menu->setToolTipsVisible(true);
    connect(menu, &QMenu::aboutToHide, menu, &QObject::deleteLater);
    constexpr int maxPlaces = 5;
    const int maxLabelWidth = anchor.fontMetrics().averageCharWidth() * 52;
    for (int row = 0; row < qMin(maxPlaces, recentPlaces_->Count()); ++row) {
        const auto place = recentPlaces_->Place(row);
        const QString label = PlaceLabel(place);
        auto *item = menu->addAction(QtLiteralMenuText(
            menu->fontMetrics().elidedText(label, Qt::ElideMiddle, maxLabelWidth)));
        item->setToolTip(label);
        item->setData(place.path);
        connect(item, &QAction::triggered, this, [this, panel, place] {
            QTimer::singleShot(0, this, [this, panel, place] { OpenRecentPlace(panel, place); });
        });
    }
    if (menu->isEmpty())
        menu->addAction(tr("No recent places yet."))->setEnabled(false);
    menu->popup(
        anchor.mapToGlobal(QPoint(anchor.width() - menu->sizeHint().width(), anchor.height())));
}

void QtWindowWorkspace::OpenRecentPlace(unsigned panel, const QtRecentPlace &place) {
    if (!window_.app_.IsPanelVisible(panel) || !Ready())
        return;
    window_.activatePanelUi(panel);
    if (window_.projectedPanel_ != panel)
        return;
    const auto interaction = interactionRevision_;
    Navigate(
        [&]() -> HRESULT {
            if (place.diskFolder && !QFileInfo(QDir::cleanPath(place.path)).isDir())
                return S_FALSE;
            ProtectNavigation();
            return OpenRecordedLocation(panel, place.path, place.nativeIdentity, place.archive);
        },
        [this, panel, interaction](HRESULT result) {
            if (window_.app_.QtOperations().closed())
                return;
            if (result == S_OK) {
                FocusListAfterLocationOpen(panel, interaction);
                return;
            }
            const QString error = result == E_ABORT ? tr("Opening canceled.")
                                  : result == S_FALSE
                                      ? tr("The location is unavailable or disconnected.")
                                      : tr("Could not open this location. %1")
                                            .arg(ToQString(HResultToMessage(result)));
            window_.statusStrip_->SetMessage(&window_.app_.Panels[panel], error, 6000);
        });
}
