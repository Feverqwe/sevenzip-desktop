#include "fileops/transfer/qtfiletransfertransaction.h"
#include "7zip/Common/FilePathAutoRename.h"
#include "Common/StringConvert.h"
#include "Windows/ErrorMsg.h"
#include "Windows/FileFind.h"
#include "archive/callbacks/IFileExtractCallback.h"
#include "filemanager/providers/FSFolder.h"
#include "fileops/links/qtlinkservices.h"
#include "fileops/transfer/filetransferoperation.h"
#include "localization/qtlanguageservices.h"
#include "platform/temp/qtownedtempidentity.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QScopeGuard>
#include <QSet>
#include <QTemporaryDir>
#include <QUuid>
#include <algorithm>

namespace {
using NWindows::NFile::NFind::CFileInfo;

QString TargetPathError(const QFileInfo &source, const QFileInfo &target,
                        const QSet<QString> &targets, bool checkAliases = false) {
    if (targets.contains(target.filePath()))
        return QCoreApplication::translate("qtfiletransfertransaction",
                                           "Multiple items would use the same destination: %1")
            .arg(target.filePath());
    if (QDir::cleanPath(source.absoluteFilePath()) == QDir::cleanPath(target.absoluteFilePath()) ||
        (checkAliases && target.exists() &&
         source.canonicalFilePath() == target.canonicalFilePath()))
        return QCoreApplication::translate("qtfiletransfertransaction",
                                           "Source and destination are the same: %1")
            .arg(target.filePath());
    return {};
}

struct MergeMember {
    CFileInfo metadata;
    QString identity;

    bool matches(const MergeMember &other, bool renamed) const {
#ifndef _WIN32
        if (metadata.dev != other.metadata.dev || metadata.ino != other.metadata.ino)
            return false;
#endif
        return identity == other.identity && metadata.Size == other.metadata.Size &&
               metadata.GetWinAttrib() == other.metadata.GetWinAttrib() &&
               Compare_FiTime(&metadata.MTime, &other.metadata.MTime) == 0 &&
               (renamed || Compare_FiTime(&metadata.CTime, &other.metadata.CTime) == 0);
    }
    bool operator==(const MergeMember &other) const { return matches(other, false); }
};

using MergeSnapshot = QMap<QString, MergeMember>;

std::optional<MergeMember> captureMember(const QString &path) {
    const auto native = QDir::toNativeSeparators(path).toStdWString();
    MergeMember member;
    if (!member.metadata.Find(us2fs(native.c_str())))
        return std::nullopt;
    if (!member.metadata.IsOsSymLink()) {
        member.identity = QtOwnedTempIdentity(path);
        if (member.identity.isEmpty())
            return std::nullopt;
    }
    return member;
}

std::optional<MergeSnapshot> captureMergeTree(const QString &root,
                                              const std::atomic_bool &canceled) {
    MergeSnapshot snapshot;
    QStringList directories{QString()};
    while (!directories.isEmpty()) {
        const QString relative = directories.takeLast();
        const QDir directory(QDir(root).filePath(relative));
        if (canceled || !directory.isReadable() || QtIsFileSystemLink(directory.path()))
            return std::nullopt;
        const auto entries = directory.entryList(QDir::AllEntries | QDir::NoDotAndDotDot |
                                                 QDir::Hidden | QDir::System);
        for (const auto &name : entries) {
            const QString absolute = directory.filePath(name);
            if (canceled)
                return std::nullopt;
            auto member = captureMember(absolute);
            if (!member)
                return std::nullopt;
            const QString key = relative.isEmpty() ? name : relative + '/' + name;
            if (member->metadata.IsDir() && !member->metadata.IsOsSymLink())
                directories.append(key);
            snapshot.insert(key, std::move(*member));
        }
    }
    return snapshot;
}
} // namespace

struct QtFileTransferTransaction::TransferPath {
    QString source;
    QString target;
    MergeMember sourceMember;
};

OverwriteQuestion QtFileTransferTransaction::snapshot(const OverwriteSource &source) {
    OverwriteQuestion question;
    question.Existing = FilesystemOverwriteFile(source.Existing);
    if (!source.DisplayExisting.isEmpty())
        question.Existing.Path = source.DisplayExisting;
    if (const auto *path = std::get_if<QString>(&source.Incoming))
        question.Incoming = FilesystemOverwriteFile(*path);
    else {
        question.Incoming = std::get<OverwriteQuestion::File>(source.Incoming);
        question.Incoming.Directory = QFileInfo(question.Incoming.Path).isDir();
    }
    return question;
}

QtFileTransferTransaction::OverwriteDecision
QtFileTransferTransaction::decideOverwrite(const OverwriteSource &source) {
    if (rememberedOverwrite_)
        return *rememberedOverwrite_;
    auto question = snapshot(source);
    question.MultipleConflictsPossible =
        worker_.sources_.size() != 1 || !QFileInfo(worker_.sources_.first()).isFile();
    switch (worker_.askOverwrite(question)) {
    case NOverwriteAnswer::kNo:
        skipped_ = true;
        return OverwriteDecision::Skip;
    case NOverwriteAnswer::kYes:
        return OverwriteDecision::Replace;
    case NOverwriteAnswer::kAutoRenameOnce:
        return OverwriteDecision::Rename;
    case NOverwriteAnswer::kNoToAll:
        skipped_ = true;
        rememberedOverwrite_ = OverwriteDecision::Skip;
        break;
    case NOverwriteAnswer::kYesToAll:
        rememberedOverwrite_ = OverwriteDecision::Replace;
        break;
    case NOverwriteAnswer::kAutoRename:
        rememberedOverwrite_ = OverwriteDecision::Rename;
        break;
    default:
        return OverwriteDecision::Cancel;
    }
    return *rememberedOverwrite_;
}

bool QtFileTransferTransaction::removeEntry(const QString &path) {
    return QtRemoveFileSystemEntry(path);
}

QString FileTransferOperation::checkDirectoryPaths(const QStringList &sources,
                                                   const QString &destination) {
    const QDir directory(destination);
    QSet<QString> targets;
    for (const auto &path : sources) {
        const QFileInfo source(path);
        if (!source.exists() && !source.isSymLink() && !source.isJunction())
            return QCoreApplication::translate("qtfiletransfertransaction",
                                               "Source unavailable: %1")
                .arg(path);
        const QString target = directory.filePath(source.fileName());
        if (const QString error = TargetPathError(source, QFileInfo(target), targets);
            !error.isEmpty())
            return error;
        targets.insert(target);
    }
    return {};
}

QList<QtFileTransferTransaction::TransferPath>
QtFileTransferTransaction::prepareTargets(QString &error) {
    const bool directPath =
        worker_.destinationKind_ == FileTransferOperation::DestinationKind::DirectPath;
    destinationDirectory_ =
        directPath ? QFileInfo(worker_.destination_).absolutePath() : worker_.destination_;
    const QFileInfo destinationInfo(destinationDirectory_);
    destinationIdentity_ = QtOwnedTempIdentity(destinationDirectory_);
    const QString canonicalDestination = destinationInfo.canonicalFilePath();
    QList<TransferPath> targets;
    if (!destinationInfo.isDir() || destinationIdentity_.isEmpty() || worker_.sources_.isEmpty() ||
        (directPath && worker_.sources_.size() != 1)) {
        error = QCoreApplication::translate("qtfiletransfertransaction",
                                            "The destination folder is unavailable.");
        return {};
    }
    for (const QString &source : worker_.sources_) {
        const QFileInfo info(source);
        const bool sourceLink = QtIsFileSystemLink(source);
        auto member =
            sourceLink || info.isFile() || info.isDir() ? captureMember(source) : std::nullopt;
        if (!member) {
            error =
                QCoreApplication::translate("qtfiletransfertransaction", "Source unavailable: %1")
                    .arg(source);
            return {};
        }
        const QString target = directPath ? worker_.destination_
                                          : QDir(destinationDirectory_).filePath(info.fileName());
        const QFileInfo targetInfo(target);
        error = TargetPathError(info, targetInfo, plannedTargets_, !sourceLink);
        if (!error.isEmpty())
            return {};
        if (!sourceLink && info.isDir() &&
            NFsFolder::IsDestChild(info.canonicalFilePath(), canonicalDestination)) {
            error = QCoreApplication::translate("qtfiletransfertransaction",
                                                "Destination is inside the source folder: %1")
                        .arg(target);
            return {};
        }
        if (QtIsFileSystemLink(target)) {
            error = QCoreApplication::translate("qtfiletransfertransaction",
                                                "Destination is a symbolic link: %1")
                        .arg(target);
            return {};
        }
        plannedTargets_.insert(target);
        targets.append({source, target, std::move(*member)});
    }
    return targets;
}

bool QtFileTransferTransaction::destinationMatches() const {
    return !destinationIdentity_.isEmpty() &&
           QtOwnedTempIdentity(destinationDirectory_) == destinationIdentity_;
}

QString QtFileTransferTransaction::destinationChangedMessage() const {
    return QCoreApplication::translate("qtfiletransfertransaction",
                                       "Destination folder changed during transfer: %1")
        .arg(destinationDirectory_);
}

void QtFileTransferTransaction::discardBackup(const TransferEntry &entry) {
    if (entry.backup.isEmpty())
        return;
    if (!destinationMatches()) {
        rollbackErrors_.append(destinationChangedMessage());
        return;
    }
    const std::atomic_bool keepChecking{false};
    if (!entry.backupMatches(entry.backup, keepChecking)) {
        rollbackErrors_.append(
            QCoreApplication::translate("qtfiletransfertransaction",
                                        "Backup changed during transfer and was kept: %1")
                .arg(entry.backup));
    } else if (!removeEntry(entry.backup)) {
        rollbackErrors_.append(
            QCoreApplication::translate("qtfiletransfertransaction", "Could not remove backup: %1")
                .arg(entry.backup));
    }
}

void QtFileTransferTransaction::rollbackEntry(const TransferEntry &entry, RollbackMode mode) {
    if (entry.ownedPath.isEmpty())
        return;
    if (!destinationMatches()) {
        rollbackErrors_.append(QCoreApplication::translate(
                                   "qtfiletransfertransaction",
                                   "Could not roll back %1. Check the original destination folder.")
                                   .arg(entry.target));
        return;
    }
    if (mode == RollbackMode::KeepMovedSources && entry.sourceState != SourceState::Intact) {
        discardBackup(entry);
        return;
    }
    if (entry.sourceState == SourceState::Renamed) {
        if (!QtRenameFileSystemEntryWithoutReplacing(entry.ownedPath, entry.source))
            rollbackErrors_.append(QCoreApplication::translate("qtfiletransfertransaction",
                                                               "Could not restore %1 from %2.")
                                       .arg(entry.source, entry.ownedPath));
    } else if (!removeEntry(entry.ownedPath) &&
               (QFileInfo::exists(entry.ownedPath) || QtIsFileSystemLink(entry.ownedPath))) {
        rollbackErrors_.append(QCoreApplication::translate("qtfiletransfertransaction",
                                                           "Could not remove transfer output: %1")
                                   .arg(entry.ownedPath));
    }
    if (!entry.backup.isEmpty() &&
        !QtRenameFileSystemEntryWithoutReplacing(entry.backup, entry.target))
        rollbackErrors_.append(QCoreApplication::translate("qtfiletransfertransaction",
                                                           "Could not restore %1 from backup %2.")
                                   .arg(entry.target, entry.backup));
}

void QtFileTransferTransaction::rollback(const QList<TransferEntry> &completed, RollbackMode mode) {
    for (qsizetype i = completed.size(); i > 0; --i)
        rollbackEntry(completed[i - 1], mode);
}

void QtFileTransferTransaction::removeSources(QList<TransferEntry> &completed, QString &error) {
    for (TransferEntry &entry : completed) {
        if (entry.sourceState != SourceState::Intact)
            continue;
        if (!destinationMatches()) {
            error = destinationChangedMessage();
            return;
        }
        if (worker_.canceled_) {
            error = QCoreApplication::translate("qtfiletransfertransaction", "Operation canceled.");
            return;
        }
        if (!entry.sourceMatches(entry.source, false)) {
            entry.sourceState = SourceState::Changed;
            error = QCoreApplication::translate("qtfiletransfertransaction",
                                                "Source changed after copying and was kept: %1")
                        .arg(entry.source);
            return;
        }
        if (!removeStagedSource(entry, error))
            return;
    }
}

bool QtFileTransferTransaction::removeStagedSource(TransferEntry &entry, QString &error) {
    const QFileInfo source(entry.source);
    QTemporaryDir holding(source.dir().filePath(".sevenzip-remove-XXXXXX"));
    const QString staged = holding.filePath(source.fileName());
    if (!holding.isValid() || !QtRenameFileSystemEntryWithoutReplacing(entry.source, staged)) {
        if (!entry.sourceMatches(entry.source, false))
            entry.sourceState = SourceState::Changed;
        error = QCoreApplication::translate("qtfiletransfertransaction",
                                            "Could not delete source after copying: %1")
                    .arg(entry.source);
        return false;
    }
    entry.sourceState = SourceState::Changed;
    holding.setAutoRemove(false);
    const auto restore = qScopeGuard([&] {
        if ((QFileInfo::exists(staged) || QtIsFileSystemLink(staged)) &&
            !QtRenameFileSystemEntryWithoutReplacing(staged, entry.source))
            rollbackErrors_.append(QCoreApplication::translate("qtfiletransfertransaction",
                                                               "Could not restore %1 from %2.")
                                       .arg(entry.source, staged));
        QDir().rmdir(holding.path());
    });
    if (!entry.sourceMatches(staged, true)) {
        error = QCoreApplication::translate("qtfiletransfertransaction",
                                            "Source changed after copying and was kept: %1")
                    .arg(entry.source);
        return false;
    }
    if (worker_.canceled_) {
        error = QCoreApplication::translate("qtfiletransfertransaction", "Operation canceled.");
        return false;
    }
    // Recursive deletion can fail after consuming some entries. Keep the
    // published copy and restore anything still in the holding directory.
    entry.sourceState = SourceState::RemovalStarted;
    bool removed = false;
    bool rootRemoved = false;
    try {
        if (entry.merged) {
            QStringList stagedFiles;
            const QDir original(entry.source);
            for (const auto &path : entry.writtenSources)
                stagedFiles.append(QDir(staged).filePath(original.relativeFilePath(path)));
            removed = removeMergedSource(staged, stagedFiles, &error, rootRemoved);
        } else {
            rootRemoved = removed = worker_.adapter_.removeSource(staged);
        }
    } catch (...) {
        error = QString::fromWCharArray(NWindows::NError::MyFormatMessage(E_FAIL));
    }
    if (rootRemoved)
        entry.sourceState = SourceState::Removed;
    if (!removed && error.isEmpty())
        error = QCoreApplication::translate("qtfiletransfertransaction",
                                            "Could not delete source after copying: %1")
                    .arg(entry.source);
    return removed;
}

QStringList QtFileTransferTransaction::movedSources(const QList<TransferEntry> &completed) {
    QStringList moved;
    for (const auto &entry : completed)
        if (entry.sourceState == SourceState::Renamed || entry.sourceState == SourceState::Removed)
            moved.append(entry.source);
    return moved;
}

void QtFileTransferTransaction::rememberFailure(const QString &error) noexcept {
    // Cleanup can outlive a later Stop request; retain the cause that started it.
    if (!error.isEmpty() && !failureKind_)
        failureKind_ = worker_.canceled_ ? FileTransferOperation::ResultKind::Canceled
                                         : FileTransferOperation::ResultKind::Failure;
}

FileTransferOperation::Completion QtFileTransferTransaction::finish(QList<TransferEntry> &completed,
                                                                    QString &error) {
    if (error.isEmpty() && !destinationMatches())
        error = destinationChangedMessage();
    if (!error.isEmpty()) {
        rememberFailure(error);
        rollback(completed);
        if (!rollbackErrors_.isEmpty())
            return {FileTransferOperation::ResultKind::Failure,
                    error + '\n' + rollbackErrors_.join('\n'),
                    {},
                    true};
        return {*failureKind_, error};
    }
    if (worker_.move_) {
        removeSources(completed, error);
        if (!error.isEmpty()) {
            rollback(completed, RollbackMode::KeepMovedSources);
            const bool retained =
                std::any_of(completed.cbegin(), completed.cend(), [](const TransferEntry &entry) {
                    return !entry.ownedPath.isEmpty() && entry.sourceState != SourceState::Intact;
                });
            const bool changesRemain = retained || !rollbackErrors_.isEmpty();
            if (changesRemain)
                error += QCoreApplication::translate("qtfiletransfertransaction",
                                                     " Some items may have been moved.");
            if (!rollbackErrors_.isEmpty())
                error += '\n' + rollbackErrors_.join('\n');
            return {FileTransferOperation::ResultKind::Failure, error, movedSources(completed),
                    changesRemain};
        }
    }
    for (const TransferEntry &entry : completed)
        discardBackup(entry);
    QString message;
    const bool partial =
        skipped_ ||
        (worker_.move_ &&
         std::any_of(completed.cbegin(), completed.cend(), [](const TransferEntry &entry) {
             return entry.merged && entry.sourceState != SourceState::Removed;
         }));
    if (skipped_ && completed.isEmpty())
        message =
            QCoreApplication::translate("qtfiletransfertransaction", "All items were skipped.");
    else if (!worker_.move_ && partial)
        message =
            QCoreApplication::translate("qtfiletransfertransaction", "Some items were skipped.");
    else if (!worker_.move_)
        message = QCoreApplication::translate("qtfiletransfertransaction", "Items copied.");
    else if (partial)
        message = QCoreApplication::translate("qtfiletransfertransaction",
                                              "Some files skipped; sources retained.");
    else
        message = QCoreApplication::translate("qtfiletransfertransaction", "Items moved.");
    if (!rollbackErrors_.isEmpty())
        message += '\n' + rollbackErrors_.join('\n');
    return {partial || !rollbackErrors_.isEmpty()
                ? FileTransferOperation::ResultKind::PartialSuccess
                : FileTransferOperation::ResultKind::Success,
            message, movedSources(completed), !completed.isEmpty() || !rollbackErrors_.isEmpty()};
}

QString
QtFileTransferTransaction::availableName(const QString &target,
                                         const std::function<bool(const QString &)> &reserved) {
    FString renamed = us2fs(target.toStdWString().c_str());
    if (!AutoRenamePath(renamed))
        return {};
    const QString candidate = QString::fromWCharArray(fs2us(renamed));
    if (!reserved(candidate))
        return candidate;
    const auto dot = target.lastIndexOf(u'.');
    const auto extension = dot > target.lastIndexOf(u'/') + 1 ? target.mid(dot) : QString();
    const auto stem = target.chopped(extension.size()) + u'_';
    auto number =
        candidate.mid(stem.size(), candidate.size() - stem.size() - extension.size()).toUInt();
    while (++number < (1u << 30)) {
        const QString next = stem + QString::number(number) + extension;
        if (!reserved(next) && !QFileInfo::exists(next) && !QtIsFileSystemLink(next))
            return next;
    }
    return {};
}

bool QtFileTransferTransaction::transferEntry(const TransferPath &path,
                                              QList<TransferEntry> &completed, QString &error) {
    if (!destinationMatches()) {
        error = destinationChangedMessage();
        return false;
    }
    const QString &source = path.source;
    QString target = path.target;
    bool mayReplace = false;
    const QFileInfo sourceInfo(source);
    const auto &sourceMember = path.sourceMember;
    const auto sourceChanged = [&] {
        error = QCoreApplication::translate("qtfiletransfertransaction",
                                            "Source changed during transfer: %1")
                    .arg(source);
        return false;
    };
    if (captureMember(source) != sourceMember)
        return sourceChanged();
    const QFileInfo existingInfo(target);
    const QString existingIdentity =
        existingInfo.exists() ? QtOwnedTempIdentity(target) : QString();
    if (!existingIdentity.isEmpty() &&
        std::any_of(completed.cbegin(), completed.cend(), [&](const TransferEntry &entry) {
            return entry.target.compare(target, Qt::CaseInsensitive) == 0 &&
                   QtOwnedTempIdentity(entry.target) == existingIdentity;
        })) {
        error = QCoreApplication::translate("qtfiletransfertransaction",
                                            "Multiple items would use the same destination: %1")
                    .arg(target);
        return false;
    }
    const auto existingModified = existingInfo.lastModified();
    const qint64 existingSize = existingInfo.size();
    const auto matchesExisting = [existingIdentity, existingModified,
                                  existingSize](const QString &candidate) {
        const QFileInfo info(candidate);
        return !existingIdentity.isEmpty() && QtOwnedTempIdentity(candidate) == existingIdentity &&
               info.lastModified() == existingModified && info.size() == existingSize;
    };
    const auto destinationChanged = [&] {
        error = QCoreApplication::translate("qtfiletransfertransaction",
                                            "Destination changed during transfer: %1")
                    .arg(target);
        return false;
    };
    const bool merge = existingInfo.isDir() && sourceInfo.isDir() && !QtIsFileSystemLink(source);
    if (existingInfo.exists()) {
        if (existingInfo.isDir() != sourceInfo.isDir()) {
            error = QCoreApplication::translate(
                        "qtfiletransfertransaction",
                        "Cannot replace a file with a folder or a folder with a file: %1")
                        .arg(target);
            return false;
        }
        if (merge) {
            mayReplace = true;
        } else {
            switch (decideOverwrite({target, target, source})) {
            case OverwriteDecision::Skip:
                return true;
            case OverwriteDecision::Replace:
                mayReplace = true;
                break;
            case OverwriteDecision::Rename: {
                const QString candidate = availableName(target, [this](const QString &name) {
#ifdef Q_OS_WIN
                    return std::any_of(plannedTargets_.cbegin(), plannedTargets_.cend(),
                                       [&name](const QString &planned) {
                                           return planned.compare(name, Qt::CaseInsensitive) == 0;
                                       });
#else
                    return plannedTargets_.contains(name);
#endif
                });
                if (candidate.isEmpty()) {
                    error = QCoreApplication::translate("qtfiletransfertransaction",
                                                        "Could not find an available name: %1")
                                .arg(target);
                    break;
                }
                target = candidate;
                break;
            }
            case OverwriteDecision::Cancel:
                error =
                    QCoreApplication::translate("qtfiletransfertransaction", "Operation canceled.");
                break;
            }
        }
        if (!error.isEmpty())
            return false;
    }
    if (mayReplace && !matchesExisting(target))
        return destinationChanged();
    const auto mergeSnapshot = merge ? captureMergeTree(target, worker_.canceled_) : std::nullopt;
    if (merge && !mergeSnapshot)
        return destinationChanged();
    const QString suffix =
        QStringLiteral(".sevenzip-%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    const QString stage = target + suffix;
    TransferEntry entry{source, target, {}, SourceState::Intact, merge, {}};
    auto rollbackItem = qScopeGuard([&] {
        rememberFailure(error);
        rollbackEntry(entry, RollbackMode::RestoreSources);
    });
    emit worker_.currentFile(source);
    if (!destinationMatches()) {
        error = destinationChangedMessage();
        return false;
    }
    if (captureMember(source) != sourceMember)
        return sourceChanged();
    if (worker_.move_ && !validateEntry(source, &error))
        return false;
    entry.ownedPath = stage;
    if (worker_.move_ && !merge && worker_.adapter_.trySourceRename(source, stage))
        entry.sourceState = SourceState::Renamed;
    if (entry.sourceState == SourceState::Intact && (worker_.move_ || merge)) {
        const bool directory =
            sourceMember.metadata.IsDir() && !sourceMember.metadata.IsOsSymLink();
        const auto tree = directory ? captureMergeTree(source, worker_.canceled_) : std::nullopt;
        if (directory && !tree) {
            error =
                QCoreApplication::translate("qtfiletransfertransaction", "Source unavailable: %1")
                    .arg(source);
            return false;
        }
        entry.sourceMatches = [this, sourceMember, tree](const QString &path, bool renamed) {
            const auto current = captureMember(path);
            return current && current->matches(sourceMember, renamed) &&
                   (!tree || captureMergeTree(path, worker_.canceled_) == tree);
        };
    }
    const bool staged =
        merge ? (copyEntry(target, stage, &error, false) &&
                 mergeEntry(source, stage, target, &entry.writtenSources, &error))
              : (entry.sourceState == SourceState::Renamed || copyEntry(source, stage, &error));
    if (!staged || !error.isEmpty()) {
        if (error.isEmpty())
            error =
                QCoreApplication::translate("qtfiletransfertransaction", "Could not move item: %1")
                    .arg(source);
        return false;
    }
    if (worker_.canceled_) {
        error = QCoreApplication::translate("qtfiletransfertransaction", "Operation canceled.");
        return false;
    }
    if (!destinationMatches()) {
        error = destinationChangedMessage();
        return false;
    }
    if (entry.sourceState == SourceState::Renamed) {
        const auto moved = captureMember(stage);
        if (!moved || !moved->matches(sourceMember, true))
            return sourceChanged();
    } else if (entry.sourceMatches ? !entry.sourceMatches(source, false)
                                   : captureMember(source) != sourceMember) {
        return sourceChanged();
    }
    if (mayReplace) {
        if (!matchesExisting(target))
            return destinationChanged();
        // Keep the old entry until every source has published successfully.
        const QString backupPath = target + suffix + QStringLiteral("-backup");
        if (!QtRenameFileSystemEntryWithoutReplacing(target, backupPath)) {
            error = QCoreApplication::translate("qtfiletransfertransaction",
                                                "Could not preserve the previous item: %1")
                        .arg(target);
            return false;
        }
        entry.backup = backupPath;
        entry.backupMatches = [matchesExisting, mergeSnapshot](const QString &candidate,
                                                               const std::atomic_bool &canceled) {
            return matchesExisting(candidate) &&
                   (!mergeSnapshot || captureMergeTree(candidate, canceled) == mergeSnapshot);
        };
        if (!entry.backupMatches(backupPath, worker_.canceled_))
            return destinationChanged();
    } else if (QFileInfo::exists(target) || QtIsFileSystemLink(target)) {
        return destinationChanged();
    }
    if (!QtRenameFileSystemEntryWithoutReplacing(stage, target)) {
        error = QCoreApplication::translate("qtfiletransfertransaction",
                                            "Could not finish transfer: %1")
                    .arg(target);
        return false;
    }
    entry.ownedPath = target;
    completed.append(entry);
    rollbackItem.dismiss();
    return true;
}

FileTransferOperation::Completion QtFileTransferTransaction::run() {
    rememberedOverwrite_.reset();
    failureKind_.reset();
    rollbackErrors_.clear();
    QString error;
    const auto targets = prepareTargets(error);
    QList<TransferEntry> completed;
    auto rollbackBatch = qScopeGuard([&] { rollback(completed); });
    try {
        for (const auto &path : targets) {
            if (worker_.canceled_) {
                error =
                    QCoreApplication::translate("qtfiletransfertransaction", "Operation canceled.");
                break;
            }
            if (!transferEntry(path, completed, error))
                break;
        }
    } catch (...) {
        error = QString::fromWCharArray(NWindows::NError::MyFormatMessage(E_FAIL));
        failureKind_ = FileTransferOperation::ResultKind::Failure;
    }
    rollbackBatch.dismiss();
    return finish(completed, error);
}
