#pragma once
#include "Common/MyTypes.h"
#include "Common/MyWindows.h"
#include <QMetaType>
#include <QString>
#include <optional>

// Owned snapshot of the native overwrite callback's optional metadata.
struct OverwriteQuestion {
    struct File {
        QString Path;
        std::optional<FILETIME> Modified;
        std::optional<UInt64> Size;
        bool Filesystem = false;
        bool Directory = false;
    };
    File Existing;
    File Incoming;
};
Q_DECLARE_METATYPE(OverwriteQuestion)

OverwriteQuestion::File NativeOverwriteFile(const QString &path, const FILETIME *time,
                                            const UInt64 *size, bool filesystem);
OverwriteQuestion::File FilesystemOverwriteFile(const QString &path);
