#include "archive/resources/qtarchivefileresource.h"
#include "filemanager/qtfilemanagercommands.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtapppresentation.h"
#include "filemanager/window/qtfileclipboard.h"
#include "filemanager/window/qtwindowworkspace.h"
#include "localization/qtlangcontrols.h"
#include "platform/locale/qtregionalsettings.h"
#include "ui/controls/qtwindowstatusbar.h"
#include "ui/formatting/qtregionalformat.h"
#include <QScopedValueRollback>

void MainWindow::setState(State state) {
    const unsigned scanningPanels = scanningPanelMask();
    const auto kind = archiveLocationPending_ || state == State::Loading
                          ? QtAppOperationState::Kind::Open
                      : state == State::Operating ? QtAppOperationState::Kind::Operation
                      : scanningPanels            ? QtAppOperationState::Kind::Scan
                                                  : QtAppOperationState::Kind::Idle;
    if (!(panelProjectionDepth_ && app_.QtOperations().blocking()) &&
        !app_.QtOperations().Begin(kind, scanningPanels))
        return;
    projectedUi().state = state;
    refreshActions();
}

void MainWindow::refreshActions() {
    // Disabling a view can synchronously move native Qt focus. Keep the
    // current source transition bound until its caller finishes scheduling.
    {
        QScopedValueRollback<unsigned> projection(panelProjectionDepth_, panelProjectionDepth_ + 1);
        app_.RefreshTitle();
        const bool navigating = !app_.QtOperations().closed() && !app_.QtOperations().blocking();
        const bool operating = app_.QtOperations().kind() == QtAppOperationState::Kind::Operation;
        const bool idle =
            navigating && !projectedUi().scanningDisk && !projectedUi().scanningArchive;
        refreshBookmarks();
        if (commands_)
            commands_->RefreshActions();
        if (commands_ && !projectedUi().scanningDisk && !projectedUi().scanningArchive &&
            app_.GetFocusedPanel().ShouldProcessNotifications())
            refreshPanelStatus();
        else if (projectedUi().scanningDisk || projectedUi().scanningArchive ||
                 projectedUi().state == State::Loading)
            showPanelStatus(projectedPanel_, {});
        for (unsigned index = 0; index < kNumPanelsMax; ++index) {
            const auto &ui = panelUi_[index];
            if (!ui.container || !ui.table || !ui.iconList || !ui.path)
                continue;
            const bool projecting = ui.scanningDisk || ui.scanningArchive;
            const bool panelIdle = navigating && !projecting && !ui.locationUnavailable;
            QWidget *focused = QApplication::focusWidget();
            if ((projecting || operating) && focused &&
                (ui.table == focused || ui.table->isAncestorOf(focused) || ui.iconList == focused ||
                 ui.iconList->isAncestorOf(focused)))
                workspace_->RetainListFocus(index);
            // RefreshListCtrl disables redraw until native sorting and selection
            // restoration finish. Keep that policy across Qt's asynchronous batches.
            ui.table->setUpdatesEnabled(!projecting);
            ui.iconList->setUpdatesEnabled(!projecting);
            ui.table->setEnabled(panelIdle);
            ui.iconList->setEnabled(panelIdle);
            // Native archive progress is modal to the file manager. Qt disables
            // its lists during the operation; disable the address controls too so
            // focus cannot move to an inactive pane while its source guards block
            // the corresponding focus notification.
            ui.pathCombo->setEnabled(navigating);
            ui.path->setReadOnly(!navigating);
        }
        const bool dragAvailable =
            idle && app_.GetFocusedPanel().DoesItSupportOperations() &&
            (projectedUi().state == State::Filesystem || projectedUi().state == State::Viewing);
        projectedUi().table->setDragEnabled(dragAvailable);
        projectedUi().iconList->setDragEnabled(dragAvailable);
    }
    if (appPresentation_ && !panelProjectionDepth_) {
        appPresentation_->CompleteInitialFocusIfReady();
        appPresentation_->CompleteCopyIfReady();
    }
    if (workspace_)
        workspace_->Refresh();
    if (clipboard_)
        clipboard_->Refresh();
}

void MainWindow::refreshPanelStatus() {
    auto &panel = app_.GetFocusedPanel();
    panel.Refresh_StatusBar();
    const auto direct = [](IFolderFolder *folder) {
        const auto *agent = dynamic_cast<CAgentFolder *>(folder);
        const auto *resource =
            agent ? dynamic_cast<QtArchiveFileResource *>(agent->GetEngineResource()) : nullptr;
        return resource;
    };
    bool device = false, limitedRead = false;
    const auto inspect = [&](IFolderFolder *folder) {
        if (const auto *resource = direct(folder)) {
            const bool filesystemLimitations = !resource->FilesystemReadLimitations().isEmpty();
            device |= resource->HasDirectInput() || filesystemLimitations;
            limitedRead |= resource->AllowsIncompleteDeviceRead() || filesystemLimitations;
        }
    };
    inspect(panel.GetFolder());
    const auto &parents = panel.GetParentFolders();
    FOR_VECTOR(i, parents) { inspect(parents[i].ParentFolder); }
    auto *indicator = projectedUi().readOnlyIndicator;
    QtBindLocalText(indicator, "mainwindow_panel",
                    limitedRead
                        ? QT_TRANSLATE_NOOP("mainwindow_panel", "Read only · limited access")
                        : QT_TRANSLATE_NOOP("mainwindow_panel", "Read only"));
    indicator->setVisible(device);
}

void MainWindow::showPanelStatus(unsigned index, const CPanelStatus &status) {
    if (index != statusPanel_ || index >= kNumPanelsMax || app_.QtOperations().closed() ||
        !statusStrip_)
        return;
    const auto &ui = panelUi_[index];
    const bool loading = ui.scanningDisk || ui.scanningArchive || ui.state == State::Loading;
    QString summary;
    if (loading)
        summary = QCoreApplication::translate("WindowStatus", "Reading folder…");
    else if (!ui.locationUnavailable) {
        summary = status.selectedCount
                      ? QCoreApplication::translate("WindowStatus", "Selected: %1 / %2")
                            .arg(QtSystemRegionalNumber(quint64(status.selectedCount)),
                                 QtSystemRegionalNumber(quint64(status.totalCount)))
                      : QCoreApplication::translate("WindowStatus", "Objects: %1")
                            .arg(QtSystemRegionalNumber(quint64(status.totalCount)));
    }
    if (app_.NumPanels == 2 && !summary.isEmpty())
        summary = (index == 0 ? QCoreApplication::translate("WindowStatus", "Left")
                              : QCoreApplication::translate("WindowStatus", "Right")) +
                  " · " + summary;
    const bool hasSize = !loading && !ui.locationUnavailable && status.selectedSize.has_value();
    statusStrip_->SetSummary(
        &app_.Panels[index], summary,
        hasSize ? QtRegionalFormat::bytes(*status.selectedSize) : QString(),
        hasSize ? QtRegionalFormat::exactBytes(*status.selectedSize) : QString(), loading);
}

void MainWindow::showStatusMessage(const QString &message, int timeout) {
    if (statusStrip_)
        statusStrip_->SetMessage(&app_.GetFocusedPanel(), message, timeout);
}

void MainWindow::clearStatusMessage() { showStatusMessage({}); }
