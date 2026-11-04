#include "mount/qtarchivemountsession.h"
#include "mount/qtsmbmountplatform.h"
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>

QtArchiveMountSession::QtArchiveMountSession(QString label, QObject *parent)
    : QObject(parent), label_(std::move(label)), platform_(new QtSmbMountPlatform(this)) {
    if (qEnvironmentVariableIsSet("SEVENZIP_SMB_TRACE"))
        child_.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    connect(platform_, &QtSmbMountPlatform::mountFinished, this,
            [this](const QString &directory, const QString &error) {
                if (!error.isEmpty() || directory.isEmpty()) {
                    setStatus(Status::Preparing);
                    fail(error.isEmpty() ? tr("The system returned no mount path.") : error);
                    return;
                }
                path_ = directory;
                setStatus(Status::Mounted);
                if (stopRequested_)
                    unmount();
            });
    connect(platform_, &QtSmbMountPlatform::unmountFinished, this, [this](const QString &error) {
        if (!error.isEmpty()) {
            error_ = tr("Could not unmount. Close files using this mount and retry. %1").arg(error);
            setStatus(Status::Mounted);
            return;
        }
        path_.clear();
        cleanup();
    });
    connect(&worker_, &QThread::finished, this, [this] {
        source_ = nullptr;
        finishCleanup();
    });
    connect(&child_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (!cleanupStarted() && error == QProcess::FailedToStart)
            fail(tr("Could not start the bundled SMB server: %1").arg(child_.errorString()));
    });
    connect(&child_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int, QProcess::ExitStatus) {
                if (!cleanupStarted())
                    fail(tr("The SMB server stopped unexpectedly."));
                finishCleanup();
            });
    // Diagnostic output is not a protocol and must not accumulate in memory.
    connect(&child_, &QProcess::readyReadStandardError, this,
            [this] { child_.readAllStandardError(); });
    connect(&child_, &QProcess::readyReadStandardOutput, this,
            [this] { child_.readAllStandardOutput(); });
}

QtArchiveMountSession::~QtArchiveMountSession() {
    // The UI must complete unmount before destroying a live session. Keep the
    // directory on an abnormal application shutdown; never recurse into a mount.
    if (source_ && worker_.isRunning())
        source_->requestCancel();
    if (worker_.isRunning()) {
        QMetaObject::invokeMethod(source_, &QtArchiveMountSource::close,
                                  Qt::BlockingQueuedConnection);
        worker_.quit();
        worker_.wait();
    }
    if (child_.state() != QProcess::NotRunning) {
        child_.kill();
        child_.waitForFinished(3000);
    }
}

void QtArchiveMountSession::start(IUnknown *resource, quint64 quota, const QString &archivePassword,
                                  const QString &serverExecutable, const QString &folder,
                                  std::shared_ptr<ArchiveReadOnlyView> preparedView) {
    if (started_)
        return;
    started_ = true;
#ifdef Q_OS_UNIX
    directory_ = std::make_unique<QTemporaryDir>(QStringLiteral("/tmp/7zm-XXXXXX"));
#else
    directory_ = std::make_unique<QTemporaryDir>();
#endif
    if (!directory_->isValid() || !QDir(directory_->path()).mkdir("volume")) {
        fail(tr("Could not create a private mount directory."));
        return;
    }
    // Removing a mounted tree recursively would access its contents. Remove
    // only our known empty directories after the OS confirms unmount success.
    directory_->setAutoRemove(false);
#ifdef Q_OS_WIN
    const QUrl endpoint(QStringLiteral("local:SevenzipSource-") +
                        QUuid::createUuid().toString(QUuid::Id128));
#else
    const QUrl endpoint(QStringLiteral("local:") + directory_->filePath("source"));
#endif
    user_ = QStringLiteral("sz") + QUuid::createUuid().toString(QUuid::Id128);
    password_ = QUuid::createUuid().toString(QUuid::Id128);
    share_ = QStringLiteral("Sevenzip-") + QUuid::createUuid().toString(QUuid::Id128);
    source_ = new QtArchiveMountSource(resource, quota, archivePassword, user_, password_, endpoint,
                                       share_, folder, std::move(preparedView));
    source_->moveToThread(&worker_);
    connect(&worker_, &QThread::started, source_, &QtArchiveMountSource::initialize);
    connect(&worker_, &QThread::finished, source_, &QObject::deleteLater);
    connect(source_, &QtArchiveMountSource::closed, &worker_, &QThread::quit, Qt::DirectConnection);
    connect(source_, &QtArchiveMountSource::extractionRequired, this,
            &QtArchiveMountSession::extractionRequired);
    connect(source_, &QtArchiveMountSource::unverifiedDataRequired, this,
            &QtArchiveMountSession::unverifiedDataRequired);
    connect(source_, &QtArchiveMountSource::entryIssue, this, &QtArchiveMountSession::entryIssue);
    connect(source_, &QtArchiveMountSource::failure, this, &QtArchiveMountSession::fail);
    connect(
        source_, &QtArchiveMountSource::prepared, this,
        [this, endpoint, serverExecutable](const QString &limitations, const QStringList &skipped) {
            if (cleanupStarted())
                return;
            limitations_ = limitations;
            skipped_ = skipped;
            emit changed();
            if (cleanupStarted())
                return;
            child_.start(serverExecutable, {"--source", endpoint.toString()});
            QTimer::singleShot(15000, this, [this] {
                if (status_ == Status::Preparing)
                    fail(tr("The SMB server did not become ready."));
            });
        });
    connect(source_, &QtArchiveMountSource::serverListening, this, [this](quint16 port) {
        if (cleanupStarted())
            return;
        setStatus(Status::Mounting);
        platform_->mount(port, share_, user_, password_,
                         QFileInfo(directory_->filePath("volume")).canonicalFilePath());
    });
    worker_.start();
}

void QtArchiveMountSession::unmount(bool force) {
    stopRequested_ = true;
    if (status_ == Status::Mounted) {
        error_.clear();
        setStatus(Status::Unmounting);
        platform_->unmount(path_, force);
    } else if (status_ == Status::Preparing) {
        cleanup();
    }
    // An in-flight native mount keeps its server alive. On completion it is
    // immediately unmounted, including when the user cancels during mounting.
}
void QtArchiveMountSession::fail(const QString &error) {
    error_ = error;
    if (status_ == Status::Mounted || status_ == Status::Unmounting ||
        status_ == Status::Mounting) {
        emit changed();
        return;
    }
    cleanup();
}
void QtArchiveMountSession::cleanup() {
    if (cleanupStarted())
        return;
    setStatus(Status::Stopping);
    if (source_) {
        source_->requestCancel();
        QMetaObject::invokeMethod(source_, &QtArchiveMountSource::close, Qt::QueuedConnection);
    }
    if (child_.state() != QProcess::NotRunning) {
        child_.terminate();
        QTimer::singleShot(3000, this, [this] {
            if (cleanupStarted() && child_.state() != QProcess::NotRunning)
                child_.kill();
        });
    }
    finishCleanup();
}
void QtArchiveMountSession::finishCleanup() {
    if (!cleanupStarted() || worker_.isRunning() || child_.state() != QProcess::NotRunning)
        return;
    if (directory_ && directory_->isValid()) {
        QDir root(directory_->path());
        root.rmdir("volume");
        QDir().rmdir(root.path());
    }
    if (status_ != Status::Stopped) {
        setStatus(Status::Stopped);
        emit stopped();
    }
}
bool QtArchiveMountSession::cleanupStarted() const {
    return status_ == Status::Stopping || status_ == Status::Stopped;
}
void QtArchiveMountSession::setStatus(Status status) {
    status_ = status;
    emit changed();
}
QtArchiveMountSession::Status QtArchiveMountSession::status() const { return status_; }
QString QtArchiveMountSession::label() const { return label_; }
QString QtArchiveMountSession::path() const { return path_; }
QString QtArchiveMountSession::error() const { return error_; }
QString QtArchiveMountSession::limitations() const { return limitations_; }
QStringList QtArchiveMountSession::skippedEntries() const { return skipped_; }
