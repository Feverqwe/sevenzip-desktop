#pragma once
#include "Windows/FileFind.h"
#include <QJsonObject>
#include <optional>

// Lossless native size/mtime values for persisted operation contexts.
struct QtNativeFileStamp {
    UInt64 size;
    CFiTime modified;
    QJsonObject Save() const;
    static std::optional<QtNativeFileStamp> Restore(const QJsonObject &value);
};
