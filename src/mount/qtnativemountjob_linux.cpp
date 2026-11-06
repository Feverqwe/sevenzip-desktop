#include "mount/qtnativemountjob.h"
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <cerrno>
#include <cstring>
#include <limits>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
// The user cannot rename the mountpoint or substitute a symlink while the
// privileged client is resolving it. Only its final, empty directory is chowned.
bool runtimeDirectory(const QByteArray &path) {
    if (::mkdir(path.constData(), 0711) != 0 && errno != EEXIST)
        return false;
    struct stat info{};
    return ::lstat(path.constData(), &info) == 0 && S_ISDIR(info.st_mode) && info.st_uid == 0 &&
           (info.st_mode & 0022) == 0;
}
QString systemError() { return QString::fromLocal8Bit(std::strerror(errno)); }
bool requestingOwner(const QtNativeMountParameters &parameters, const QUrl &endpoint,
                     uid_t &owner) {
    bool valid = false;
    const auto value = parameters.Owner.toULongLong(&valid);
    struct stat socketInfo{};
    const auto socketPath = QFile::encodeName(endpoint.path());
    if (::geteuid() != 0 || !valid || value > std::numeric_limits<uid_t>::max() ||
        ::lstat(socketPath.constData(), &socketInfo) != 0 || !S_ISSOCK(socketInfo.st_mode) ||
        socketInfo.st_uid != uid_t(value))
        return false;
    owner = uid_t(value);
    return true;
}
QString mountDirectory(uid_t owner, const QString &share) {
    return QStringLiteral("/run/sevenzip-desktop/mounts/%1-%2").arg(owner).arg(share);
}
} // namespace

struct QtNativeMountJob::State {
    QProcess Process;
    std::unique_ptr<QTemporaryDir> Credentials;
    QString Path, Error;
    bool Mounted = false, Unmounting = false, Running = false;
    void removeEmptyTarget() {
        if (!Mounted && !Path.isEmpty()) {
            ::rmdir(QFile::encodeName(Path).constData());
            Path.clear();
        }
    }
};

QtNativeMountJob::QtNativeMountJob(QObject *parent)
    : QObject(parent), state_(std::make_unique<State>()) {
    const auto finished = [this](bool success) {
        if (!state_->Running)
            return;
        state_->Running = false;
        const QString diagnostic =
            QString::fromLocal8Bit(state_->Process.readAllStandardError()).trimmed();
        state_->Credentials.reset();
        if (state_->Unmounting) {
            if (success) {
                state_->Mounted = false;
                state_->removeEmptyTarget();
            }
            emit unmountFinished(
                success
                    ? QString()
                    : tr("Could not unmount the read-only volume: %1")
                          .arg(diagnostic.isEmpty() ? state_->Process.errorString() : diagnostic));
        } else {
            state_->Mounted = success;
            if (!success)
                state_->removeEmptyTarget();
            emit mountFinished(
                success ? state_->Path : QString(),
                success
                    ? QString()
                    : tr("The system CIFS client could not mount the volume: %1\n"
                         "Check that your distribution's CIFS support and cifs-utils are "
                         "installed.")
                          .arg(diagnostic.isEmpty() ? state_->Process.errorString() : diagnostic));
        }
    };
    connect(&state_->Process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [finished](int code, QProcess::ExitStatus status) {
                finished(code == 0 && status == QProcess::NormalExit);
            });
    connect(&state_->Process, &QProcess::errorOccurred, this,
            [finished](QProcess::ProcessError error) {
                if (error == QProcess::FailedToStart)
                    finished(false);
            });
    connect(&state_->Process, &QProcess::readyReadStandardOutput, this,
            [this] { state_->Process.readAllStandardOutput(); });
}
QtNativeMountJob::~QtNativeMountJob() = default;
bool QtNativeMountJob::mounted() const { return state_->Mounted; }
bool QtNativeMountJob::busy() const { return state_->Running; }

void QtNativeMountJob::mount(const QtNativeMountParameters &parameters, const QUrl &endpoint) {
    if (busy() || mounted()) {
        emit mountFinished({}, tr("A system mount operation is already active."));
        return;
    }
    const auto fail = [this](const QString &error) { emit mountFinished({}, error); };
    const QString client = QStandardPaths::findExecutable("mount.cifs", {"/usr/sbin", "/sbin"});
    if (client.isEmpty()) {
        fail(tr("Install system CIFS support and cifs-utils with your distribution's package "
                "manager, then retry. Sevenzip does not provide a filesystem driver."));
        return;
    }
    uid_t owner = 0;
    if (!requestingOwner(parameters, endpoint, owner)) {
        fail(tr("The private mount channel does not belong to the requesting user, or "
                "administrator authorization is unavailable."));
        return;
    }
    const passwd *account = ::getpwuid(uid_t(owner));
    if (!account) {
        fail(tr("Could not identify the requesting user's primary group."));
        return;
    }
    const gid_t group = account->pw_gid;
    if (!runtimeDirectory("/run/sevenzip-desktop") ||
        !runtimeDirectory("/run/sevenzip-desktop/mounts")) {
        fail(tr("Could not create a protected mount directory: %1").arg(systemError()));
        return;
    }
    state_->Path = mountDirectory(owner, parameters.Share);
    const QByteArray target = QFile::encodeName(state_->Path);
    if (::mkdir(target.constData(), 0700) != 0) {
        state_->Path.clear();
        fail(tr("Could not create the mountpoint: %1").arg(systemError()));
        return;
    }
    if (::chown(target.constData(), uid_t(owner), group) != 0) {
        const QString error = systemError();
        state_->removeEmptyTarget();
        fail(tr("Could not restrict the mountpoint to the requesting user: %1").arg(error));
        return;
    }
    state_->Credentials =
        std::make_unique<QTemporaryDir>(QStringLiteral("/run/sevenzip-desktop/credentials-XXXXXX"));
    QFile credentials(state_->Credentials->filePath("login"));
    const QByteArray login = "username=" + parameters.User.toUtf8() +
                             "\npassword=" + parameters.Password.toUtf8() + "\n";
    if (!state_->Credentials->isValid() ||
        !credentials.open(QIODevice::WriteOnly | QIODevice::NewOnly) ||
        !credentials.setPermissions(QFile::ReadOwner | QFile::WriteOwner) ||
        credentials.write(login) != login.size() || !credentials.flush()) {
        state_->Credentials.reset();
        state_->removeEmptyTarget();
        fail(tr("Could not create private credentials for the system CIFS client."));
        return;
    }
    credentials.close();
    const QString options =
        QStringLiteral("ro,nosuid,nodev,port=%1,vers=2.1,sec=ntlmssp,nounix,uid=%2,gid=%3,"
                       "file_mode=0400,dir_mode=0500,cache=none,noserverino,credentials=%4")
            .arg(parameters.Port)
            .arg(owner)
            .arg(group)
            .arg(credentials.fileName());
    state_->Unmounting = false;
    state_->Running = true;
    state_->Process.start(
        client, {QStringLiteral("//127.0.0.1/") + parameters.Share, state_->Path, "-o", options});
}
void QtNativeMountJob::unmount(bool force) {
    if (busy())
        return;
    if (!mounted()) {
        emit unmountFinished({});
        return;
    }
    const QString client = QStandardPaths::findExecutable("umount", {"/usr/bin", "/bin"});
    if (client.isEmpty()) {
        emit unmountFinished(tr("The system umount command is unavailable."));
        return;
    }
    state_->Unmounting = true;
    state_->Running = true;
    QStringList arguments;
    if (force)
        arguments << "-l";
    arguments << "--" << state_->Path;
    state_->Process.start(client, arguments);
}
void QtNativeMountJob::recover(const QtNativeMountParameters &parameters, const QUrl &endpoint,
                               bool force) {
    uid_t owner = 0;
    if (!requestingOwner(parameters, endpoint, owner)) {
        emit unmountFinished(
            tr("The private mount channel does not belong to the requesting user, or "
               "administrator authorization is unavailable."));
        return;
    }
    QFile table(QStringLiteral("/proc/self/mountinfo"));
    if (!table.open(QIODevice::ReadOnly)) {
        emit unmountFinished(tr("Could not inspect the system mount table."));
        return;
    }
    const QString target = mountDirectory(owner, parameters.Share);
    const auto expected = QByteArray("//127.0.0.1/") + parameters.Share.toUtf8();
    bool found = false;
    // procfs reports a zero file size; consume reads rather than trusting
    // QFile::atEnd() before the first read.
    for (auto line = table.readLine(); !line.isEmpty(); line = table.readLine()) {
        const auto parts = line.split(' ');
        if (parts.size() < 7 || parts[4] != target.toUtf8())
            continue;
        const auto separator = line.indexOf(" - ");
        const auto volume =
            separator >= 0 ? line.mid(separator + 3).split(' ') : QList<QByteArray>();
        if (volume.size() < 3 || volume[0] != "cifs" || volume[1] != expected ||
            !parts[5].split(',').contains("ro")) {
            emit unmountFinished(
                tr("The mount belongs to another session and has been preserved."));
            return;
        }
        found = true;
        break;
    }
    state_->Path = target;
    state_->Mounted = found;
    if (found)
        unmount(force);
    else {
        state_->removeEmptyTarget();
        emit unmountFinished({});
    }
}
