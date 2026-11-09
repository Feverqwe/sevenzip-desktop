#pragma once

#include "fileops/links/qtlinkservices.h"
#include <QCoreApplication>
#include <QFileInfo>

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
