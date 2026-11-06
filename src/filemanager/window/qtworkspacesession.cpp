#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtwindowworkspace.h"
#include "filemanager/window/qtworkspaceruntime.h"
#include <QFileInfo>
#include <QJsonArray>
#include <QScopedValueRollback>

QJsonObject QtWindowWorkspace::Save() {
    QJsonArray panels;
    for (unsigned index = 0; index < 2; ++index) {
        Capture(index, false);
        QJsonArray tabs;
        for (const auto &tab : panels_[index].tabs) {
            const QString path =
                tab.unavailable ? (tab.archiveLocation.physicalPath.isEmpty()
                                       ? tab.path
                                       : QFileInfo(tab.archiveLocation.physicalPath).absolutePath())
                : tab.runtime   ? ToQString(tab.runtime->source->GetResumeFolderPath())
                : tab.resumePath ? *tab.resumePath
                : !tab.archiveLocation.physicalPath.isEmpty()
                    ? QFileInfo(tab.archiveLocation.physicalPath).absolutePath()
                    : tab.path;
            tabs.append(QJsonObject{{"id", tab.id},
                                    {"path", path},
                                    {"nativePlace", tab.archiveLocation.physicalPath.isEmpty()
                                                        ? tab.nativePlaceIdentity
                                                        : QString()},
                                    {"view", tab.viewMode},
                                    {"sort", int(tab.sort)},
                                    {"ascending", tab.ascending}});
        }
        panels.append(QJsonObject{{"tabs", tabs}, {"active", panels_[index].current}});
    }
    QJsonArray split;
    for (const int size : window_.panelSplitter_->sizes())
        split.append(size);
    return {{"panels", panels},
            {"count", int(window_.app_.NumPanels)},
            {"focused", int(window_.app_.GetFocusedPanelIndex())},
            {"sidebar", sidebarVisible_},
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
                                     std::function<void()> finished,
                                     std::function<void()> canceled) {
    const auto tabs = saved.value("tabs").toArray();
    if (!panels_[panel].bar || tabs.isEmpty()) {
        finished();
        return;
    }
    auto &state = panels_[panel];
    {
        const auto source = window_.app_.Panels.Share(panel);
        const CPanel::CDisableNotify notifications(*source);
        const QScopedValueRollback<bool> changing(changing_, true);
        // Releasing retained lists emits selection signals. Do not publish
        // workspace state while its previous tabs are being replaced.
        state.tabs.clear();
        for (const auto &value : tabs) {
            const auto object = value.toObject();
            Tab tab;
            tab.id = object.value("id").toString(QUuid::createUuid().toString());
            tab.path = object.value("path").toString();
            tab.nativePlaceIdentity = object.value("nativePlace").toString();
            // Earlier workspace settings persisted open archive chains. Resume
            // the containing directory, just like CApp's single-window Save.
            const auto archive = object.value("archiveLocation").toObject();
            const QString physical = archive.value("physical").toString();
            if (!physical.isEmpty()) {
                tab.path = QFileInfo(physical).absolutePath();
                tab.nativePlaceIdentity.clear();
            } else if (QFileInfo(tab.path).isFile()) {
                tab.path = QFileInfo(tab.path).absolutePath();
                tab.nativePlaceIdentity.clear();
            }
            tab.resumePath = tab.path;
            tab.viewMode = qBound(0, object.value("view").toInt(3), 3);
            tab.sort = object.value("sort").toInt();
            tab.ascending = object.value("ascending").toBool(true);
            state.tabs.push_back(std::move(tab));
        }
        state.current = qBound(0, saved.value("active").toInt(), int(state.tabs.size()) - 1);
        Rebuild(panel);
    }
    Activate(panel, state.current);
    WhenReady(std::move(finished), std::move(canceled));
}

void QtWindowWorkspace::Restore(const QJsonObject &value, std::function<void()> finished,
                                std::function<void()> canceled) {
    const auto panels = value.value("panels").toArray();
    const bool hasSecond =
        panels.size() > 1 && !panels[1].toObject().value("tabs").toArray().isEmpty();
    if ((hasSecond || value.value("count").toInt(1) == 2) && window_.app_.NumPanels == 1)
        window_.togglePanels();
    WhenReady(
        [this, value, panels, finished = std::move(finished), canceled] {
            const auto complete = [this, value, finished] {
                const unsigned focus = qBound(0, value.value("focused").toInt(), 1);
                if (window_.panelUi_[focus].container)
                    window_.activatePanelUi(focus);
                if (value.value("count").toInt(1) == 1 && window_.app_.NumPanels == 2)
                    window_.togglePanels();
                sidebarVisible_ = value.value("sidebar").toBool(true);
                if (auto *action = window_.findChild<QAction *>("sidebarAction"))
                    action->setChecked(sidebarVisible_);
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
                canceled);
        },
        canceled);
}
