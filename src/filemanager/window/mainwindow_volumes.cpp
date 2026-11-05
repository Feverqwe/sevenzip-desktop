#include "7zip/UI/FileManager/SplitDialogRes.h"
#include "7zip/UI/FileManager/resource.h"
#include "dialogs/split/SplitDialog.h"
#include "filemanager/FormatUtils.h"
#include "filemanager/SplitUtils.h"
#include "filemanager/window/mainwindow.h"
#include "fileops/volumes/nativevolumeworker.h"
#include "fileops/volumes/qtvolumeservices.h"
#include "localization/qtlangcontrols.h"
#include "ui/progress/ProgressDialog2.h"

class QtVolumeWindowServices final : public IQtVolumeServices {
    MainWindow &window_;
    const unsigned panel_;

  public:
    QtVolumeWindowServices(MainWindow &window, unsigned panel) : window_(window), panel_(panel) {}
    void StartSplit(const CSplitVolumePlan &plan, const UString &title,
                    std::function<void()> completion) override {
        const QString source = ToQString(fs2us(plan.FilePath));
        window_.launchVolume(panel_, new VolumeOperation(plan, &window_), title, source,
                             UInt64(QFileInfo(source).size()), std::move(completion));
    }
    void StartCombine(const CCombineVolumePlan &plan, const UString &title,
                      std::function<void()> completion) override {
        window_.launchVolume(panel_, new VolumeOperation(plan, &window_), title,
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
    QtVolumeWindowServices services(*this, app_.GetFocusedPanelIndex());
    if (mode == VolumeOperation::Mode::Split)
        app_.Split(services);
    else
        app_.Combine(services);
}

void MainWindow::launchVolume(unsigned index, VolumeOperation *operation, const UString &title,
                              const QString &source, UInt64 totalSize,
                              std::function<void()> completion) {
    // Native Split/Combine retain their source across the modal plan dialog.
    // Restore that owner before the asynchronous operation publishes state.
    activatePanelUi(index);
    auto &panel = app_.Panels[index];
    CSelectedState selection;
    panel.SaveSelectedState(selection);
    volumeOperation_ = operation;
    beginWorkerProgress(ToQString(title), source,
                        [operation = volumeOperation_] { operation->cancel(); });
    auto *progress = progressDialog();
    progress->MainAddTitle = title;
    progress->MainAddTitle.Add_Space();
    progress->Sync.Set_TitleFileName(ToUString(QFileInfo(source).fileName()));
    progress->Sync.Set_NumBytesTotal(totalSize);
    volumeOperation_->attachProgress(progress->Sync);
    connect(volumeOperation_, &VolumeOperation::result, this,
            [this, index, selection, operation = volumeOperation_,
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
                if (auto *progress = progressDialog(); !success && !canceled && progress)
                    progress->Sync.FinalMessage.ErrorMessage.Message = ToUString(message);
                closeProgressDialog(true);
                if (!guard || app_.QtOperations().closed())
                    return;
                setState(State::Filesystem);
                if (success) {
                    app_.Panels[index].RefreshListCtrl(selection);
                    if (guard && !app_.QtOperations().closed() && !app_.QtOperations().blocking())
                        statusBar()->showMessage(message);
                }
            });
    setState(State::Operating);
    volumeOperation_->start();
}
