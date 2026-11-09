#include "Common/StringConvert.h"
#include "filemanager/providers/FSFolder.h"
#include "fileops/links/qtlinkservices.h"
#include "fileops/paths/qtautorename.h"
#include "fileops/transfer/qtfiletransfercopycallback.h"
#include "fileops/transfer/qtfiletransfertransaction.h"
#include "localization/qtlanguageservices.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <algorithm>

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

bool QtFileTransferTransaction::mergeEntry(const QString &source, const QString &stage,
                                           const QString &target, QStringList *writtenSources,
                                           bool &publish, QString *error) {
    QString conflict;
    bool skippedConflict = false;
    CFileTransferCopyCallback callback(
        [this, &source, &stage, &conflict, &skippedConflict,
         &target](const OverwriteQuestion::File &incoming,
                  const QString &requested) -> CFileTransferCopyCallback::WriteDecision {
            if (worker_.canceled_)
                return std::unexpected(E_ABORT);
            const QFileInfo existing(requested);
            const QString visible = target + requested.mid(stage.size());
            if (QtIsFileSystemLink(requested))
                conflict = QCoreApplication::translate("qtfiletransfertransaction",
                                                       "Destination is a symbolic link: %1")
                               .arg(visible);
            else if (existing.exists() && !existing.isFile())
                conflict = QCoreApplication::translate(
                               "qtfiletransfertransaction",
                               "Cannot replace a file with a folder or a folder with a file: %1")
                               .arg(visible);
            if (!conflict.isEmpty())
                return std::unexpected(E_FAIL);
            QString effective = requested;
            if (existing.exists()) {
                switch (decideOverwrite({requested, visible, incoming})) {
                case OverwriteDecision::Skip:
                    skippedConflict = true;
                    return std::nullopt;
                case OverwriteDecision::Rename: {
                    effective =
                        QtAvailableRenamePath(requested, [&source, &stage](const QString &name) {
                            const QString original = source + name.mid(stage.size());
                            return QFileInfo::exists(original) || QtIsFileSystemLink(original);
                        });
                    if (effective.isEmpty())
                        return std::unexpected(E_ABORT);
                    break;
                }
                case OverwriteDecision::Replace:
                    if (!QFile::remove(requested))
                        return std::unexpected(E_ABORT);
                    break;
                case OverwriteDecision::Cancel:
                    return std::unexpected(E_ABORT);
                }
            }
            return effective;
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
    publish = !skippedConflict || state.CreatedDirectory || !writtenSources->isEmpty();
    *error = !conflict.isEmpty()             ? conflict
             : !state.ErrorMessage.isEmpty() ? state.ErrorMessage
                                             : callback.errorMessage;
    if (!merged && error->isEmpty()) {
        if (worker_.canceled_)
            *error =
                QCoreApplication::translate("qtfiletransfertransaction", "Operation canceled.");
        else
            *error = QCoreApplication::translate("qtfiletransfertransaction",
                                                 "Could not merge folders: %1")
                         .arg(target);
    }
    return merged;
}

bool QtFileTransferTransaction::removeMergedSource(const QString &source,
                                                   const QStringList &writtenSources,
                                                   QString *error, bool &rootRemoved) {
    for (const QString &path : writtenSources) {
        if (worker_.canceled_) {
            *error = sourceRemovalCanceled();
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
            *error = sourceRemovalCanceled();
            return false;
        }
        directories << iterator.next();
    }
    std::sort(directories.begin(), directories.end(),
              [](const QString &a, const QString &b) { return a.size() > b.size(); });
    for (const QString &directory : directories)
        QDir().rmdir(directory);
    rootRemoved = QDir().rmdir(source); // Skipped files keep the source folder.
    if (!rootRemoved && worker_.canceled_) {
        *error = sourceRemovalCanceled();
        return false;
    }
    return true;
}
