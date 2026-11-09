#pragma once

#include <QString>

inline QString QtTransferTargetKey(const QString &path) {
#ifdef Q_OS_WIN
    return path.toCaseFolded();
#else
    return path;
#endif
}
