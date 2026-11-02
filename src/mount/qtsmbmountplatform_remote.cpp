#include "mount/qtnativeclientcontrol.h"
#include "mount/qtsmbmountplatform.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLocalServer>
#include <QOperatingSystemVersion>
#include <QProcess>
#include <QRemoteObjectHost>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>
#ifdef Q_OS_WIN
#include <QWinEventNotifier>
// clang-format off
#include <windows.h>
#include <shellapi.h>
// clang-format on
#else
#include <unistd.h>
#endif

struct QtSmbMountPlatform::State {
    std::unique_ptr<QRemoteObjectHost> Host;
    std::unique_ptr<QtNativeClientControl> Source;
    std::unique_ptr<QTemporaryDir> Directory;
    std::unique_ptr<QProcess> Helper = std::make_unique<QProcess>();
    bool Reported = false, Disconnected = false, Exited = false;
    QUrl Endpoint;
    QString LaunchFailure;
#ifdef Q_OS_WIN
    HANDLE Process = nullptr;
    std::unique_ptr<QWinEventNotifier> Watcher;
    ~State() {
        Host.reset();
        Watcher.reset();
        if (Process)
            CloseHandle(Process);
    }
#else
    ~State() {
        // Losing this channel makes the independent broker detach its mount.
        // Never kill it while it may be performing that OS operation.
        Host.reset();
        if (Helper->state() != QProcess::NotRunning) {
            auto *orphan = Helper.release();
            QObject::connect(orphan, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                             orphan, &QObject::deleteLater);
        }
    }
#endif
};
QtSmbMountPlatform::QtSmbMountPlatform(QObject *parent)
    : QObject(parent), state_(std::make_unique<State>()) {
    connect(
        state_->Helper.get(), qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this](int, QProcess::ExitStatus) { helperEnded(); }, Qt::QueuedConnection);
    connect(
        state_->Helper.get(), &QProcess::errorOccurred, this,
        [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart)
                launchFailed(state_->Helper->errorString());
        },
        Qt::QueuedConnection);
    // Do not retain process diagnostics or any command's input in the UI.
    connect(state_->Helper.get(), &QProcess::readyReadStandardError, this,
            [this] { state_->Helper->readAllStandardError(); });
    connect(state_->Helper.get(), &QProcess::readyReadStandardOutput, this,
            [this] { state_->Helper->readAllStandardOutput(); });
}
QtSmbMountPlatform::~QtSmbMountPlatform() = default;
void QtSmbMountPlatform::launchFailed(const QString &error) {
    state_->Exited = true;
    if (state_->Reported)
        emit unmountFinished(error);
    else {
        state_->Disconnected = true;
        state_->Host.reset();
        state_->Source.reset();
        state_->Directory.reset();
        emit mountFinished({}, error);
    }
}
void QtSmbMountPlatform::helperEnded() {
    state_->Exited = true;
    const bool reportFailure = !state_->Disconnected;
    QString error;
    if (reportFailure) {
        error = !state_->Reported && !state_->LaunchFailure.isEmpty()
                    ? state_->LaunchFailure
                    : tr("The system mount helper stopped unexpectedly.");
        if (state_->Reported || (state_->Source && state_->Source->connected())) {
            // Preserve the server even when a mount's path acknowledgement
            // was lost. A fresh broker can recover only this session's mount.
            state_->Reported = true;
            emit unmountFinished(error);
            return;
        }
    }
    state_->Host.reset();
    state_->Source.reset();
    state_->Directory.reset();
    if (reportFailure)
        emit mountFinished({}, error);
}

QString QtSmbMountPlatform::unavailableReason() {
#ifdef Q_OS_WIN
    if (QOperatingSystemVersion::current().microVersion() < 26100)
        return tr("Mounting on a free SMB port requires Windows 11 24H2 or Windows Server 2025 "
                  "or newer and administrator permission.");
#elif defined(Q_OS_LINUX)
    if (QStandardPaths::findExecutable("mount.cifs", {"/usr/sbin", "/sbin"}).isEmpty())
        return tr("System CIFS support is required. Install cifs-utils with your distribution's "
                  "package manager, then retry. Sevenzip does not provide a filesystem driver.");
#endif
    return {};
}

void QtSmbMountPlatform::mount(quint16 port, const QString &share, const QString &user,
                               const QString &password, const QString &directory) {
    const QString unavailable = unavailableReason();
    if (!unavailable.isEmpty()) {
        emit mountFinished({}, unavailable);
        return;
    }
    if (state_->Host) {
        emit mountFinished({}, tr("A system mount operation is already active."));
        return;
    }
    const QString owner = QtNativeClientOwnerIdentity();
    if (owner.isEmpty()) {
        emit mountFinished({}, tr("Could not identify the current user for private mount access."));
        return;
    }
    state_->Reported = state_->Disconnected = false;
    QUrl endpoint;
#ifdef Q_OS_WIN
    endpoint =
        QUrl(QStringLiteral("local:SevenzipNative-") + QUuid::createUuid().toString(QUuid::Id128));
#else
    state_->Directory = std::make_unique<QTemporaryDir>(QStringLiteral("/tmp/7zn-XXXXXX"));
    if (!state_->Directory->isValid()) {
        emit mountFinished({}, state_->Directory->errorString());
        return;
    }
    endpoint = QUrl(QStringLiteral("local:") + state_->Directory->filePath("control"));
#endif
    state_->Source = std::make_unique<QtNativeClientControl>(
        QtNativeMountParameters{port, share, user, password, owner, directory});
    state_->Host = std::make_unique<QRemoteObjectHost>();
    QRemoteObjectHost::setLocalServerOptions(QLocalServer::UserAccessOption);
    if (!state_->Host->setHostUrl(endpoint) ||
        !state_->Host->enableRemoting<NativeClientSourceAPI>(state_->Source.get())) {
        state_->Host.reset();
        state_->Source.reset();
        state_->Directory.reset();
        emit mountFinished({}, tr("Could not create the private system-mount channel."));
        return;
    }
    connect(
        state_->Source.get(), &QtNativeClientControl::mountReported, this,
        [this](const QString &path, const QString &error) {
            state_->Reported = true;
            if (!error.isEmpty())
                state_->Disconnected = true;
            emit mountFinished(path, error);
        },
        Qt::QueuedConnection);
    connect(
        state_->Source.get(), &QtNativeClientControl::unmountReported, this,
        [this](const QString &error) {
            state_->Disconnected = error.isEmpty();
            emit unmountFinished(error);
        },
        Qt::QueuedConnection);
    state_->Endpoint = endpoint;
    launchHelper();
}
void QtSmbMountPlatform::launchHelper() {
    state_->Exited = false;
    const auto endpoint = state_->Endpoint;
    QString helper = QDir(QCoreApplication::applicationDirPath()).filePath("SevenzipMount");
#ifdef Q_OS_WIN
    helper += QStringLiteral(".exe");
#endif
    if (!QFileInfo(helper).isExecutable()) {
        launchFailed(tr("The bundled system mount helper is missing or cannot be executed."));
        return;
    }
#ifdef Q_OS_WIN
    state_->Watcher.reset();
    if (state_->Process) {
        CloseHandle(state_->Process);
        state_->Process = nullptr;
    }
    const QString arguments = QStringLiteral("--source ") + endpoint.toString();
    SHELLEXECUTEINFOW execute{};
    execute.cbSize = sizeof(execute);
    execute.fMask = SEE_MASK_NOCLOSEPROCESS;
    execute.lpVerb = L"runas";
    const QString nativeHelper = QDir::toNativeSeparators(helper);
    execute.lpFile = reinterpret_cast<LPCWSTR>(nativeHelper.utf16());
    execute.lpParameters = reinterpret_cast<LPCWSTR>(arguments.utf16());
    execute.nShow = SW_HIDE;
    if (!ShellExecuteExW(&execute)) {
        launchFailed(tr("Administrator authorization was cancelled or unavailable."));
        return;
    }
    state_->Process = execute.hProcess;
    state_->Watcher = std::make_unique<QWinEventNotifier>(state_->Process, this);
    connect(state_->Watcher.get(), &QWinEventNotifier::activated, this, [this](HANDLE) {
        state_->Watcher->setEnabled(false);
        helperEnded();
    });
#else
#ifdef Q_OS_MACOS
    state_->Helper->start(helper, {"--source", endpoint.toString()});
#else
    QStringList arguments{helper, "--source", endpoint.toString()};
    const auto pkexec = QStandardPaths::findExecutable("pkexec", {"/usr/bin", "/bin"});
    if (geteuid() == 0)
        state_->Helper->start(helper, {"--source", endpoint.toString()});
    else if (!pkexec.isEmpty())
        state_->Helper->start(pkexec, arguments);
    else {
        // Existing authorization (for example an administrator's sudo session)
        // is usable without collecting an OS password in Sevenzip.
        arguments.prepend("-n");
        state_->LaunchFailure =
            tr("Administrator authorization is required. Enable your system's "
               "PolicyKit helper (pkexec), or ask your administrator to authorize "
               "the Sevenzip mount helper, then retry.");
        const auto sudo = QStandardPaths::findExecutable("sudo", {"/usr/bin", "/bin"});
        if (sudo.isEmpty()) {
            launchFailed(tr("Administrator authorization is required, but no system "
                            "authorization helper is available."));
            return;
        }
        state_->Helper->start(sudo, arguments);
    }
#endif
#endif
}
void QtSmbMountPlatform::unmount(const QString &, bool force) {
    if (state_->Source) {
        if (state_->Exited) {
            state_->Source->prepareRecovery(force);
            launchHelper();
        } else
            emit state_->Source->requestUnmount(force);
    } else
        emit unmountFinished(tr("The system mount helper is unavailable."));
}
