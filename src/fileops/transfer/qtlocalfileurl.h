#pragma once

#include <QDir>
#include <QUrl>

inline QString QtLocalFileUrlPath(const QUrl &url) {
    if (!url.isValid() || !url.isLocalFile())
        return {};
    QString path = QDir::fromNativeSeparators(url.toLocalFile());
    if (!QDir::isAbsolutePath(path) || path.contains(QChar::Null))
        return {};
    while (path.endsWith(u'/') && !QDir(path).isRoot())
        path.chop(1);
    return path;
}
