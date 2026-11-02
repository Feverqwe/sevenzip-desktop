// Windows reuses upstream Windows/ProcessUtils CProcess (7-Zip
// 9128b80e3a471108678e2b3ec3c985861c8d7b0f). POSIX adapts only spawn/wait;
// upstream CompressCall's handle owner must not kill an independent GUI job.
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include "operations/qtoperationprocess.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QScopeGuard>
#ifdef Q_OS_WIN
#include "Windows/ProcessUtils.h"
#else
#include "platform/process/qtchildprocessreaper.h"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#ifdef Q_OS_MACOS
#include <crt_externs.h>
#else
extern char **environ;
#endif
#endif

#ifndef Q_OS_WIN
namespace {
int Spawn(pid_t *pid, const char *program, char *const *arguments, char *const *environment) {
    posix_spawnattr_t attributes;
    int error = ::posix_spawnattr_init(&attributes);
    if (error)
        return error;
    const auto closeAttributes = qScopeGuard([&] { ::posix_spawnattr_destroy(&attributes); });
    posix_spawn_file_actions_t files;
    error = ::posix_spawn_file_actions_init(&files);
    if (error)
        return error;
    const auto closeFiles = qScopeGuard([&] { ::posix_spawn_file_actions_destroy(&files); });
    // No manager descriptors, terminal ownership or stdio pipes may keep its
    // resources alive in the independent operation process. Results use IPC.
    short flags = POSIX_SPAWN_SETSID;
#ifdef Q_OS_MACOS
    flags |= POSIX_SPAWN_CLOEXEC_DEFAULT;
#else
    error = ::posix_spawn_file_actions_addclosefrom_np(&files, 3);
#endif
    if (!error)
        error = ::posix_spawnattr_setflags(&attributes, flags);
    for (int descriptor = 0; !error && descriptor < 3; ++descriptor)
        error = ::posix_spawn_file_actions_addopen(&files, descriptor, "/dev/null",
                                                   descriptor == 0 ? O_RDONLY : O_WRONLY, 0);
    if (error)
        return error;
    return ::posix_spawn(pid, program, &files, &attributes, arguments, environment);
}
} // namespace
#endif

struct QtOperationProcess::Native {
#ifdef Q_OS_WIN
    NWindows::CProcess Process;
#else
    QtChildProcessReaper Process;
#endif
};

QtOperationProcess::QtOperationProcess() : native_(std::make_unique<Native>()) {}
QtOperationProcess::~QtOperationProcess() = default;

quint64 QtOperationProcess::processId() const {
    if (result_.Status != State::Running)
        return 0;
#ifdef Q_OS_WIN
    return ::GetProcessId(native_->Process);
#else
    return quint64(native_->Process.Id());
#endif
}

std::expected<void, QString> QtOperationProcess::start(const QString &program,
                                                       const QString &channel) {
    if (result_.Status == State::Running || result_.Status == State::ObservationFailed) {
        return std::unexpected(
            QCoreApplication::translate("QtOperation", "An operation process is already running."));
    }
    // Only the generated channel address goes through the native command
    // line. All selection, options and secrets belong to the local channel.
    bool validChannel = !channel.isEmpty() && !channel.contains(QChar::Null);
#ifdef Q_OS_WIN
    static const QRegularExpression identifier(QStringLiteral("^[A-Za-z0-9_-]+$"));
    validChannel = validChannel && identifier.match(channel).hasMatch();
#endif
    if (!QFileInfo(program).isAbsolute() || program.contains(QChar::Null) || !validChannel) {
        return std::unexpected(
            QCoreApplication::translate("QtOperation", "Invalid operation process or channel."));
    }
#ifdef Q_OS_WIN
    const UString arguments = UString(L"--operation-channel ") + channel.toStdWString().c_str();
    const WRes code = native_->Process.Create(program.toStdWString().c_str(), arguments, nullptr);
    if (code) {
        return std::unexpected(
            QCoreApplication::translate("QtOperation", "Cannot launch operation process (%1).")
                .arg(code));
    }
#else
    QByteArray executable = QFile::encodeName(program);
    QByteArray argument = QFile::encodeName(channel);
    char option[] = "--operation-channel";
    char *arguments[] = {executable.data(), option, argument.data(), nullptr};
#ifdef Q_OS_MACOS
    char **environment = *_NSGetEnviron();
#else
    char **environment = environ;
#endif
    native_->Process.Prepare();
    pid_t pid = 0;
    const int code = Spawn(&pid, executable.constData(), arguments, environment);
    if (code) {
        native_->Process.Reaped();
        return std::unexpected(QString::fromLocal8Bit(std::strerror(code)));
    }
    native_->Process.Adopt(pid);
#endif
    result_ = {State::Running, 0, {}};
    return {};
}

QtOperationProcess::Result QtOperationProcess::poll() {
    if (result_.Status != State::Running)
        return result_;
#ifdef Q_OS_WIN
    const DWORD wait = ::WaitForSingleObject(native_->Process, 0);
    if (wait == WAIT_TIMEOUT)
        return result_;
    DWORD code = 0;
    if (wait != WAIT_OBJECT_0 || !native_->Process.GetExitCodeProcess(&code)) {
        result_ = {
            State::ObservationFailed, 0,
            QCoreApplication::translate("QtOperation", "Cannot observe operation process (%1).")
                .arg(::GetLastError())};
    } else {
        result_ = {code >= 0x80000000u ? State::Crashed : State::Exited, quint32(code), {}};
        native_->Process.Close();
    }
#else
    int status = 0;
    pid_t process;
    do {
        process = ::waitpid(native_->Process.Id(), &status, WNOHANG);
    } while (process < 0 && errno == EINTR);
    if (process == 0)
        return result_;
    if (process < 0) {
        const int error = errno;
        result_ = {State::ObservationFailed, 0, {}};
        if (error == ECHILD)
            native_->Process.Reaped();
        result_.Error = QString::fromLocal8Bit(std::strerror(error));
    } else if (WIFEXITED(status)) {
        native_->Process.Reaped();
        result_ = {State::Exited, quint32(WEXITSTATUS(status)), {}};
    } else if (WIFSIGNALED(status)) {
        native_->Process.Reaped();
        result_ = {State::Crashed, quint32(WTERMSIG(status)), {}};
    } else
        return result_;
#endif
    return result_;
}
