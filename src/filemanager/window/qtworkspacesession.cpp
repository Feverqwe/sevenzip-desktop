#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtwindowworkspace.h"
#include <QFileInfo>
#include <QJsonArray>
#include <QScopedValueRollback>

namespace {
QJsonObject SavedLocation(const QJsonObject &panel) {
    if (!panel.contains("tabs"))
        return panel;
    const auto tabs = panel.value("tabs").toArray();
    if (tabs.isEmpty())
        return {};
    const int active = qBound(0, panel.value("active").toInt(), int(tabs.size()) - 1);
    return tabs.at(active).toObject();
}
} // namespace

QJsonObject QtWindowWorkspace::Save() {
    QJsonArray panels;
    for (unsigned index = 0; index < 2; ++index) {
        Capture(index, false);
        if (!window_.panelUi_[index].table) {
            panels.append(QJsonObject{});
            continue;
        }
        const auto &locationState = panels_[index].location;
        const auto &ui = window_.panelUi_[index];
        QString path = ToQString(window_.app_.Panels[index].GetResumeFolderPath());
        if (ui.locationUnavailable)
            path = locationState.archiveLocation.physicalPath.isEmpty()
                       ? locationState.path
                       : QFileInfo(locationState.archiveLocation.physicalPath).absolutePath();
        panels.append(QJsonObject{
            {"path", path},
            {"nativePlace", !ui.scanningDisk && locationState.archiveLocation.physicalPath.isEmpty()
                                ? locationState.nativePlaceIdentity
                                : QString()},
            {"view", locationState.viewMode},
            {"sort", int(locationState.sort)},
            {"ascending", locationState.ascending}});
    }
    QJsonArray split;
    for (const int size : window_.panelSplitter_->sizes())
        split.append(size);
    return {{"panels", panels},
            {"count", int(window_.app_.NumPanels)},
            {"focused", int(window_.app_.GetFocusedPanelIndex())},
            {"split", split},
            {"activeWindow", window_.isActiveWindow()},
            {"geometry", QString::fromLatin1(window_.saveGeometry().toBase64())}};
}

void QtWindowWorkspace::WhenReady(std::function<void()> body, std::function<void()> canceled) {
    auto *timer = new QTimer(this);
    const auto pending = std::make_shared<bool>(true);
    const auto cancel = [pending, canceled = std::move(canceled)] {
        if (std::exchange(*pending, false) && canceled)
            canceled();
    };
    connect(timer, &QObject::destroyed, cancel);
    timer->setInterval(25);
    connect(timer, &QTimer::timeout, this, [this, timer, pending, body = std::move(body), cancel] {
        if (window_.app_.QtOperations().closed()) {
            timer->stop();
            timer->deleteLater();
            cancel();
            return;
        }
        if (!Ready())
            return;
        timer->stop();
        timer->deleteLater();
        *pending = false;
        body();
    });
    timer->start();
}

void QtWindowWorkspace::RestorePanel(unsigned panel, const QJsonObject &saved,
                                     std::function<void()> finished, std::function<void()> canceled,
                                     const QtArchiveLocation &liveArchive) {
    const auto object = SavedLocation(saved);
    if (!window_.panelUi_[panel].table || object.isEmpty()) {
        finished();
        return;
    }
    auto &locationState = panels_[panel].location;
    locationState = {};
    locationState.path = object.value("path").toString();
    locationState.nativePlaceIdentity = object.value("nativePlace").toString();
    // Earlier workspace settings persisted open archive chains. Resume
    // the containing directory, just like CApp's single-window Save.
    const auto archive = object.value("archiveLocation").toObject();
    const QString physical = archive.value("physical").toString();
    if (!liveArchive.physicalPath.isEmpty()) {
        locationState.archiveLocation = liveArchive;
    } else if (!physical.isEmpty()) {
        locationState.path = QFileInfo(physical).absolutePath();
        locationState.nativePlaceIdentity.clear();
    } else if (QFileInfo(locationState.path).isFile()) {
        locationState.path = QFileInfo(locationState.path).absolutePath();
        locationState.nativePlaceIdentity.clear();
    }
    locationState.viewMode = object.value("view").toInt(3) == 0 ? 0 : 3;
    locationState.sort = object.value("sort").toInt();
    locationState.ascending = object.value("ascending").toBool(true);
    window_.withPanelProjection(panel, [this, panel, &locationState] {
        const Visit target{locationState.path,
                           locationState.nativePlaceIdentity,
                           locationState.archiveLocation,
                           {},
                           {}};
        Navigate(
            [&] {
                const QScopedValueRollback<bool> changing(changing_, true);
                auto &source = window_.app_.Panels[panel];
                source.SetListViewMode(locationState.viewMode);
                if (locationState.sort) {
                    source._sortID = locationState.sort;
                    source._ascending = locationState.ascending;
                }
                return OpenPanelLocation(panel);
            },
            [this, panel, target](HRESULT result) {
                if (result == S_OK || window_.app_.QtOperations().closed())
                    return;
                auto &location = panels_[panel].location;
                location.nativePlaceIdentity = target.nativePlaceIdentity;
                location.archiveLocation = target.archiveLocation;
                CompleteLocationOpen(panel, target.path, result);
                window_.refreshActions();
            },
            NavigationHistory::Skip);
    });
    window_.refreshActions();
    WhenReady(std::move(finished), std::move(canceled));
}

void QtWindowWorkspace::Restore(const QJsonObject &value, std::function<void()> finished,
                                std::function<void()> canceled,
                                const QtArchiveLocation &firstArchive) {
    const auto panels = value.value("panels").toArray();
    const bool savedTwo = value.value("count").toInt(1) == 2;
    bool needsOtherPane = false;
    for (unsigned panel = 0; panel < 2 && panel < panels.size(); ++panel)
        needsOtherPane |=
            !window_.panelUi_[panel].table && !SavedLocation(panels.at(panel).toObject()).isEmpty();
    if ((needsOtherPane || savedTwo) && window_.app_.NumPanels == 1)
        window_.togglePanels();
    WhenReady(
        [this, value, panels, finished = std::move(finished), canceled, firstArchive] {
            const auto complete = [this, value, finished] {
                const unsigned focus = qBound(0, value.value("focused").toInt(), 1);
                if (window_.panelUi_[focus].container)
                    window_.activatePanelUi(focus);
                if (value.value("count").toInt(1) == 1 && window_.app_.NumPanels == 2)
                    window_.togglePanels();
                const QByteArray geometry =
                    QByteArray::fromBase64(value.value("geometry").toString().toLatin1());
                if (!geometry.isEmpty())
                    window_.restoreGeometry(geometry);
                QList<int> sizes;
                for (const auto &size : value.value("split").toArray())
                    sizes.append(size.toInt());
                if (sizes.size() == 2) {
                    window_.panelSplitter_->setSizes(sizes);
                    window_.panelSplitter_->RememberPosition();
                }
                window_.refreshActions();
                if (value.value("activeWindow").toBool()) {
                    window_.raise();
                    window_.activateWindow();
                }
                if (finished)
                    finished();
            };
            RestorePanel(
                0, panels.size() > 0 ? panels.at(0).toObject() : QJsonObject{},
                [this, panels, complete, canceled] {
                    RestorePanel(1, panels.size() > 1 ? panels.at(1).toObject() : QJsonObject{},
                                 complete, canceled);
                },
                canceled, firstArchive);
        },
        canceled);
}
