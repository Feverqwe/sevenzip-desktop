#pragma once
#include "fileops/overwrite/overwritequestion.h"

#include <QMutex>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QWaitCondition>
#include <atomic>
#include <functional>

class FileTransferOperation : public QThread {
    Q_OBJECT

  public:
    enum class DestinationKind { Directory, DirectPath };
    enum class ResultKind { Success, PartialSuccess, Failure, Canceled };
    struct FileSystemAdapter {
        std::function<bool(const QString &, const QString &)> trySourceRename;
        std::function<bool(const QString &)> removeSource;
    };
    FileTransferOperation(QStringList sources, QString destination, bool move,
                          QObject *parent = nullptr, FileSystemAdapter adapter = {},
                          DestinationKind destinationKind = DestinationKind::Directory);
    static QString checkDirectoryPaths(const QStringList &sources, const QString &destination);
    void cancel();
    void submitOverwriteAnswer(int answer);
    bool isCanceled() const { return canceled_; }
    ResultKind resultKind() const { return resultKind_; }
    bool hasRetainedOutput() const { return retainedOutput_; }

  signals:
    void currentFile(const QString &path);
    void advanced(qint64 bytes);
    void result(bool success, const QString &message, const QStringList &movedSources);
    void overwriteRequested(const OverwriteQuestion &question);
    void dismissOverwriteRequested();

  protected:
    void run() override;

  private:
    friend class QtFileTransferTransaction;
    struct Completion {
        ResultKind kind;
        QString message;
        QStringList movedSources{};
        bool retainedOutput = false;
    };
    int askOverwrite(const OverwriteQuestion &question);

    QStringList sources_;
    QString destination_;
    bool move_;
    DestinationKind destinationKind_;
    FileSystemAdapter adapter_;
    std::atomic_bool canceled_{false};
    std::atomic<ResultKind> resultKind_{ResultKind::Failure};
    std::atomic_bool retainedOutput_{false};
    QMutex overwriteMutex_;
    QWaitCondition overwriteCondition_;
    bool overwriteWaiting_ = false;
    int overwriteAnswer_ = -1;
};
