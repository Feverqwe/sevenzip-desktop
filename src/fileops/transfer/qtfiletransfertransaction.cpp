#include "fileops/transfer/qtfiletransfertransaction.h"
#include "7zip/Common/FilePathAutoRename.h"
#include "Common/StringConvert.h"
#include "Windows/ErrorMsg.h"
#include "archive/callbacks/IFileExtractCallback.h"
#include "filemanager/providers/FSFolder.h"
#include "fileops/links/qtlinkservices.h"
#include "fileops/transfer/filetransferoperation.h"
#include "fileops/transfer/qtfiletransfercopycallback.h"
#include "localization/qtlanguageservices.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QUuid>
#include <algorithm>

bool QtFileTransferTransaction::removeEntry(const QString &path) {
    return QtRemoveFileSystemEntry(path);
}

bool QtFileTransferTransaction::validateEntry(const QString &source, QString *error) {
    if (worker_.canceled_) {
        *error = QCoreApplication::translate("qtfiletransfertransaction", "Operation canceled.");
        return false;
    }
    const QFileInfo info(source);
    if (QtIsFileSystemLink(source))
        return true;
    if (!info.exists() || (!info.isFile() && !info.isDir())) {
        *error = QCoreApplication::translate("qtfiletransfertransaction", "Source unavailable: %1")
                     .arg(source);
        return false;
    }
    if (!info.isDir())
        return true;
    const QFileInfoList children = QDir(source).entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    for (const QFileInfo &child : children) {
        if (!validateEntry(child.absoluteFilePath(), error))
            return false;
    }
    return true;
}

bool QtFileTransferTransaction::copyEntry(const QString &source, const QString &target,
                                          QString *error, bool reportProgress) {
    NFsFolder::CCopyState state;
    quint64 previous = 0;
    state.Progress = [this, &previous, reportProgress](quint64 completed) {
        if (reportProgress && completed != previous)
            emit worker_.advanced(qint64(completed - previous));
        previous = completed;
        return !worker_.canceled_;
    };
    if (reportProgress)
        state.CurrentFile = [this](const QString &path) { emit worker_.currentFile(path); };
    if (!state.CallProgress()) {
        *error = state.ErrorMessage;
        return false;
    }
    const QFileInfo info(source);
    const bool copied = info.isDir() && !QtIsFileSystemLink(source)
                            ? NFsFolder::CopyFolder(state, source, target)
                            : NFsFolder::CopyFile_Ask(state, source, info, target);
    *error = state.ErrorMessage;
    return copied;
}

bool QtFileTransferTransaction::mergeEntry(
    const QString &source, const QString &stage, const QString &target,
    const std::function<int(const QString &, const QString &)> &answer, QStringList *writtenSources,
    QString *error) {
    CFileTransferCopyCallback callback(
        [this, &answer, &stage, &target](const QString &incoming, const QString &requested,
                                         QString &effective, bool &write) -> HRESULT {
            if (worker_.canceled_)
                return E_ABORT;
            const QFileInfo existing(requested);
            if (existing.isSymLink() || (existing.exists() && !existing.isFile()))
                return E_ABORT;
            if (existing.exists()) {
                const QString visible = target + requested.mid(stage.size());
                const int decision = answer(visible, incoming);
                if (decision == NOverwriteAnswer::kNo || decision == NOverwriteAnswer::kNoToAll) {
                    write = false;
                    return S_OK;
                }
                if (decision == NOverwriteAnswer::kAutoRename) {
                    FString renamed = us2fs(requested.toStdWString().c_str());
                    if (!AutoRenamePath(renamed))
                        return E_ABORT;
                    effective = QString::fromWCharArray(fs2us(renamed));
                } else if (decision == NOverwriteAnswer::kYes ||
                           decision == NOverwriteAnswer::kYesToAll) {
                    if (!QFile::remove(requested))
                        return E_ABORT;
                } else {
                    return E_ABORT;
                }
            }
            write = true;
            return S_OK;
        },
        [this](quint64 delta) {
            if (delta)
                emit worker_.advanced(qint64(delta));
            return !worker_.canceled_;
        },
        [this](const QString &path) { emit worker_.currentFile(path); });
    NFsFolder::CCopyState state;
    state.Callback = &callback;
    const bool merged = NFsFolder::CopyFolder(state, source, stage);
    *writtenSources = callback.writtenSources;
    *error = !state.ErrorMessage.isEmpty() ? state.ErrorMessage : callback.errorMessage;
    if (!merged && error->isEmpty())
        *error =
            QCoreApplication::translate("qtfiletransfertransaction", "Could not merge folders: %1")
                .arg(target);
    return merged;
}

bool QtFileTransferTransaction::removeMergedSource(const QString &source,
                                                   const QStringList &writtenSources,
                                                   QString *error) {
    for (const QString &path : writtenSources) {
        if (worker_.canceled_) {
            *error =
                QCoreApplication::translate("qtfiletransfertransaction", "Operation canceled.");
            return false;
        }
        if (!worker_.adapter_.removeSource(path)) {
            *error = QCoreApplication::translate("qtfiletransfertransaction",
                                                 "Could not delete source after copying: %1")
                         .arg(path);
            return false;
        }
    }
    QStringList directories;
    QDirIterator iterator(source, QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        if (worker_.canceled_) {
            *error =
                QCoreApplication::translate("qtfiletransfertransaction", "Operation canceled.");
            return false;
        }
        directories << iterator.next();
    }
    std::sort(directories.begin(), directories.end(),
              [](const QString &a, const QString &b) { return a.size() > b.size(); });
    for (const QString &directory : directories)
        QDir().rmdir(directory);
    QDir().rmdir(source); // Skipped files intentionally keep the source folder.
    if (worker_.canceled_) {
        *error = QCoreApplication::translate("qtfiletransfertransaction", "Operation canceled.");
        return false;
    }
    return true;
}

QStringList QtFileTransferTransaction::prepareTargets(QString &error) const {
    const bool directPath =
        worker_.destinationKind_ == FileTransferOperation::DestinationKind::DirectPath;
    const QString destinationDirectory =
        directPath ? QFileInfo(worker_.destination_).absolutePath() : worker_.destination_;
    const QFileInfo destinationInfo(destinationDirectory);
    const QString canonicalDestination = destinationInfo.canonicalFilePath();
    QStringList targets;
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
            QtIsFileSystemLink(target) || samePath || targets.contains(target) ||
            (!sourceLink && info.isDir() &&
             NFsFolder::IsDestChild(info.canonicalFilePath(), canonicalDestination)))
            error = QCoreApplication::translate(
                "qtfiletransfertransaction",
                "Source or destination unavailable, or the name is already in use.");
        targets << target;
    }
    return targets;
}

void QtFileTransferTransaction::rollback(const QList<Completed> &completed) {
    for (qsizetype i = completed.size(); i > 0; --i) {
        const Completed &entry = completed[i - 1];
        if (worker_.move_ && !entry.copiedMove)
            QDir().rename(entry.target, entry.source);
        else
            removeEntry(entry.target);
        if (!entry.backup.isEmpty())
            QDir().rename(entry.backup, entry.target);
    }
}

void QtFileTransferTransaction::finish(QList<Completed> &completed, QString &error) {
    if (!error.isEmpty()) {
        rollback(completed);
        worker_.resultKind_ = worker_.canceled_ ? FileTransferOperation::ResultKind::Canceled
                                                : FileTransferOperation::ResultKind::Failure;
        emit worker_.result(false, error);
        return;
    }
    if (worker_.move_) {
        for (Completed &entry : completed) {
            if (!entry.copiedMove)
                continue;
            if (worker_.canceled_) {
                error =
                    QCoreApplication::translate("qtfiletransfertransaction", "Operation canceled.");
                break;
            }
            // An attempted recursive removal can already have consumed bytes.
            entry.retainPublished = true;
            bool removed = false;
            try {
                removed = entry.merged
                              ? removeMergedSource(entry.source, entry.writtenSources, &error)
                              : worker_.adapter_.removeSource(entry.source);
            } catch (...) {
                error = QString::fromWCharArray(NWindows::NError::MyFormatMessage(E_FAIL));
            }
            if (!removed) {
                // Recursive deletion can fail after removing some children.
                // Retain the published copy so the remaining bytes stay available.
                if (error.isEmpty())
                    error = QCoreApplication::translate("qtfiletransfertransaction",
                                                        "Could not delete source after copying: %1")
                                .arg(entry.source);
                break;
            }
        }
        if (!error.isEmpty()) {
            for (qsizetype i = completed.size(); i > 0; --i) {
                const Completed &entry = completed[i - 1];
                if (entry.retainPublished) {
                    if (!entry.backup.isEmpty())
                        removeEntry(entry.backup);
                    continue;
                }
                removeEntry(entry.target);
                if (!entry.backup.isEmpty())
                    QDir().rename(entry.backup, entry.target);
            }
            worker_.resultKind_ = FileTransferOperation::ResultKind::Failure;
            emit worker_.result(
                false, error + QCoreApplication::translate("qtfiletransfertransaction",
                                                           " Some items may have been moved."));
            return;
        }
    }
    for (const Completed &entry : completed)
        if (!entry.backup.isEmpty())
            removeEntry(entry.backup);
    QString message;
    if (!worker_.move_)
        message = QCoreApplication::translate("qtfiletransfertransaction", "Items copied.");
    else if (std::any_of(completed.cbegin(), completed.cend(), [](const Completed &entry) {
                 return entry.merged && QFileInfo::exists(entry.source);
             }))
        message = QCoreApplication::translate("qtfiletransfertransaction",
                                              "Some files skipped; sources retained.");
    else
        message = QCoreApplication::translate("qtfiletransfertransaction", "Items moved.");
    worker_.resultKind_ = FileTransferOperation::ResultKind::Success;
    emit worker_.result(true, message);
}

void QtFileTransferTransaction::run() {
    enum class OverwriteMode { Ask, Skip, Overwrite, Rename };
    OverwriteMode overwriteMode = OverwriteMode::Ask;
    auto overwriteAnswer = [this, &overwriteMode](const QString &existing,
                                                  const QString &incoming) -> int {
        if (overwriteMode == OverwriteMode::Skip)
            return NOverwriteAnswer::kNo;
        if (overwriteMode == OverwriteMode::Overwrite)
            return NOverwriteAnswer::kYes;
        if (overwriteMode == OverwriteMode::Rename)
            return NOverwriteAnswer::kAutoRename;
        const int answer = worker_.askOverwrite(existing, incoming);
        if (answer == NOverwriteAnswer::kNoToAll)
            overwriteMode = OverwriteMode::Skip;
        else if (answer == NOverwriteAnswer::kYesToAll)
            overwriteMode = OverwriteMode::Overwrite;
        else if (answer == NOverwriteAnswer::kAutoRename)
            overwriteMode = OverwriteMode::Rename;
        return answer;
    };
    QString error;
    QStringList targets = prepareTargets(error);
    QList<Completed> completed;
    auto rollbackBatch = qScopeGuard([&] { rollback(completed); });
    for (qsizetype i = 0; i < worker_.sources_.size() && error.isEmpty(); ++i) {
        if (worker_.canceled_) {
            error = QCoreApplication::translate("qtfiletransfertransaction", "Operation canceled.");
            break;
        }
        const QString source = worker_.sources_[i];
        QString target = targets[i];
        bool mayReplace = false;
        const QFileInfo sourceInfo(source);
        const QFileInfo existingInfo(target);
        const bool merge =
            existingInfo.isDir() && sourceInfo.isDir() && !QtIsFileSystemLink(source);
        if (existingInfo.exists()) {
            if (existingInfo.isDir() != sourceInfo.isDir()) {
                error = QCoreApplication::translate(
                            "qtfiletransfertransaction",
                            "Cannot replace a file with a folder or a folder with a file: %1")
                            .arg(target);
                break;
            }
            if (merge) {
                mayReplace = true;
            } else {
                const int answer = overwriteAnswer(target, source);
                switch (answer) {
                case NOverwriteAnswer::kNo:
                case NOverwriteAnswer::kNoToAll:
                    continue;
                case NOverwriteAnswer::kYes:
                case NOverwriteAnswer::kYesToAll:
                    mayReplace = true;
                    break;
                case NOverwriteAnswer::kAutoRename: {
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
                default:
                    error = QCoreApplication::translate("qtfiletransfertransaction",
                                                        "Operation canceled.");
                    break;
                }
            }
            if (!error.isEmpty())
                break;
        }
        const QString suffix =
            QStringLiteral(".sevenzip-%1").arg(QUuid::createUuid().toString(QUuid::Id128));
        const QString stage = target + suffix;
        QString backup;
        QStringList writtenSources;
        QString ownedPath;
        bool renamed = false;
        auto rollbackItem = qScopeGuard([&] {
            if (ownedPath.isEmpty())
                return;
            if (renamed)
                QDir().rename(ownedPath, source);
            else
                removeEntry(ownedPath);
            if (!backup.isEmpty())
                QDir().rename(backup, target);
        });
        emit worker_.currentFile(worker_.sources_[i]);
        const bool valid = !worker_.move_ || validateEntry(source, &error);
        if (valid)
            ownedPath = stage;
        renamed =
            valid && worker_.move_ && !merge && worker_.adapter_.trySourceRename(source, stage);
        const bool copiedMove = valid && worker_.move_ && !renamed;
        const bool staged =
            valid &&
            (merge ? (copyEntry(target, stage, &error, false) &&
                      mergeEntry(source, stage, target, overwriteAnswer, &writtenSources, &error))
                   : (renamed || copyEntry(source, stage, &error)));
        if (!staged && error.isEmpty())
            error =
                QCoreApplication::translate("qtfiletransfertransaction", "Could not move item: %1")
                    .arg(worker_.sources_[i]);
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
                backup = backupPath;
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
            break;
        completed.append({source, target, backup, copiedMove, worker_.move_ && !copiedMove, merge,
                          writtenSources});
        rollbackItem.dismiss();
    }
    rollbackBatch.dismiss();
    finish(completed, error);
}
