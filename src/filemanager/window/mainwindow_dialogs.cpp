#include "7zip/UI/FileManager/resource.h"
#include "Common/StringConvert.h"
#include "filemanager/window/mainwindow.h"
#include "localization/qtlangcontrols.h"
#include "ui/progress/ProgressDialog2.h"
#include "ui/progress/archiveprogresstransport.h"
#include "ui/progress/qtcallbackprogresstransport.h"
#include <utility>

void MainWindow::showSelectedProperties() {
    if (!properties_->isEnabled())
        return;
    CPanel &panel = app_.GetFocusedPanel();
    panel.LoadFullPath();
    panel.Properties();
}

void MainWindow::begin(const QString &label, const QString &detail) {
    startProgressDialog(label, detail);
    auto transport = std::make_unique<ArchiveProgressTransport>(*progress_, *backend_);
    connect(backend_, &ArchiveBackend::progress, transport.get(),
            [this, label](int, const QString &path) {
                if (!app_.QtOperations().closed() && !path.isEmpty())
                    statusBar()->showMessage(label + ": " + path);
            });
    progressTransport_ = std::move(transport);
    progress_->OnInit();
    progress_->show();
}

void MainWindow::beginWorkerProgress(const QString &label, const QString &detail,
                                     std::function<void()> cancel) {
    startProgressDialog(label, detail);
    progress_->CompressingMode = false;
    progress_->ShowCompressionInfo = false;
    progressTransport_ = std::make_unique<QtCallbackProgressTransport>(std::move(cancel));
    progress_->AttachTransport(progressTransport_.get());
    progress_->OnInit();
    progress_->show();
}

void MainWindow::startProgressDialog(const QString &label, const QString &detail) {
    closeProgressDialog();
    auto *dialog = new CProgressDialog(this, ToUString(label));
    dialog->MainWindow = this;
    dialog->MainTitle = "7-Zip";
    dialog->setWindowModality(Qt::ApplicationModal);
    dialog->Sync.Set_Status(ToUString(label));
    dialog->Sync.Set_FilePath(detail.toStdWString().c_str());
    progress_ = dialog;
}
