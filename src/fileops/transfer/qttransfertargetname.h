#pragma once

#include "fileops/links/qtlinkservices.h"
#include <QCoreApplication>
#include <QFileInfo>

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
