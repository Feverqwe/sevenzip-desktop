#pragma once
#include "Windows/FileFind.h"
#include <QString>

// File-generation identity without retaining an archive reader or its handles.
class ArchiveSourceSnapshot final {
    friend class ArchiveNativeOpen;
    QString path_, identity_;
    UInt64 size_;
    CFiTime mtime_;
    bool stable_;
    ArchiveSourceSnapshot(QString path, QString identity,
                          const NWindows::NFile::NFind::CFileInfo &info, bool stable);

  public:
    bool IsCurrent() const;
    bool IsCurrentFor(const QString &path) const;
};
