#include "filemanager/qtfilemanagercommands.h"
#include "filemanager/qtfilemanagerdrag.h"
#include "filemanager/qtfilemanagerwindowservices.h"
#include "filemanager/rename/qtrenamecontroller.h"
#include "filemanager/window/mainwindow.h"
#include <QCloseEvent>
#include <QEventLoopLocker>

class QtFileManagerWindowServices final : public IQtFileManagerWindowServices {
    MainWindow &window_;

  public:
    explicit QtFileManagerWindowServices(MainWindow &window) : window_(window) {}
    bool CanCloseNow() const override {
        const QWidget *modal = QApplication::activeModalWidget();
        return !window_.sourceArchiveProgress_ &&
               !(modal && (modal == &window_ || window_.isAncestorOf(modal))) &&
               window_.commands_->CanCloseMounts();
    }
    void Deactivate() override {
        window_.app_.QtOperations().Close();
        if (window_.autoRefreshTimer_)
            window_.autoRefreshTimer_->stop();
        for (auto &panel : window_.app_.Panels)
            panel.Disable_Processing_Timer_Notify_StatusBar();
        window_.setEnabled(false);
    }
    void ExitExternalWorkers(bool hardExit) override {
        for (auto &panel : window_.app_.Panels)
            panel.ExitExternalWorkers(hardExit);
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
    bool WindowWasCreated() const override {
        return window_.app_.Panels[0].PanelCreated || window_.app_.Panels[1].PanelCreated;
    }
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

void MainWindow::closeEvent(QCloseEvent *event) {
    QtFileManagerWindowServices services(*this);
    if (WndProc(QtFileManagerMessage::Close, app_, services)) {
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

void MainWindow::quiesceOperations() {
    if (renameController_)
        renameController_->Stop();
    for (const auto &ui : panelUi_) {
        if (ui.pathCombo)
            disconnect(ui.pathCombo, nullptr, this, nullptr);
        if (ui.path)
            disconnect(ui.path, nullptr, this, nullptr);
    }
    disconnect(backend_, nullptr, this, nullptr);
    if (overwriteDialog_)
        overwriteDialog_->reject();
    for (const auto &ui : panelUi_) {
        disconnect(ui.filesystemScanner, nullptr, this, nullptr);
        ui.listProjection->cancel();
        ui.filesystemScanner->cancelAndWait();
    }
    backend_->cancelAndWait();
    if (fileTransfer_) {
        disconnect(fileTransfer_, nullptr, this, nullptr);
        fileTransfer_->cancel();
        fileTransfer_->wait();
    }
    if (volumeOperation_) {
        disconnect(volumeOperation_, nullptr, this, nullptr);
        volumeOperation_->cancel();
        volumeOperation_->wait();
    }
    closeProgressDialog();
}
