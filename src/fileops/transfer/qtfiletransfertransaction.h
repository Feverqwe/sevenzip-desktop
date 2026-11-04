#pragma once
#include "fileops/overwrite/overwritequestion.h"
#include "fileops/transfer/filetransferoperation.h"
#include <QList>
#include <QString>
#include <QStringList>
#include <optional>
#include <variant>

// Filesystem staging, publication and rollback for one joined transfer worker.
// The worker retains immutable inputs, cancellation and overwrite interaction.
class QtFileTransferTransaction final {
    struct OverwriteSource {
        QString Existing;
        QString DisplayExisting;
        std::variant<QString, OverwriteQuestion::File> Incoming;
    };
    static OverwriteQuestion snapshot(const OverwriteSource &source);
    enum class OverwriteDecision { Skip, Replace, Rename, Cancel };
    std::optional<OverwriteDecision> rememberedOverwrite_;
    OverwriteDecision decideOverwrite(const OverwriteSource &source);
    enum class SourceState { Intact, Renamed, RemovalStarted };
    enum class RollbackMode { RestoreSources, KeepMovedSources };
    struct TransferPath {
        QString source;
        QString target;
    };
    struct TransferEntry {
        QString source;
        QString target;
        QString backup;
        SourceState sourceState = SourceState::Intact;
        bool merged = false;
        QStringList writtenSources;
    };
    FileTransferOperation &worker_;
    QList<TransferPath> prepareTargets(QString &error) const;
    bool transferEntry(const TransferPath &path, QList<TransferEntry> &completed, QString &error);
    FileTransferOperation::Completion finish(QList<TransferEntry> &completed, QString &error);
    void rollbackEntry(const TransferEntry &entry, const QString &ownedPath, RollbackMode mode);
    void rollback(const QList<TransferEntry> &completed,
                  RollbackMode mode = RollbackMode::RestoreSources);
    void removeSources(QList<TransferEntry> &completed, QString &error);
    bool copyEntry(const QString &source, const QString &target, QString *error,
                   bool reportProgress = true);
    bool mergeEntry(const QString &source, const QString &stage, const QString &target,
                    QStringList *writtenSources, QString *error);
    bool removeMergedSource(const QString &source, const QStringList &writtenSources,
                            QString *error);
    bool validateEntry(const QString &source, QString *error);
    static bool removeEntry(const QString &path);

  public:
    explicit QtFileTransferTransaction(FileTransferOperation &worker) : worker_(worker) {}
    FileTransferOperation::Completion run();
};
