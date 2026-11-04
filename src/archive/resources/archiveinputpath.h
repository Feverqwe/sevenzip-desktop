#pragma once
#include "archive/opening/archivecontainerpath.h"
#include <QFileInfo>
inline bool IsArchiveFileOrPackage(const QFileInfo &file) {
    return file.isFile() ||
           (file.isDir() && IsSparseBundleName(file.fileName().toStdWString().c_str()));
}
