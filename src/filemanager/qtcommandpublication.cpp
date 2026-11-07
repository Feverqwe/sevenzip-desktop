#include "7zip/UI/FileManager/resource.h"
#include "filemanager/MyLoadMenu.h"
#include "filemanager/qtfilemanagercommands.h"
#include "filemanager/window/mainwindow.h"
#include <QAction>
#include <QScopeGuard>
#include <QSignalBlocker>

void QtFileManagerCommands::RefreshActions() {
    if (actionRefresh_ != ActionRefresh::Idle) {
        actionRefresh_ = ActionRefresh::Pending;
        return;
    }
    const QPointer<MainWindow> owner(&window_);
    const auto reset = qScopeGuard([this, owner] {
        if (owner)
            actionRefresh_ = ActionRefresh::Idle;
    });
    do {
        actionRefresh_ = ActionRefresh::Publishing;
        PublishActions();
        if (!owner)
            return;
    } while (actionRefresh_ == ActionRefresh::Pending);
}

void QtFileManagerCommands::PublishActions() {
    const QPointer<MainWindow> owner(&window_);
    const auto interrupted = [this, owner] {
        return !owner || actionRefresh_ == ActionRefresh::Pending;
    };
    const auto availability = Availability();
    const auto menuPanel = CapturedViewPanel();
    const auto menuAvailability = !viewMenuSource_ ? availability
                                  : menuPanel      ? AvailabilityForPanel(*menuPanel)
                                                   : QHash<unsigned, bool>{};
    if (interrupted())
        return;
    for (auto it = actions_.cbegin(); it != actions_.cend(); ++it) {
        for (QAction *action : it.value()) {
            const auto &commands =
                action->property("workspaceViewContext").toBool() ? menuAvailability : availability;
            action->setEnabled(commands.value(it.key(), false));
            if (interrupted())
                return;
        }
    }
    RefreshViewActions();
}

void QtFileManagerCommands::RefreshViewActions() {
    const auto *panel = viewMenuSource_ ? viewMenuSource_->data() : &window_.app_.GetFocusedPanel();
    for (const auto &[name, ascending] :
         {std::pair{"sortAscendingAction", true}, std::pair{"sortDescendingAction", false}})
        if (auto *action = window_.findChild<QAction *>(QString::fromLatin1(name))) {
            const QSignalBlocker block(action);
            action->setChecked(panel && panel->_ascending == ascending);
        }
    {
        const QSignalBlocker block(window_.twoPanelsAction_);
        window_.twoPanelsAction_->setChecked(window_.app_.NumPanels == 2);
    }
    {
        const QSignalBlocker block(window_.flatView_);
        window_.flatView_->setChecked(panel && panel->GetFlatMode());
    }
    for (unsigned i : {0u, 3u}) {
        const QSignalBlocker block(window_.viewActions_[i]);
        window_.viewActions_[i]->setChecked(panel && i == panel->GetListViewMode());
    }
    for (unsigned i = 0; i < 4; ++i) {
        const QSignalBlocker block(window_.sortActions_[i]);
        window_.sortActions_[i]->setChecked(
            panel && window_.sortActions_[i]->property("nativeCommandID").toUInt() ==
                         GetSortControlID(panel->GetSortID()));
    }
}
