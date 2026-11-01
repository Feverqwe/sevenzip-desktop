#include "fileops/overwrite/overwritequestion.h"
#include "Common/StringConvert.h"
#include "Windows/FileFind.h"
#include "Windows/TimeUtils.h"
#include <QDir>
#include <QFileInfo>

OverwriteQuestion::File NativeOverwriteFile(const QString &path, const FILETIME *time,
                                            const UInt64 *size, bool filesystem) {
    OverwriteQuestion::File file;
    file.Path = path;
    if (time)
        file.Modified = *time;
    if (size)
        file.Size = *size;
    file.Filesystem = filesystem;
    return file;
}

OverwriteQuestion::File FilesystemOverwriteFile(const QString &path) {
    OverwriteQuestion::File file;
    file.Path = path;
    file.Filesystem = true;
    NWindows::NFile::NFind::CFileInfo native;
    if (native.Find(us2fs(QDir::toNativeSeparators(path).toStdWString().c_str()))) {
        FILETIME time;
        FiTime_To_FILETIME(native.MTime, time);
        file.Modified = time;
        file.Size = native.Size;
        file.Directory = native.IsOsSymLink() ? QFileInfo(path).isDir() : native.IsDir();
    }
    return file;
}
