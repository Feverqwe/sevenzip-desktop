#include "7zip/UI/FileManager/resource.h"
#include "filemanager/MyLoadMenu.h"
#include "filemanager/qtfilemanagercommands.h"
#include "filemanager/qtfilemenuprojection.h"
#include "filemanager/window/mainwindow.h"
#include "settings/RegistryUtils.h"
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
    if (interrupted())
        return;
    for (auto it = actions_.cbegin(); it != actions_.cend(); ++it) {
        const bool enabled = availability.value(it.key(), false);
        for (QAction *action : it.value()) {
            action->setEnabled(enabled);
            if (interrupted())
                return;
        }
    }
    UString diffCommand;
    ReadRegDiff(diffCommand, MainWindow::toolbarSettingsPath());
    window_.diffFiles_->setVisible(!diffCommand.IsEmpty());
    if (interrupted())
        return;
    QtFileMenuProjection versionMenu;
    versionMenu.diffPath = diffCommand;
    ReadReg_VerCtrlPath(versionMenu.versionControlPath, MainWindow::toolbarSettingsPath());
    if (!window_.app_.QtOperations().closed() && !window_.sourceArchiveProgress_ &&
        window_.app_.GetFocusedPanel().ShouldProcessNotifications() &&
        window_.app_.GetFocusedPanel().GetFolder())
        window_.app_.GetFocusedPanel().CreateFileMenu(versionMenu, true);
    if (interrupted())
        return;
    for (unsigned id = IDM_VER_EDIT; id <= IDM_VER_DIFF; ++id) {
        for (QAction *action : actions_.value(id)) {
            action->setVisible(versionMenu.IsEnabled(id));
            if (interrupted())
                return;
            for (const auto &item : versionMenu.items)
                if (item.source.wID == id) {
                    action->setText(ToQString(item.source.StringValue));
                    if (interrupted())
                        return;
                }
        }
    }
    RefreshViewActions();
}

void QtFileManagerCommands::RefreshViewActions() {
    const auto &panel = window_.app_.GetFocusedPanel();
    {
        const QSignalBlocker block(window_.twoPanelsAction_);
        window_.twoPanelsAction_->setChecked(window_.app_.NumPanels == 2);
    }
    {
        const QSignalBlocker block(window_.flatView_);
        window_.flatView_->setChecked(panel.GetFlatMode());
    }
    for (unsigned i = 0; i < 4; ++i) {
        const QSignalBlocker block(window_.viewActions_[i]);
        window_.viewActions_[i]->setChecked(i == panel.GetListViewMode());
    }
    for (unsigned i = 0; i < 5; ++i) {
        const QSignalBlocker block(window_.sortActions_[i]);
        window_.sortActions_[i]->setChecked(
            window_.sortActions_[i]->property("nativeCommandID").toUInt() ==
            GetSortControlID(panel.GetSortID()));
    }
}
