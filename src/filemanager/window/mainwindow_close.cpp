#include "filemanager/qtfilemanagercommands.h"
#include "filemanager/qtfilemanagerdrag.h"
#include "filemanager/qtfilemanagerwindowservices.h"
#include "filemanager/qtprogramfilemenu.h"
#include "filemanager/rename/qtrenamecontroller.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtapppresentation.h"
#include "filemanager/window/qtfileclipboard.h"
#include "filemanager/window/qtlocalfiletask.h"
#include "filemanager/window/qtwindowworkspace.h"
#include "filemanager/window/qtworkspaceapplication.h"
#include "platform/associations/qtassociatedfileservices.h"
#include "platform/trash/qttrashservice.h"
#include <QCloseEvent>
#include <QEventLoopLocker>

class QtFileManagerWindowServices final : public IQtFileManagerWindowServices {
    MainWindow &window_;

  public:
    explicit QtFileManagerWindowServices(MainWindow &window) : window_(window) {}
    bool CanCloseNow() const override {
        if (window_.sourceArchiveProgress_)
            return false;
        for (const auto *owner = QApplication::activeModalWidget(); owner;
             owner = owner->parentWidget())
            if (owner == &window_)
                return false;
        return window_.commands_->CanCloseMounts();
    }
    void Deactivate() override {
        if (auto *application = QtWorkspaceApplication::Find())
            application->Closing(window_);
        if (window_.autoRefreshTimer_)
            window_.autoRefreshTimer_->stop();
        for (auto &panel : window_.app_.Panels)
            panel.Disable_Processing_Timer_Notify_StatusBar();
        window_.setEnabled(false);
    }
    void ExitExternalWorkers(bool hardExit) override {
        window_.workspace_->ExitExternalWorkers(hardExit);
    }
    void RevokeDropTarget() override {
        if (window_.drag_)
            window_.drag_->Revoke();
        window_.setAcceptDrops(false);
        for (const auto &ui : window_.panelUi_) {
            if (ui.table)
                ui.table->setAcceptDrops(false);
            if (ui.iconList)
                ui.iconList->setAcceptDrops(false);
        }
    }
    void QuiesceOperations() override { window_.quiesceOperations(); }
    void SaveWindowInfo() override {
        CWindowInfo info{};
        bool positionDefined = false, panelsDefined = false;
        info.Read(positionDefined, panelsDefined, MainWindow::toolbarSettingsPath());
        const QRect geometry =
            window_.isMaximized() ? window_.normalGeometry() : window_.geometry();
        info.rect = {geometry.x(), geometry.y(), geometry.x() + geometry.width(),
                     geometry.y() + geometry.height()};
        info.maximized = window_.isMaximized();
        info.numPanels = window_.app_.NumPanels;
        info.currentPanel = window_.app_.LastFocusedPanel;
        info.splitterPos = static_cast<UInt32>(window_.panelSplitter_->SourcePosition());
        info.Save(MainWindow::toolbarSettingsPath());
    }
};

MainWindow::~MainWindow() {
    if (drag_)
        drag_->Revoke();
    removeEventFilter(this);
    // QComboBox can emit activated from editingFinished while QWidget tears
    // down focus. Navigation must stop before MainWindow members are destroyed.
    QCloseEvent close;
    closeEvent(&close);
    // Destruction cannot be vetoed by a modal child or an active source call.
    QtFileManagerWindowServices services(*this);
    CloseFileManager(app_, services, QtFileManagerCloseReason::Destruction);
    // QWidget destroys its children after CApp's member lifetime ends. Stop
    // callback text before that teardown and clear published folder identities.
    for (auto &ui : panelUi_)
        if (auto *table = dynamic_cast<FileTable *>(ui.table)) {
            table->sourcePanel = nullptr;
            table->textFolder = nullptr;
        }
    workspace_->Dispose();
}

void MainWindow::closeEvent(QCloseEvent *event) {
    QtFileManagerWindowServices services(*this);
    if (CloseFileManager(app_, services)) {
        event->accept();
        if (!isVisible()) {
            // Source WM_DESTROY posts quit even after SW_HIDE. Qt only emits
            // lastWindowClosed for a previously visible window. Request its
            // automatic quit after this close, retaining other visible windows.
            QMetaObject::invokeMethod(
                qApp,
                [] {
                    if (qApp->quitOnLastWindowClosed()) {
                        QEventLoopLocker finished;
                    }
                },
                Qt::QueuedConnection);
        }
    } else
        event->ignore();
}

QWidget *MainWindow::applicationQuitBlocker() const {
    const auto &operations = app_.QtOperations();
    if (operations.callActive() || operations.kind() == QtAppOperationState::Kind::Open ||
        (operations.blocking() && !fileTask_) || sourceArchiveProgress_)
        return const_cast<MainWindow *>(this);
    return commands_->MountCloseBlocker();
}

void MainWindow::quiesceOperations() {
    if (fileTask_) {
        fileTask_->StopAndWait();
        fileTask_ = nullptr;
    }
    if (guiOutputRefresh_)
        guiOutputRefresh_->Stop();
    if (renameController_)
        renameController_->Stop();
    disconnect(backend_, nullptr, this, nullptr);
    if (overwriteDialog_)
        overwriteDialog_->reject();
    for (const auto &ui : panelUi_) {
        if (ui.listProjection)
            ui.listProjection->cancel();
        if (ui.filesystemScanner) {
            disconnect(ui.filesystemScanner, nullptr, this, nullptr);
            ui.filesystemScanner->cancelAndWait();
        }
    }
    workspace_->QuiesceNavigation();
    backend_->cancelAndWait();
    closeProgressDialog();
}
