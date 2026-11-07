#include "fileops/transfer/qtfiletransfertransaction.h"
#include "7zip/Common/FilePathAutoRename.h"
#include "Common/StringConvert.h"
#include "Windows/ErrorMsg.h"
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
#include <QScopeGuard>
#include <QUuid>
#include <algorithm>

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

QList<QtFileTransferTransaction::TransferPath>
QtFileTransferTransaction::prepareTargets(QString &error) const {
    const bool directPath =
        worker_.destinationKind_ == FileTransferOperation::DestinationKind::DirectPath;
    const QString destinationDirectory =
        directPath ? QFileInfo(worker_.destination_).absolutePath() : worker_.destination_;
    const QFileInfo destinationInfo(destinationDirectory);
    const QString canonicalDestination = destinationInfo.canonicalFilePath();
    QList<TransferPath> targets;
    if (!destinationInfo.isDir() || destinationInfo.isSymLink() || worker_.sources_.isEmpty() ||
        (directPath && worker_.sources_.size() != 1)) {
        error = QCoreApplication::translate("qtfiletransfertransaction",
                                            "The destination folder is unavailable.");
        return {};
    }
    for (const QString &source : worker_.sources_) {
        const QFileInfo info(source);
        const QString target = directPath ? worker_.destination_
                                          : QDir(destinationDirectory).filePath(info.fileName());
        const QFileInfo targetInfo(target);
        const bool sourceLink = QtIsFileSystemLink(source);
        const bool samePath = QDir::cleanPath(info.absoluteFilePath()) ==
                                  QDir::cleanPath(targetInfo.absoluteFilePath()) ||
                              (!sourceLink && targetInfo.exists() &&
                               info.canonicalFilePath() == targetInfo.canonicalFilePath());
        if ((!sourceLink && (!info.exists() || (!info.isFile() && !info.isDir()))) ||
            QtIsFileSystemLink(target) || samePath ||
            std::any_of(targets.cbegin(), targets.cend(),
                        [&target](const TransferPath &path) { return path.target == target; }) ||
            (!sourceLink && info.isDir() &&
             NFsFolder::IsDestChild(info.canonicalFilePath(), canonicalDestination))) {
            error = QCoreApplication::translate(
                "qtfiletransfertransaction",
                "Source or destination unavailable, or the name is already in use.");
            return {};
        }
        targets.append({source, target});
    }
    return targets;
}

void QtFileTransferTransaction::rollbackEntry(const TransferEntry &entry, RollbackMode mode) {
    if (entry.ownedPath.isEmpty())
        return;
    if (mode == RollbackMode::KeepMovedSources && entry.sourceState != SourceState::Intact) {
        if (!entry.backup.isEmpty() && !removeEntry(entry.backup))
            rollbackErrors_.append(QCoreApplication::translate("qtfiletransfertransaction",
                                                               "Could not remove backup: %1")
                                       .arg(entry.backup));
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
        if (worker_.canceled_) {
            error = QCoreApplication::translate("qtfiletransfertransaction", "Operation canceled.");
            return;
        }
        // An attempted recursive removal can already have consumed bytes.
        // From here on rollback must retain the published copy.
        entry.sourceState = SourceState::RemovalStarted;
        bool removed = false;
        bool rootRemoved = false;
        try {
            if (entry.merged)
                removed =
                    removeMergedSource(entry.source, entry.writtenSources, &error, rootRemoved);
            else
                rootRemoved = removed = worker_.adapter_.removeSource(entry.source);
        } catch (...) {
            error = QString::fromWCharArray(NWindows::NError::MyFormatMessage(E_FAIL));
        }
        if (rootRemoved)
            entry.sourceState = SourceState::Removed;
        if (!removed) {
            if (error.isEmpty())
                error = QCoreApplication::translate("qtfiletransfertransaction",
                                                    "Could not delete source after copying: %1")
                            .arg(entry.source);
            return;
        }
    }
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
    if (!error.isEmpty()) {
        rememberFailure(error);
        rollback(completed);
        if (!rollbackErrors_.isEmpty())
            return {FileTransferOperation::ResultKind::Failure,
                    error + '\n' + rollbackErrors_.join('\n')};
        return {*failureKind_, error};
    }
    if (worker_.move_) {
        removeSources(completed, error);
        if (!error.isEmpty()) {
            rollback(completed, RollbackMode::KeepMovedSources);
            return {FileTransferOperation::ResultKind::Failure,
                    error +
                        QCoreApplication::translate("qtfiletransfertransaction",
                                                    " Some items may have been moved.") +
                        (rollbackErrors_.isEmpty() ? QString() : '\n' + rollbackErrors_.join('\n')),
                    movedSources(completed)};
        }
    }
    for (const TransferEntry &entry : completed)
        if (!entry.backup.isEmpty())
            removeEntry(entry.backup);
    QString message;
    const bool partial =
        skipped_ ||
        (worker_.move_ &&
         std::any_of(completed.cbegin(), completed.cend(), [](const TransferEntry &entry) {
             return entry.merged && entry.sourceState != SourceState::Removed;
         }));
    if (!worker_.move_ && partial)
        message =
            QCoreApplication::translate("qtfiletransfertransaction", "Some items were skipped.");
    else if (!worker_.move_)
        message = QCoreApplication::translate("qtfiletransfertransaction", "Items copied.");
    else if (partial)
        message = QCoreApplication::translate("qtfiletransfertransaction",
                                              "Some files skipped; sources retained.");
    else
        message = QCoreApplication::translate("qtfiletransfertransaction", "Items moved.");
    return {partial ? FileTransferOperation::ResultKind::PartialSuccess
                    : FileTransferOperation::ResultKind::Success,
            message, movedSources(completed)};
}

bool QtFileTransferTransaction::transferEntry(const TransferPath &path,
                                              QList<TransferEntry> &completed, QString &error) {
    const QString &source = path.source;
    QString target = path.target;
    bool mayReplace = false;
    const QFileInfo sourceInfo(source);
    const QFileInfo existingInfo(target);
    const QString existingIdentity =
        existingInfo.exists() ? QtOwnedTempIdentity(target) : QString();
    const auto existingModified = existingInfo.lastModified();
    const qint64 existingSize = existingInfo.size();
    const auto matchesExisting = [&](const QString &candidate) {
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
                FString renamed = us2fs(target.toStdWString().c_str());
                if (!AutoRenamePath(renamed)) {
                    error = QCoreApplication::translate("qtfiletransfertransaction",
                                                        "Could not find an available name: %1")
                                .arg(target);
                    break;
                }
                target = QString::fromWCharArray(fs2us(renamed));
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
    const QString suffix =
        QStringLiteral(".sevenzip-%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    const QString stage = target + suffix;
    TransferEntry entry{source, target, {}, SourceState::Intact, merge, {}};
    auto rollbackItem = qScopeGuard([&] {
        rememberFailure(error);
        rollbackEntry(entry, RollbackMode::RestoreSources);
    });
    emit worker_.currentFile(source);
    if (worker_.move_ && !validateEntry(source, &error))
        return false;
    entry.ownedPath = stage;
    if (worker_.move_ && !merge && worker_.adapter_.trySourceRename(source, stage))
        entry.sourceState = SourceState::Renamed;
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
        if (!matchesExisting(backupPath))
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
