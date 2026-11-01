#pragma once
#include "fileops/overwrite/overwritequestion.h"
#include <QList>
#include <QString>
#include <QStringList>
#include <functional>
#include <variant>

class FileTransferOperation;

// Filesystem staging, publication and rollback for one joined transfer worker.
// The worker retains immutable inputs, cancellation and overwrite interaction.
class QtFileTransferTransaction final {
    struct OverwriteSource {
        QString Existing;
        QString DisplayExisting;
        std::variant<QString, OverwriteQuestion::File> Incoming;
    };
    static OverwriteQuestion snapshot(const OverwriteSource &source);
    struct Completed {
        QString source;
        QString target;
        QString backup;
        bool copiedMove = false;
        bool retainPublished = false;
        bool merged = false;
        QStringList writtenSources;
    };
    FileTransferOperation &worker_;
    QStringList prepareTargets(QString &error) const;
    void finish(QList<Completed> &completed, QString &error);
    void rollback(const QList<Completed> &completed);
    bool copyEntry(const QString &source, const QString &target, QString *error,
                   bool reportProgress = true);
    bool mergeEntry(const QString &source, const QString &stage, const QString &target,
                    const std::function<int(const OverwriteSource &)> &answer,
                    QStringList *writtenSources, QString *error);
    bool removeMergedSource(const QString &source, const QStringList &writtenSources,
                            QString *error);
    bool validateEntry(const QString &source, QString *error);
    static bool removeEntry(const QString &path);

  public:
    explicit QtFileTransferTransaction(FileTransferOperation &worker) : worker_(worker) {}
    void run();
};
