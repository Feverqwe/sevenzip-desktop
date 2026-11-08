#include "filemanager/window/qtlocalfiletask.h"
#include "7zip/UI/FileManager/ProgressDialog2Res.h"
#include "Common/StringConvert.h"
#include "archive/callbacks/IFileExtractCallback.h"
#include "dialogs/overwrite/OverwriteDialog.h"
#include "filemanager/panels/Panel.h"
#include "fileops/transfer/filetransferoperation.h"
#include "localization/qtlangcontrols.h"
#include "settings/ViewSettings.h"
#include "ui/lifecycle/qttaskpresentation.h"
#include "ui/progress/ProgressDialog2.h"
#include "ui/progress/qtcallbackprogresstransport.h"
#include <QApplication>
#include <QFileInfo>
#include <QPushButton>
#include <QScopeGuard>
#include <QTimer>
#include <utility>

namespace {
class TaskProgress final : public CProgressDialog {
    std::function<bool()> showPending_;
    std::function<void(bool)> presentation_;
    bool completed_ = false;

  public:
    TaskProgress(const UString &title, std::function<bool()> showPending,
                 std::function<void(bool)> presentation)
        : CProgressDialog(nullptr, title), showPending_(std::move(showPending)),
          presentation_(std::move(presentation)) {
        auto *cancel = qobject_cast<QPushButton *>(GetItem(IDCANCEL));
        disconnect(cancel, nullptr, this, nullptr);
        connect(cancel, &QPushButton::clicked, this, &TaskProgress::reject);
    }
    void Ready() {
        completed_ = true;
        GetItem(IDCANCEL)->setEnabled(false);
    }
    void reject() override {
        presentation_(true);
        const auto returned = qScopeGuard([presentation = presentation_] { presentation(false); });
        if (!completed_ || !showPending_())
            CProgressDialog::reject();
    }
};
} // namespace

QtLocalFileTask::QtLocalFileTask(QThread &worker, const QString &title, const QString &,
                                 std::function<void()> cancel)
    : QObject(qApp), worker_(worker), cancel_(std::move(cancel)) {
    worker.setParent(this);
    const QPointer<QtLocalFileTask> owner(this);
    progress_ = new TaskProgress(
        ToUString(title),
        [this] {
            if (!pending_)
                return false;
            Show();
            return true;
        },
        [owner](bool entered) {
            if (!owner)
                return;
            if (entered)
                ++owner->presentationDepth_;
            else if (!--owner->presentationDepth_ && owner->stopped_)
                owner->deleteLater();
        });
    progress_->setWindowModality(Qt::NonModal);
    progress_->CompressingMode = false;
    progress_->ShowCompressionInfo = false;
    transport_ = std::make_unique<QtCallbackProgressTransport>(cancel_);
    progress_->AttachTransport(transport_.get());
    connect(progress_, &QDialog::finished, this, &QObject::deleteLater);
    connect(qApp, &QApplication::focusChanged, this, [this] {
        if (pending_)
            QTimer::singleShot(0, this, &QtLocalFileTask::TryPresent);
    });
    connect(&QtTaskPresentation::Application(), &QtTaskPresentation::changed, this, [this] {
        if (pending_)
            QTimer::singleShot(0, this, &QtLocalFileTask::TryPresent);
    });
}

QtLocalFileTask::QtLocalFileTask(FileTransferOperation &worker, const QString &title,
                                 const QString &detail)
    : QtLocalFileTask(worker, title, detail, [&worker] { worker.cancel(); }) {
    connect(&worker, &FileTransferOperation::overwriteRequested, this,
            [this, &worker](const OverwriteQuestion &question) {
                RequestPresentation([this, &worker, question] { AskOverwrite(worker, question); });
            });
    auto *sync = &progress_->Sync;
    connect(
        &worker, &FileTransferOperation::currentFile, this,
        [sync](const QString &path) { sync->Set_FilePath(path.toStdWString().c_str()); },
        Qt::DirectConnection);
    connect(
        &worker, &FileTransferOperation::advanced, this,
        [sync, completed = UInt64(0)](qint64 bytes) mutable {
            completed += quint64(qMax<qint64>(0, bytes));
            sync->Set_NumBytesCur(completed);
        },
        Qt::DirectConnection);
    connect(&worker, &FileTransferOperation::result, this,
            [this, &worker](bool success, const QString &message, const QStringList &movedSources) {
                Finish(success, worker.resultKind() == FileTransferOperation::ResultKind::Canceled,
                       message,
                       worker.resultKind() == FileTransferOperation::ResultKind::PartialSuccess,
                       movedSources);
            });
}

QtLocalFileTask::~QtLocalFileTask() {
    if (worker_.isRunning()) {
        cancel_();
        worker_.wait();
    }
    if (progress_)
        progress_->AttachTransport(nullptr);
    transport_.reset();
    delete progress_.data();
}

void QtLocalFileTask::Start() {
    if (stopped_)
        return;
    progress_->OnInit();
    QtTaskPresentation::BeginWork(*progress_);
    progress_->show();
    worker_.start();
}

void QtLocalFileTask::BindOwner(CPanel &source, QWidget &window) {
    source_ = &source;
    progress_->setParent(&window, progress_->windowFlags());
    progress_->MainWindow = &window;
    progress_->MainTitle = ToUString(window.windowTitle());
    progress_->setWindowModality(Qt::WindowModal);
    connect(&source, &QObject::destroyed, this, [this] { StopAndWait(); });
    connect(&window, &QObject::destroyed, this, [this] { StopAndWait(); });
}

void QtLocalFileTask::SetLocations(const QString &source, const QString &destination) {
    progress_->SetLocations(source, destination);
}

void QtLocalFileTask::StopForSource(CPanel &source) {
    for (auto *task : Active())
        if (task->source_ == &source)
            task->StopAndWait();
}

void QtLocalFileTask::StopAndWait() {
    if (std::exchange(stopped_, true))
        return;
    pending_ = {};
    disconnect(&worker_, nullptr, this, nullptr);
    // This also removes completion continuations whose receiver is the task,
    // rather than the file window. None may publish into a released source.
    disconnect(this, nullptr, nullptr, nullptr);
    cancel_();
    worker_.wait();
    if (progress_) {
        progress_->AttachTransport(nullptr);
        // A result/question may still be executing a nested modal stack.
        // Keep that stack's widgets alive after the source window is destroyed.
        progress_->setParent(nullptr, progress_->windowFlags());
        progress_->MainWindow = nullptr;
        for (auto *dialog : progress_->findChildren<QDialog *>())
            dialog->reject();
        progress_->hide();
    }
    transport_.reset();
    if (!presentationDepth_)
        deleteLater();
}

bool QtLocalFileTask::OwnsWindow(const QWidget &window) const {
    if (overwrite_ == &window)
        return true;
    for (auto *owner = &window; owner; owner = owner->parentWidget())
        if (owner == progress_)
            return true;
    return false;
}

void QtLocalFileTask::AskOverwrite(FileTransferOperation &worker,
                                   const OverwriteQuestion &question) {
    if (stopped_ || worker.isCanceled()) {
        worker.submitOverwriteAnswer(NOverwriteAnswer::kCancel);
        return;
    }
    const QPointer<FileTransferOperation> operation(&worker);
    const QPointer<QtLocalFileTask> self(this);
    ++presentationDepth_;
    const auto finishPresentation = qScopeGuard([self] {
        if (self && !--self->presentationDepth_ && self->stopped_)
            self->deleteLater();
    });
    QPointer<COverwriteDialog> dialog = new COverwriteDialog(progress_);
    dialog->setWindowModality(Qt::WindowModal);
    overwrite_ = dialog;
    dialog->SetQuestion(question);
    connect(&worker, &FileTransferOperation::dismissOverwriteRequested, dialog, &QDialog::reject,
            Qt::QueuedConnection);
    const int answer = NOverwriteDialog::MapButtonResult(dialog->Create(progress_));
    if (self)
        overwrite_ = nullptr;
    delete dialog.data();
    if (operation)
        operation->submitOverwriteAnswer(answer);
}

void QtLocalFileTask::Finish(bool success, bool cancelled, const QString &message, bool partial,
                             const QStringList &movedSources) {
    if (stopped_)
        return;
    worker_.wait();
    const QPointer<QtLocalFileTask> self(this);
    if (success || !movedSources.isEmpty())
        emit transferOutput(movedSources);
    if (!self || stopped_)
        return;
    emit transferCompleted(success && !partial);
    if (!self || stopped_)
        return;
    progress_->AttachTransport(nullptr);
    transport_.reset();
    progress_->SetOperationResult(cancelled ? E_ABORT : success ? S_OK : E_FAIL, partial);
    QtTaskPresentation::SetPhase(*progress_, QtOperationPhase::Reviewing);
    static_cast<TaskProgress *>(progress_.data())->Ready();
    progress_->RetireStopQuestion();
    RequestPresentation([this, success, cancelled, message, partial] {
        PresentFinish(success, cancelled, message, partial);
    });
}

void QtLocalFileTask::PresentFinish(bool success, bool cancelled, const QString &message,
                                    bool partial) {
    if (stopped_)
        return;
    if (!success && !cancelled)
        progress_->Sync.FinalMessage.ErrorMessage.Message = ToUString(message);
    if (cancelled)
        progress_->Sync.FinalMessage.OkMessage.Message = ToUString(
            QCoreApplication::translate("QtTaskResultDialog", "The operation was cancelled."));
    else if (partial)
        progress_->Sync.FinalMessage.OkMessage.Message = ToUString(message);
    progress_->setObjectName("completedProgressDialog");
    const QPointer<QtLocalFileTask> guard(this);
    ++presentationDepth_;
    const auto finishPresentation = qScopeGuard([guard] {
        if (guard && !--guard->presentationDepth_ && guard->stopped_)
            guard->deleteLater();
    });
    progress_->ProcessWasFinished();
    QCoreApplication::sendPostedEvents(progress_, QEvent::MetaCall);
    if (guard && !stopped_) {
        if (progress_->isVisible()) {
            connect(progress_, &QDialog::finished, this,
                    [this, success, cancelled, message, partial] {
                        if (!stopped_)
                            emit finished(success, cancelled, message, partial);
                    });
        } else
            emit finished(success, cancelled, message, partial);
    }
}

QList<QtLocalFileTask *> QtLocalFileTask::Active() {
    return qApp->findChildren<QtLocalFileTask *>(Qt::FindDirectChildrenOnly);
}

void QtLocalFileTask::Show() {
    if (stopped_)
        return;
    if (progress_ && progress_->isMinimized())
        progress_->showNormal();
    if (pending_) {
        auto action = std::exchange(pending_, {});
        action();
        return;
    }
    QWidget *window = overwrite_ && overwrite_->isVisible()
                          ? static_cast<QWidget *>(overwrite_.data())
                          : static_cast<QWidget *>(progress_.data());
    if (auto *modal = QApplication::activeModalWidget(); modal && OwnsWindow(*modal))
        window = modal;
    if (!window)
        return;
    if (window->isMinimized())
        window->showNormal();
    window->show();
    window->raise();
    window->activateWindow();
}

void QtLocalFileTask::RequestPresentation(std::function<void()> action) {
    if (stopped_)
        return;
    pending_ = std::move(action);
    TryPresent();
}

void QtLocalFileTask::TryPresent() {
    if (stopped_ || !pending_)
        return;
    if (!QtTaskPresentation::CanPresent(progress_)) {
        if (auto *modal = QApplication::activeModalWidget(); modal && blockedModal_ != modal) {
            if (blockedModal_)
                blockedModal_->removeEventFilter(this);
            blockedModal_ = modal;
            modal->installEventFilter(this);
            connect(modal, &QObject::destroyed, this,
                    [this] { QTimer::singleShot(0, this, &QtLocalFileTask::TryPresent); });
        }
        return;
    }
    auto action = std::exchange(pending_, {});
    action();
}

bool QtLocalFileTask::eventFilter(QObject *object, QEvent *event) {
    if (object == blockedModal_ && event->type() == QEvent::Hide)
        QTimer::singleShot(0, this, &QtLocalFileTask::TryPresent);
    return QObject::eventFilter(object, event);
}
