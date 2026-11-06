#include "7zip/UI/FileManager/resource.h"
#include "Common/StringConvert.h"
#include "filemanager/window/mainwindow.h"
#include "localization/qtlangcontrols.h"
#include "ui/progress/ProgressDialog2.h"
#include "ui/progress/archiveprogresstransport.h"
#include <utility>

void MainWindow::showSelectedProperties() {
    if (!properties_->isEnabled())
        return;
    CPanel &panel = app_.GetFocusedPanel();
    panel.LoadFullPath();
    panel.Properties();
}

void MainWindow::begin(const QString &label, const QString &detail) {
    auto progress = createProgressDialog(label, detail);
    auto transport = std::make_unique<ArchiveProgressTransport>(*progress->dialog, *backend_);
    connect(backend_, &ArchiveBackend::progress, transport.get(),
            [this, label](int, const QString &path) {
                if (!app_.QtOperations().closed() && !path.isEmpty())
                    statusBar()->showMessage(label + ": " + path);
            });
    progress->transport = std::move(transport);
    progress_ = std::move(progress);
    progressDialog()->OnInit();
    progressDialog()->show();
}

std::unique_ptr<MainWindow::WindowProgress>
MainWindow::createProgressDialog(const QString &label, const QString &detail) {
    closeProgressDialog();
    auto progress = std::make_unique<WindowProgress>();
    auto *dialog = new CProgressDialog(this, ToUString(label));
    dialog->MainWindow = this;
    dialog->MainTitle = "7-Zip";
    dialog->setWindowModality(Qt::ApplicationModal);
    dialog->Sync.Set_Status(ToUString(label));
    dialog->Sync.Set_FilePath(detail.toStdWString().c_str());
    progress->dialog = dialog;
    return progress;
}

CProgressDialog *MainWindow::progressDialog() const {
    return progress_ ? progress_->dialog.data() : nullptr;
}
