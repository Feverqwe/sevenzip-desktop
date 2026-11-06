#include "7zip/UI/FileManager/SplitDialogRes.h"
#include "7zip/UI/FileManager/resource.h"
#include "dialogs/split/SplitDialog.h"
#include "filemanager/FormatUtils.h"
#include "filemanager/SplitUtils.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtlocalfiletask.h"
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
                             ToQString(fs2us(plan.VolBasePath)), UInt64(QFileInfo(source).size()),
                             std::move(completion));
    }
    void StartCombine(const CCombineVolumePlan &plan, const UString &title,
                      std::function<void()> completion) override {
        window_.launchVolume(panel_, new VolumeOperation(plan, &window_), title,
                             ToQString(fs2us(plan.InputDirPrefix + plan.Names.Front())),
                             ToQString(fs2us(plan.OutputPath)), plan.TotalSize,
                             std::move(completion));
    }
};

void MainWindow::startVolume(VolumeOperation::Mode mode) {
    if (app_.QtOperations().closed())
        return;
    QtAppOperationState::CallScope call(app_.QtOperations());
    if (projectedUi().state != State::Filesystem || fileTask_ ||
        !(mode == VolumeOperation::Mode::Split ? split_ : combine_)->isEnabled())
        return;
    QtVolumeWindowServices services(*this, app_.GetFocusedPanelIndex());
    if (mode == VolumeOperation::Mode::Split)
        app_.Split(services);
    else
        app_.Combine(services);
}

void MainWindow::launchVolume(unsigned index, VolumeOperation *operation, const UString &title,
                              const QString &source, const QString &destination, UInt64 totalSize,
                              std::function<void()> completion) {
    // Native Split/Combine retain their source across the modal plan dialog.
    // Restore that owner before the asynchronous operation publishes state.
    activatePanelUi(index);
    auto &panel = app_.Panels[index];
    CSelectedState selection;
    panel.SaveSelectedState(selection);
    auto *task = new QtLocalFileTask(*operation, ToQString(title), source, totalSize);
    task->SetLocations(source, destination);
    fileTask_ = task;
    task->BindOwner(panel, *this);
    connect(task, &QtLocalFileTask::finished, this,
            [this, index, selection, task,
             completion = std::move(completion)](bool success, bool, const QString &message) {
                if (app_.QtOperations().closed())
                    return;
                if (fileTask_ == task)
                    fileTask_ = nullptr;
                const QPointer<MainWindow> guard(this);
                completion();
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
    task->Start();
}
