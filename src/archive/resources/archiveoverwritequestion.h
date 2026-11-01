#pragma once
#include "Common/MyTypes.h"
#include "Common/MyWindows.h"
#include <QMetaType>
#include <QString>
#include <optional>

// Owned snapshot of the native overwrite callback's optional metadata.
struct ArchiveOverwriteQuestion {
    struct File {
        QString Path;
        std::optional<FILETIME> Modified;
        std::optional<UInt64> Size;
    };
    File Existing;
    File Incoming;
};
Q_DECLARE_METATYPE(ArchiveOverwriteQuestion)
