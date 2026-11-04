#include "fileops/transfer/qtfiletransfertransaction.h"
#include "7zip/Common/FilePathAutoRename.h"
#include "Common/StringConvert.h"
#include "Windows/ErrorMsg.h"
#include "archive/callbacks/IFileExtractCallback.h"
#include "filemanager/providers/FSFolder.h"
#include "fileops/links/qtlinkservices.h"
#include "fileops/transfer/filetransferoperation.h"
#include "localization/qtlanguageservices.h"

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
    switch (worker_.askOverwrite(snapshot(source))) {
    case NOverwriteAnswer::kNo:
        return OverwriteDecision::Skip;
    case NOverwriteAnswer::kYes:
        return OverwriteDecision::Replace;
    case NOverwriteAnswer::kNoToAll:
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
        (directPath && worker_.sources_.size() != 1))
        error = QCoreApplication::translate("qtfiletransfertransaction",
                                            "The destination folder is unavailable.");
    for (const QString &source : worker_.sources_) {
        if (!error.isEmpty())
            break;
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
             NFsFolder::IsDestChild(info.canonicalFilePath(), canonicalDestination)))
            error = QCoreApplication::translate(
                "qtfiletransfertransaction",
                "Source or destination unavailable, or the name is already in use.");
        targets.append({source, target});
    }
    return targets;
}

void QtFileTransferTransaction::rollbackEntry(const TransferEntry &entry, const QString &ownedPath,
                                              RollbackMode mode) {
    if (ownedPath.isEmpty())
        return;
    if (mode == RollbackMode::KeepMovedSources && entry.sourceState != SourceState::Intact) {
        if (!entry.backup.isEmpty())
            removeEntry(entry.backup);
        return;
    }
    if (entry.sourceState == SourceState::Renamed)
        QDir().rename(ownedPath, entry.source);
    else
        removeEntry(ownedPath);
    if (!entry.backup.isEmpty())
        QDir().rename(entry.backup, entry.target);
}

void QtFileTransferTransaction::rollback(const QList<TransferEntry> &completed, RollbackMode mode) {
    for (qsizetype i = completed.size(); i > 0; --i)
        rollbackEntry(completed[i - 1], completed[i - 1].target, mode);
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
        try {
            removed = entry.merged ? removeMergedSource(entry.source, entry.writtenSources, &error)
                                   : worker_.adapter_.removeSource(entry.source);
        } catch (...) {
            error = QString::fromWCharArray(NWindows::NError::MyFormatMessage(E_FAIL));
        }
        if (!removed) {
            if (error.isEmpty())
                error = QCoreApplication::translate("qtfiletransfertransaction",
                                                    "Could not delete source after copying: %1")
                            .arg(entry.source);
            return;
        }
    }
}

FileTransferOperation::Completion QtFileTransferTransaction::finish(QList<TransferEntry> &completed,
                                                                    QString &error) {
    if (!error.isEmpty()) {
        rollback(completed);
        return {worker_.canceled_ ? FileTransferOperation::ResultKind::Canceled
                                  : FileTransferOperation::ResultKind::Failure,
                error};
    }
    if (worker_.move_) {
        removeSources(completed, error);
        if (!error.isEmpty()) {
            rollback(completed, RollbackMode::KeepMovedSources);
            return {FileTransferOperation::ResultKind::Failure,
                    error + QCoreApplication::translate("qtfiletransfertransaction",
                                                        " Some items may have been moved.")};
        }
    }
    for (const TransferEntry &entry : completed)
        if (!entry.backup.isEmpty())
            removeEntry(entry.backup);
    QString message;
    if (!worker_.move_)
        message = QCoreApplication::translate("qtfiletransfertransaction", "Items copied.");
    else if (std::any_of(completed.cbegin(), completed.cend(), [](const TransferEntry &entry) {
                 return entry.merged && QFileInfo::exists(entry.source);
             }))
        message = QCoreApplication::translate("qtfiletransfertransaction",
                                              "Some files skipped; sources retained.");
    else
        message = QCoreApplication::translate("qtfiletransfertransaction", "Items moved.");
    return {FileTransferOperation::ResultKind::Success, message};
}

bool QtFileTransferTransaction::transferEntry(const TransferPath &path,
                                              QList<TransferEntry> &completed, QString &error) {
    const QString &source = path.source;
    QString target = path.target;
    bool mayReplace = false;
    const QFileInfo sourceInfo(source);
    const QFileInfo existingInfo(target);
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
    const QString suffix =
        QStringLiteral(".sevenzip-%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    const QString stage = target + suffix;
    TransferEntry entry{source, target, {}, SourceState::Intact, merge, {}};
    QString ownedPath;
    auto rollbackItem =
        qScopeGuard([&] { rollbackEntry(entry, ownedPath, RollbackMode::RestoreSources); });
    emit worker_.currentFile(source);
    const bool valid = !worker_.move_ || validateEntry(source, &error);
    if (valid)
        ownedPath = stage;
    if (valid && worker_.move_ && !merge && worker_.adapter_.trySourceRename(source, stage))
        entry.sourceState = SourceState::Renamed;
    const bool staged =
        valid &&
        (merge ? (copyEntry(target, stage, &error, false) &&
                  mergeEntry(source, stage, target, &entry.writtenSources, &error))
               : (entry.sourceState == SourceState::Renamed || copyEntry(source, stage, &error)));
    if (!staged && error.isEmpty())
        error = QCoreApplication::translate("qtfiletransfertransaction", "Could not move item: %1")
                    .arg(source);
    if (error.isEmpty() && worker_.canceled_)
        error = QCoreApplication::translate("qtfiletransfertransaction", "Operation canceled.");
    if (error.isEmpty() && QFileInfo(target).isSymLink())
        error = QCoreApplication::translate("qtfiletransfertransaction",
                                            "Destination changed during transfer: %1")
                    .arg(target);
    if (error.isEmpty() && QFileInfo::exists(target) && !mayReplace)
        error = QCoreApplication::translate("qtfiletransfertransaction",
                                            "Destination changed during transfer: %1")
                    .arg(target);
    if (error.isEmpty() && QFileInfo::exists(target)) {
        // Keep the old entry until every source has published successfully.
        const QString backupPath = target + suffix + QStringLiteral("-backup");
        if (QDir().rename(target, backupPath))
            entry.backup = backupPath;
        else
            error = QCoreApplication::translate("qtfiletransfertransaction",
                                                "Could not preserve the previous item: %1")
                        .arg(target);
    }
    if (error.isEmpty()) {
        if (QDir().rename(stage, target))
            ownedPath = target;
        else
            error = QCoreApplication::translate("qtfiletransfertransaction",
                                                "Could not finish transfer: %1")
                        .arg(target);
    }
    if (!error.isEmpty())
        return false;
    completed.append(entry);
    rollbackItem.dismiss();
    return true;
}

FileTransferOperation::Completion QtFileTransferTransaction::run() {
    rememberedOverwrite_.reset();
    QString error;
    const auto targets = prepareTargets(error);
    QList<TransferEntry> completed;
    auto rollbackBatch = qScopeGuard([&] { rollback(completed); });
    for (const auto &path : targets) {
        if (!error.isEmpty())
            break;
        if (worker_.canceled_) {
            error = QCoreApplication::translate("qtfiletransfertransaction", "Operation canceled.");
            break;
        }
        if (!transferEntry(path, completed, error))
            break;
    }
    rollbackBatch.dismiss();
    return finish(completed, error);
}
