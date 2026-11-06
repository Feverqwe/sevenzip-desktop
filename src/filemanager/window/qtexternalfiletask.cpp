#include "Common/StringConvert.h"
#include "dialogs/copy/CopyDialog.h"
#include "filemanager/external/qtexternalarchivetarget.h"
#include "filemanager/external/qtexternalfilesession.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtexternalfiletask_p.h"
#include "filemanager/window/qtlocalfiletask.h"
#include "filemanager/window/qtworkspaceapplication.h"
#include "fileops/native/FileDir.h"
#include "fileops/transfer/filetransferoperation.h"
#include "localization/qtlangbuttonbox.h"
#include "localization/qtlangcontrols.h"
#include "platform/process/qtexternalprocess.h"
#include "platform/temp/qtownedtempidentity.h"
#include "platform/temp/qtownedtempregistry.h"
#include "ui/controls/qtsourcesummary.h"
#include "ui/dialogs/qtdialoglayout.h"
#include "ui/lifecycle/qttaskpresentation.h"
#include "ui/progress/ProgressDialog2.h"
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDir>
#include <QEventLoop>
#include <QEventLoopLocker>
#include <QFileInfo>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScopeGuard>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

namespace {
bool SameCopy(const QString &selected, const QString &stored) {
    if (selected == stored)
        return true;
    const QString identity = QtOwnedTempIdentity(selected);
    return !identity.isEmpty() && identity == QtOwnedTempIdentity(stored);
}

class Sessions final : public QObject {
  public:
    std::vector<std::shared_ptr<QtExternalFileTask>> records;
    explicit Sessions(QObject *parent) : QObject(parent) {}
    ~Sessions() override {
        for (const auto &record : records)
            record->Stop();
    }
    static Sessions &Application() {
        for (auto *child : qApp->children())
            if (auto *sessions = dynamic_cast<Sessions *>(child))
                return *sessions;
        return *new Sessions(qApp);
    }
    void Retire(QtExternalFileTask *task) {
        const auto found = std::find_if(records.begin(), records.end(), [task](const auto &record) {
            return record.get() == task;
        });
        if (found == records.end())
            return;
        auto retired = std::move(*found);
        records.erase(found);
        QTimer::singleShot(0, qApp, [retired = std::move(retired)] { (void)retired; });
    }
};

bool CopyAndWait(const QString &source, const QString &destination,
                 QPointer<FileTransferOperation> &active) {
    auto *worker = new FileTransferOperation({source}, destination, false, nullptr, {},
                                             FileTransferOperation::DestinationKind::DirectPath);
    auto *task =
        new QtLocalFileTask(*worker, QtExternalFileTask::tr("Preparing edited file"), source);
    active = worker;
    QEventLoop loop;
    bool success = false;
    QObject::connect(task, &QtLocalFileTask::transferCompleted, &loop, [&](bool complete) {
        success = complete;
        loop.quit();
    });
    QObject::connect(task, &QObject::destroyed, &loop, &QEventLoop::quit);
    task->Start();
    loop.exec();
    if (active && active->isRunning()) {
        active->cancel();
        active->wait();
    }
    active = nullptr;
    return success;
}
} // namespace

QtExternalFileTask::QtExternalFileTask(CPanel &source, const CTempFileInfo &file,
                                       const ArchiveBackend &backend, const QString &settings,
                                       const QString &prefix)
    : state_(std::make_unique<State>()) {
    state_->file = file;
    state_->target = QtExternalArchiveTarget::Capture(source, file, backend, settings);
    state_->prefix = prefix;
    state_->directoryIdentity =
        QtOwnedTempIdentity(QFileInfo(ToQString(fs2us(file.FilePath))).absolutePath());
    state_->record = QtExternalFileRecord{QFileInfo(ToQString(fs2us(file.FilePath))).fileName(),
                                          ToQString(file.RelPath),
                                          prefix,
                                          state_->target ? state_->target->Save() : QJsonObject{},
                                          {file.FileInfo.Size, file.FileInfo.MTime},
                                          {},
                                          false};
    state_->persisted = Persist();
    Initialize();
}

void QtExternalFileTask::Initialize() {
    connect(qApp, &QCoreApplication::aboutToQuit, this, &QtExternalFileTask::Stop);
    connect(this, &QtExternalFileTask::published, qApp, [](const QString &archive) {
        if (auto *application = QtWorkspaceApplication::Find()) {
            application->ArchivePublished(archive);
            return;
        }
        for (auto *widget : QApplication::topLevelWidgets())
            if (auto *window = dynamic_cast<MainWindow *>(widget))
                window->ArchivePublished(archive);
    });
}

std::shared_ptr<IQtExternalSessionOwner>
QtExternalFileTask::Create(CPanel &source, const CTempFileInfo &file, const ArchiveBackend &backend,
                           const QString &settings, const QString &prefix) {
    auto record = std::shared_ptr<QtExternalFileTask>(
        new QtExternalFileTask(source, file, backend, settings, prefix));
    Sessions::Application().records.push_back(record);
    return record;
}

QtExternalFileTask::~QtExternalFileTask() {
    Stop();
    delete state_->dialog.data();
}

void QtExternalFileTask::Stop() {
    state_->launcher.RequestExit();
    if (state_->question) {
        state_->question->reject();
        QTimer::singleShot(0, this, &QtExternalFileTask::Stop);
        return;
    }
    if (state_->updating) {
        if (state_->copy)
            state_->copy->cancel();
        if (state_->dialog)
            for (auto *dialog : state_->dialog->findChildren<QDialog *>())
                if (auto *progress = dynamic_cast<CProgressDialog *>(dialog))
                    progress->Sync.Set_Stopped(true);
        QTimer::singleShot(25, this, &QtExternalFileTask::Stop);
        return;
    }
    state_->launcher.Exit(true);
    if (state_->dialog)
        state_->dialog->close();
}
CExitEventLauncher &QtExternalFileTask::Launcher() { return state_->launcher; }
IQtProcessServices &QtExternalFileTask::ProcessServices() { return state_->processes; }
bool QtExternalFileTask::ReadyToLaunch() const { return state_->persisted; }

bool QtExternalFileTask::Details(const QtOwnedTempEntry &requested, const QString &path,
                                 const ArchiveBackend &backend, const QString &settings,
                                 bool show) {
    QtOwnedTempRegistry registry;
    const auto entries = registry.Entries();
    const auto found = std::find_if(entries.begin(), entries.end(), [&](const auto &entry) {
        return entry.key == requested.key && entry.path == requested.path &&
               entry.identity == requested.identity;
    });
    if (found == entries.end())
        return false;
    const bool directory = QtOwnedTempMatchesIdentity(path, found->identity);
    const bool child = QtOwnedTempMatchesIdentity(QFileInfo(path).absolutePath(), found->identity);
    if (!directory && !child)
        return false;
    auto saved = QtExternalFileRecord::Restore(found->context);
    if (directory && !saved)
        return false;
    const QString selected = directory ? QDir(found->path).filePath(saved->fileName) : path;
    auto &sessions = Sessions::Application();
    for (const auto &record : sessions.records) {
        if (record->state_->directoryIdentity != found->identity ||
            !SameCopy(selected, ToQString(fs2us(record->state_->file.FilePath))))
            continue;
        if (show)
            QTimer::singleShot(0, record.get(), [task = record.get()] { task->Show(); });
        return true;
    }
    if (!saved || (!directory && !SameCopy(path, QDir(found->path).filePath(saved->fileName)))) {
        if (!child || !QFileInfo(path).isFile() || QtOwnedTempIdentity(path).isEmpty())
            return false;
        NWindows::NFile::NFind::CFileInfo info;
        if (!info.Find(us2fs(ToUString(path))))
            return false;
        saved = QtExternalFileRecord{QFileInfo(path).fileName(),
                                     QFileInfo(path).fileName(),
                                     found->path,
                                     {},
                                     {info.Size, info.MTime},
                                     {},
                                     false};
    }
    if (show) {
        auto task = std::shared_ptr<QtExternalFileTask>(
            new QtExternalFileTask(*found, *saved, backend, settings));
        sessions.records.push_back(task);
        QTimer::singleShot(0, task.get(), [task] { task->Show(); });
    }
    return true;
}

QList<QtExternalFileTask *> QtExternalFileTask::Active() {
    QList<QtExternalFileTask *> result;
    for (const auto &record : Sessions::Application().records)
        if (record->state_->keepAlive || record->state_->updating)
            result.append(record.get());
    return result;
}

bool QtExternalFileTask::CanPresent() const {
    if (state_->exports)
        return false;
    for (const auto &record : Sessions::Application().records)
        if (record->state_->updating)
            return false;
    return QtTaskPresentation::CanPresent();
}

void QtExternalFileTask::Launched(bool knownProcess) {
    state_->launched = true;
    state_->known = knownProcess;
    state_->observation = QtExternalObservation::Unknown;
    if (knownProcess)
        state_->keepAlive = std::make_unique<QEventLoopLocker>();
    Refresh();
}

void QtExternalFileTask::PresentObservation(QtExternalObservation outcome) {
    state_->known = false;
    state_->observation = outcome;
    Show();
}

void QtExternalFileTask::Finished(QtExternalObservation outcome) {
    QMetaObject::invokeMethod(
        this,
        [this, outcome] {
            // The worker has left its GUI callback; join before retiring captures.
            state_->launcher.Join();
            state_->known = false;
            state_->observation = outcome;
            state_->keepAlive.reset();
            state_->file.OwnedTempLease.reset();
            Refresh();
            if (!state_->launched ||
                (outcome == QtExternalObservation::Completed &&
                 !QFileInfo::exists(ToQString(fs2us(state_->file.FilePath)))) ||
                !QtOwnedTempMatchesIdentity(
                    QFileInfo(ToQString(fs2us(state_->file.FilePath))).absolutePath(),
                    state_->directoryIdentity))
                Sessions::Application().Retire(this);
        },
        Qt::QueuedConnection);
}

std::shared_ptr<const QtOwnedTempLease> QtExternalFileTask::InputLease() const {
    const QString directory = QFileInfo(ToQString(fs2us(state_->file.FilePath))).absolutePath();
    if (!QtOwnedTempMatchesIdentity(directory, state_->directoryIdentity))
        return {};
    if (state_->file.OwnedTempLease)
        return state_->file.OwnedTempLease->PinForRead();
    QtOwnedTempRegistry registry;
    for (const auto &entry : registry.Entries())
        if (entry.path == directory && entry.identity == state_->directoryIdentity)
            return registry.PinForRead(entry);
    return {};
}

bool QtExternalFileTask::Update() {
    state_->known = false;
    state_->observation = QtExternalObservation::Completed;
    return CheckChanges();
}

bool QtExternalFileTask::CheckChanges() {
    if (state_->updating || state_->published || !CanPresent())
        return false;
    QEventLoopLocker keepAlive;
    state_->updating = true;
    Refresh();
    const auto finish = qScopeGuard([&] {
        state_->publishing = false;
        state_->updating = false;
        Refresh();
    });
    if (!state_->target) {
        state_->message =
            QT_TR_NOOP("This archive cannot be updated. Save a copy of the edited file.");
        Show();
        return false;
    }
    CTempFileInfo before = state_->file;
    const QString path = ToQString(fs2us(before.FilePath));
    const QString identity = QtOwnedTempIdentity(path);
    if (identity.isEmpty() || !before.FileInfo.Find(before.FilePath) || before.FileInfo.IsDir() ||
        !QtOwnedTempMatchesIdentity(QFileInfo(path).absolutePath(), state_->directoryIdentity)) {
        state_->message =
            QT_TR_NOOP("The temporary file is unavailable. The archive was not updated.");
        Show();
        return false;
    }
    if (!state_->file.WasChanged_from_TempFileInfo(before.FileInfo)) {
        state_->message = QT_TR_NOOP("No saved changes were detected.");
        Show();
        return false;
    }
    const int decision = AskForUpdate();
    if (decision == 2) {
        SaveCopy(nullptr, true);
        return false;
    }
    if (decision != QDialog::Accepted || state_->launcher.Exiting())
        return false;
    auto inputLease = InputLease();
    if (!inputLease)
        return false;
    NWindows::NFile::NDir::CTempDir stable;
    if (!stable.Create(FTEXT("7zE")))
        return false;
    const QString input = QDir(ToQString(fs2us(stable.GetPath()))).filePath("edited-input");
    if (!CopyAndWait(path, input, state_->copy))
        return false;
    NWindows::NFile::NFind::CFileInfo after;
    const auto current = [&] {
        return after.Find(before.FilePath) && identity == QtOwnedTempIdentity(path) &&
               !before.WasChanged_from_TempFileInfo(after);
    };
    if (!current() || QtOwnedTempIdentity(input).isEmpty()) {
        state_->message =
            QT_TR_NOOP("The temporary file changed during preparation. Check it again.");
        Show();
        return false;
    }
    state_->publishing = true;
    Show();
    const auto result = state_->target->Update(input, state_->dialog);
    state_->publishing = false;
    state_->retained.append(result.retainedInputs);
    state_->retained.removeDuplicates();
    state_->record->retainedInputs = state_->retained;
    state_->published = result.Published.value_or(false);
    if (state_->published) {
        emit published(state_->target->physicalPath());
        state_->record->published = QtNativeFileStamp{before.FileInfo.Size, before.FileInfo.MTime};
        state_->record->publicationError = result.Result != S_OK;
    }
    if (state_->published || !state_->retained.isEmpty()) {
        inputLease.reset();
        if (!Persist()) {
            state_->message =
                state_->published
                    ? QT_TR_NOOP("The archive was updated, but session details could not "
                                 "be saved. The copy was kept.")
                    : QT_TR_NOOP("The archive was not fully updated. The copies were kept, "
                                 "but their session details could not be saved.");
            Show();
            return false;
        }
    }
    if (result.Result != S_OK || !state_->published) {
        state_->message =
            QT_TR_NOOP("The archive was not fully updated. The edited copy was kept.");
        Show();
        return false;
    }
    state_->message = QT_TR_NOOP("The archive was updated.");
    const bool unchanged = current();
    if (!unchanged) {
        state_->message = QT_TR_NOOP(
            "The archive was updated, but the temporary file changed again. Its copy was kept.");
        Show();
    }
    return unchanged;
}
