#pragma once

#include "fileops/links/qtlinkservices.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif

inline QString QtTransferSourceError(const QFileInfo &source, bool move) {
    if (source.isSymLink() || source.isJunction())
        return {};
    bool available = source.exists();
#ifdef Q_OS_UNIX
    if (available && !move)
        available = source.isReadable() && (!source.isDir() || source.isExecutable());
#else
    (void)move;
#endif
    if (available)
        return {};
    return QCoreApplication::translate("qtfiletransfertransaction", "Source unavailable: %1")
        .arg(source.filePath());
}

inline QString QtTransferSourceError(const QFileInfo &source, bool move, const QFileInfo &target,
                                     const QString &existingDirectory) {
    const QString error = QtTransferSourceError(source, false);
    if (error.isEmpty() || !move || (source.isDir() && target.isDir()))
        return error;
#ifdef Q_OS_UNIX
    struct stat from, to;
    if (!existingDirectory.isEmpty() &&
        stat(QFile::encodeName(source.absolutePath()).constData(), &from) == 0 &&
        stat(QFile::encodeName(existingDirectory).constData(), &to) == 0 &&
        from.st_dev != to.st_dev)
        return error;
#else
    (void)existingDirectory;
#endif
    return QtTransferSourceError(source, true);
}

inline QString QtTransferTargetKey(const QString &path) {
#ifdef Q_OS_WIN
    return path.toCaseFolded();
#else
    return path;
#endif
}

inline QString QtTransferTargetTypeError(const QFileInfo &source, const QFileInfo &target) {
    if (!target.exists())
        return {};
    const bool sourceDirectory = source.isDir() && !QtIsFileSystemLink(source.filePath());
    if (target.isDir() == sourceDirectory)
        return {};
    return QCoreApplication::translate(
               "qtfiletransfertransaction",
               "Cannot replace a file with a folder or a folder with a file: %1")
        .arg(target.filePath());
}
