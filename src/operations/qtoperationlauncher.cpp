#include "operations/qtoperationlauncher.h"
#include "operations/qtoperationprocess.h"
#include "rep_operation_merged.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QLocalServer>
#include <QRemoteObjectHost>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <functional>
#include <optional>
#include <utility>

namespace {
class OperationChannel final : public OperationSource {
  public:
    OperationRequest Request;
    std::function<void()> Accept;
    std::function<void(const QString &)> Reject;
    std::function<void(const QStringList &, const QStringList &)> Outputs;
    std::function<bool(const OperationReport &)> Complete;
    OperationRequest request() const override { return Request; }
    void accept() override { Accept(); }
    void reject(QString error) override { Reject(error); }
    void outputs(QStringList directories, QStringList archives) override {
        Outputs(directories, archives);
    }
    bool complete(OperationReport report) override { return Complete(report); }
};
} // namespace

struct QtOperationLauncher::State {
    enum class Phase { Ready, AwaitingAcceptance, AcceptanceQueued, Accepted, Finished };
    QtOperationProcess process_;
    std::unique_ptr<QTemporaryDir> directory_;
    OperationChannel channel_;
    QRemoteObjectHost host_;
    QTimer poll_, startup_;
    QElapsedTimer exited_;
    std::optional<QtOperationResult> result_;
    QStringList changedDirectories_;
    QStringList changedArchives_;
    Phase phase_ = Phase::Ready;
    bool isAccepted() const {
        return phase_ == Phase::AcceptanceQueued || phase_ == Phase::Accepted;
    }
};

QtOperationLauncher::QtOperationLauncher(QObject *parent)
    : QObject(parent), state_(std::make_unique<State>()) {
    state_->startup_.setSingleShot(true);
    state_->poll_.setInterval(25);
    state_->channel_.Accept = [this] {
        if (state_->phase_ != State::Phase::AwaitingAcceptance || state_->result_) {
            protocolFailure(QCoreApplication::translate("QtOperation",
                                                        "Unexpected operation protocol message."));
            return;
        }
        state_->phase_ = State::Phase::AcceptanceQueued;
        state_->startup_.stop();
        // The child has copied and validated the request before this call.
        // A receiver may delete the observer. Leave QtRO's dispatch stack
        // before exposing that callback, so its connection stays alive there.
        QMetaObject::invokeMethod(
            this,
            [this] {
                state_->phase_ = State::Phase::Accepted;
                emit accepted();
            },
            Qt::QueuedConnection);
    };
    state_->channel_.Reject = [this](const QString &error) {
        protocolFailure(error);
        state_->startup_.stop();
    };
    state_->channel_.Outputs = [this](const QStringList &directories, const QStringList &archives) {
        if (!state_->isAccepted() || state_->result_ || directories.isEmpty() ||
            !ValidateOperationOutputs(directories, archives)) {
            protocolFailure(QCoreApplication::translate("QtOperation",
                                                        "Unexpected operation protocol message."));
            return;
        }
        for (const auto &directory : directories)
            if (!state_->changedDirectories_.contains(directory))
                state_->changedDirectories_.append(directory);
        for (const auto &archive : archives)
            if (!state_->changedArchives_.contains(archive))
                state_->changedArchives_.append(archive);
    };
    state_->channel_.Complete = [this](const OperationReport &report) {
        auto result = state_->isAccepted() && !state_->result_
                          ? OperationResultFromReport(report)
                          : std::unexpected(QtOperationResult::Outcome::ProtocolFailed);
        if (!result) {
            protocolFailure(QCoreApplication::translate("QtOperation",
                                                        "Unexpected operation protocol message."));
            return false;
        }
        state_->result_ = std::move(*result);
        return true;
    };
    connect(&state_->poll_, &QTimer::timeout, this, &QtOperationLauncher::observe);
    connect(&state_->startup_, &QTimer::timeout, this, [this] {
        protocolFailure(QCoreApplication::translate(
            "QtOperation", "Operation startup acknowledgement was not received."));
        // A lost acknowledgement does not authorize terminating independent work.
        state_->host_.disableRemoting(&state_->channel_);
    });
}
QtOperationLauncher::~QtOperationLauncher() = default;

QString QtOperationLauncher::GuiProgram() {
    return QDir(QCoreApplication::applicationDirPath())
        .filePath(
#ifdef Q_OS_WIN
            QStringLiteral("7zG.exe")
#else
            QStringLiteral("7zG")
#endif
        );
}

quint64 QtOperationLauncher::processId() const { return state_->process_.processId(); }

QtOperationLauncher *QtOperationLauncher::Launch(const QString &program, const QtOperationJob &job,
                                                 QString &error) {
    auto *application = QCoreApplication::instance();
    if (!application || QThread::currentThread() != application->thread()) {
        error = QCoreApplication::translate("QtOperation",
                                            "GUI operations require the application owner thread.");
        return nullptr;
    }
    auto operation = std::make_unique<QtOperationLauncher>(application);
    connect(operation.get(), &QtOperationLauncher::finished, operation.get(),
            &QObject::deleteLater);
    if (!operation->start(program, job, error))
        return nullptr;
    return operation.release();
}

bool QtOperationLauncher::start(const QString &program, const QtOperationJob &job, QString &error) {
    if (state_->phase_ != State::Phase::Ready) {
        error =
            QCoreApplication::translate("QtOperation", "An operation observer cannot be reused.");
        return false;
    }
    const auto request =
        ValidateOperationJob(job).transform([&] { return OperationRequestFromJob(job); });
    if (!request) {
        error = request.error();
        return false;
    }
    state_->channel_.Request = *request;
    QString channel;
#ifdef Q_OS_WIN
    channel = QStringLiteral("sevenzip-") + QUuid::createUuid().toString(QUuid::Id128);
#else
    // sockaddr_un cannot hold a socket under an arbitrarily long TMPDIR.
    // Both supported POSIX platforms provide /tmp. Keep the socket in a
    // private, short directory; job data never goes into a filesystem file.
    state_->directory_ = std::make_unique<QTemporaryDir>(QStringLiteral("/tmp/7z-XXXXXX"));
    if (!state_->directory_->isValid()) {
        error = state_->directory_->errorString();
        return false;
    }
    channel = state_->directory_->filePath(QStringLiteral("operation"));
#endif
    QRemoteObjectHost::setLocalServerOptions(QLocalServer::UserAccessOption);
    if (!state_->host_.setHostUrl(QUrl(QStringLiteral("local:") + channel)) ||
        !state_->host_.enableRemoting<OperationSourceAPI>(&state_->channel_)) {
        error = QCoreApplication::translate("QtOperation", "Could not transfer the operation job.");
        return false;
    }
    const auto started = state_->process_.start(program, channel).transform([&] {
        state_->phase_ = State::Phase::AwaitingAcceptance;
        state_->startup_.start(10000);
        state_->poll_.start();
    });
    if (!started) {
        error = started.error();
        state_->host_.disableRemoting(&state_->channel_);
        return false;
    }
    return true;
}

void QtOperationLauncher::protocolFailure(const QString &error) {
    if (!state_->result_ || state_->result_->Status != QtOperationResult::Outcome::ProtocolFailed)
        state_->result_ = QtOperationResult{QtOperationResult::Outcome::ProtocolFailed, 2, error};
}

void QtOperationLauncher::observe() {
    if (state_->phase_ == State::Phase::Finished ||
        state_->phase_ == State::Phase::AcceptanceQueued)
        return;
    const auto native = state_->process_.poll();
    if (native.Status == QtOperationProcess::State::Running)
        return;
    // Allow queued Remote Objects calls to arrive after native exit. The child
    // normally waits for complete()'s reply, so a result is already captured.
    if (!state_->exited_.isValid())
        state_->exited_.start();
    // A crash can precede delivery of output notifications already queued by
    // the child. Retain the bounded drain window even without a final report.
    if (!state_->result_ && state_->exited_.elapsed() < 1000)
        return;
    using Outcome = QtOperationResult::Outcome;
    if (native.Status == QtOperationProcess::State::Crashed) {
        finish({Outcome::Crashed, int(native.Code),
                QCoreApplication::translate("QtOperation",
                                            "The operation process terminated unexpectedly.")});
        return;
    }
    if (native.Status == QtOperationProcess::State::ObservationFailed) {
        finish({Outcome::ProtocolFailed, 2, native.Error});
        return;
    }
    // Native exit and local socket notification can be delivered in either
    // order. Allow buffered result/EOF notifications to reach the observer.
    if (state_->result_) {
        auto result = *state_->result_;
        if (result.Status != Outcome::ProtocolFailed && native.Code != quint32(result.ExitCode))
            result = {Outcome::ProtocolFailed, int(native.Code),
                      QCoreApplication::translate("QtOperation",
                                                  "Operation result and process exit disagree.")};
        finish(result);
    } else if (state_->exited_.elapsed() >= 1000)
        finish({Outcome::ProtocolFailed, int(native.Code),
                QCoreApplication::translate(
                    "QtOperation", "Operation process exited without a complete result.")});
}

void QtOperationLauncher::finish(QtOperationResult result) {
    if (state_->phase_ == State::Phase::Finished)
        return;
    state_->phase_ = State::Phase::Finished;
    for (const auto &directory : state_->changedDirectories_)
        if (!result.ChangedDirectories.contains(directory))
            result.ChangedDirectories.append(directory);
    for (const auto &archive : state_->changedArchives_)
        if (!result.ChangedArchives.contains(archive))
            result.ChangedArchives.append(archive);
    state_->poll_.stop();
    state_->startup_.stop();
    state_->host_.disableRemoting(&state_->channel_);
    emit finished(result);
}
