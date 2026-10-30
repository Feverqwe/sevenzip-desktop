#include "7zip/UI/FileManager/SplitDialogRes.h"
#include "7zip/UI/FileManager/resource.h"
#include "dialogs/split/SplitDialog.h"
#include "filemanager/FormatUtils.h"
#include "filemanager/SplitUtils.h"
#include "filemanager/panels/PanelSplitFile.h"
#include "filemanager/qtfilemanagercommands.h"
#include "filemanager/qtfilemanagerdrag.h"
#include "filemanager/qtprogramfilemenu.h"
#include "filemanager/rename/qtrenamecontroller.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtapppresentation.h"
#include "fileops/volumes/qtvolumeservices.h"
#include "localization/qtlangcontrols.h"
#include "platform/associations/qtassociatedfileservices.h"
#include "platform/trash/qttrashservice.h"
#include "ui/progress/ProgressDialog2.h"
#include <QCloseEvent>

MainWindow::~MainWindow() {
    if (drag_)
        drag_->Revoke();
    removeEventFilter(this);
    // QComboBox can emit activated from editingFinished while QWidget tears
    // down focus. Navigation must stop before MainWindow members are destroyed.
    QCloseEvent close;
    closeEvent(&close);
    if (!app_.QtOperations().closed()) {
        // Destruction cannot be vetoed; retain the same input lifetime even
        // when a modal child prevented an ordinary user close.
        app_.QtOperations().Close();
        for (auto &panel : app_.Panels)
            panel.Disable_Processing_Timer_Notify_StatusBar();
        quiesceOperations();
        app_.Save();
        app_.ReleaseApp();
    }
    // QWidget destroys its children after CApp's member lifetime ends. Stop
    // callback text before that teardown and clear published folder identities.
    for (auto &ui : panelUi_)
        if (auto *table = dynamic_cast<FileTable *>(ui.table)) {
            table->sourcePanel = nullptr;
            table->textFolder = nullptr;
        }
}

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
    if (state_ != State::Filesystem || volumeOperation_ ||
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
                    showDirectory(directory_, false);
                    if (guard && !app_.QtOperations().closed() && !app_.QtOperations().blocking())
                        statusBar()->showMessage(message);
                } else {
                    setState(State::Filesystem);
                }
            });
    setState(State::Operating);
    volumeOperation_->start();
}
