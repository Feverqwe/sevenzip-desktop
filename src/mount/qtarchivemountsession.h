#pragma once
#include "7zip/IStream.h"
#include "mount/qtarchivemountsource.h"
#include <QObject>
#include <QProcess>
#include <QThread>
#include <memory>

class QTemporaryDir;
class QtSmbMountPlatform;

class QtArchiveMountSession final : public QObject {
    Q_OBJECT
  public:
    enum class Status { Preparing, Mounting, Mounted, Unmounting, Stopping, Stopped };
    Q_ENUM(Status)
    QtArchiveMountSession(QString label, QObject *parent = nullptr);
    ~QtArchiveMountSession() override;
    void start(IUnknown *resource, quint64 quota, const QString &archivePassword,
               const QString &serverExecutable, const QString &folder = {},
               std::shared_ptr<ArchiveReadOnlyView> preparedView = {});
    void unmount(bool force = false);
    Status status() const;
    QString label() const;
    QString path() const;
    QString error() const;
    QString limitations() const;
    QStringList skippedEntries() const;

  signals:
    void changed();
    void stopped();
    void extractionRequired(QString path, quint64 size, ArchiveMountDecisionPtr decision);
    void unverifiedDataRequired(QString path, quint64 size, qint32 operation,
                                ArchiveMountDecisionPtr decision);
    void entryIssue(quint64 id, quint32 result, QString error, qint32 operation);

  private:
    const QString label_;
    QString path_, error_, limitations_, user_, password_, share_;
    QStringList skipped_;
    Status status_ = Status::Preparing;
    bool started_ = false, stopRequested_ = false;
    std::unique_ptr<QTemporaryDir> directory_;
    QThread worker_;
    QProcess child_;
    QtArchiveMountSource *source_ = nullptr;
    QtSmbMountPlatform *platform_;
    void setStatus(Status status);
    void fail(const QString &error);
    bool cleanupStarted() const;
    void cleanup();
    void finishCleanup();
};
