#include "7zip/UI/FileManager/SplitDialogRes.h"
#include "7zip/UI/FileManager/resource.h"
#include "dialogs/split/SplitDialog.h"
#include "filemanager/FormatUtils.h"
#include "filemanager/SplitUtils.h"
#include "filemanager/panels/PanelSplitFile.h"
#include "filemanager/window/mainwindow.h"
#include "fileops/volumes/qtvolumeservices.h"
#include "localization/qtlangcontrols.h"
#include "ui/progress/ProgressDialog2.h"

class QtVolumeWindowServices final : public IQtVolumeServices {
    MainWindow &window_;

  public:
    explicit QtVolumeWindowServices(MainWindow &window) : window_(window) {}
    void StartSplit(const CThreadSplit &plan, const UString &title,
                    std::function<void()> completion) override {
        const QString source = ToQString(fs2us(plan.FilePath));
        window_.launchVolume(new VolumeOperation(plan, &window_), title, source,
                             UInt64(QFileInfo(source).size()), std::move(completion));
    }
    void StartCombine(const CThreadCombine &plan, const UString &title,
                      std::function<void()> completion) override {
        window_.launchVolume(new VolumeOperation(plan, &window_), title,
                             ToQString(fs2us(plan.InputDirPrefix + plan.Names.Front())),
                             plan.TotalSize, std::move(completion));
    }
};

void MainWindow::startVolume(VolumeOperation::Mode mode) {
    if (app_.QtOperations().closed())
        return;
    QtAppOperationState::CallScope call(app_.QtOperations());
    if (projectedUi().state != State::Filesystem || volumeOperation_ ||
        !(mode == VolumeOperation::Mode::Split ? split_ : combine_)->isEnabled())
        return;
    QtVolumeWindowServices services(*this);
    if (mode == VolumeOperation::Mode::Split)
        app_.Split(services);
    else
        app_.Combine(services);
}

void MainWindow::launchVolume(VolumeOperation *operation, const UString &title,
                              const QString &source, UInt64 totalSize,
                              std::function<void()> completion) {
    volumeOperation_ = operation;
    beginWorkerProgress(ToQString(title), source,
                        [operation = volumeOperation_] { operation->cancel(); });
    progress_->MainAddTitle = title;
    progress_->MainAddTitle.Add_Space();
    progress_->Sync.Set_TitleFileName(ToUString(QFileInfo(source).fileName()));
    progress_->Sync.Set_NumBytesTotal(totalSize);
    volumeOperation_->attachProgress(progress_->Sync);
    connect(volumeOperation_, &VolumeOperation::result, this,
            [this, operation = volumeOperation_,
             completion = std::move(completion)](bool success, const QString &message) {
                if (app_.QtOperations().closed())
                    return;
                operation->wait();
                const bool canceled = operation->isCanceled();
                if (volumeOperation_ == operation)
                    volumeOperation_ = nullptr;
                operation->deleteLater();
                const QPointer<MainWindow> guard(this);
                completion();
                if (!guard || app_.QtOperations().closed())
                    return;
                if (!success && !canceled && progress_)
                    progress_->Sync.FinalMessage.ErrorMessage.Message = ToUString(message);
                closeProgressDialog(true);
                if (!guard || app_.QtOperations().closed())
                    return;
                if (success) {
                    showDirectory(projectedUi().directory, false);
                    if (guard && !app_.QtOperations().closed() && !app_.QtOperations().blocking())
                        statusBar()->showMessage(message);
                } else {
                    setState(State::Filesystem);
                }
            });
    setState(State::Operating);
    volumeOperation_->start();
}
