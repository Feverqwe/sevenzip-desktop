#include "fileops/links/qtlinkservices.h"
#include "Common/StringConvert.h"
#include "Common/UTFConvert.h"
#include "Windows/FileFind.h"
#include "Windows/FileIO.h"
#include "fileops/native/FileDir.h"
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#ifndef _WIN32
#include <cstdio>
#ifdef __APPLE__
#include <sys/stdio.h>
#endif
#endif

namespace {
FString NativePath(const QString &path) {
    return us2fs(QDir::toNativeSeparators(path).toStdWString().c_str());
}
} // namespace

bool QtIsFileSystemLink(const QString &path) {
    NWindows::NFile::NFind::CFileInfo info;
    return info.Find(NativePath(path)) && info.IsOsSymLink();
}

bool QtRenameFileSystemEntry(const QString &source, const QString &destination) {
#ifdef _WIN32
    return NWindows::NFile::NDir::MyMoveFile(NativePath(source), NativePath(destination));
#else
    // Original MyMoveFile falls back to copying file bytes on EXDEV. This
    // narrow rename service must let the caller clone a link object instead.
    return ::rename(NativePath(source), NativePath(destination)) == 0;
#endif
}

bool QtRenameFileSystemEntryWithoutReplacing(const QString &source, const QString &destination) {
#ifdef __APPLE__
    // Match source MoveFileW's no-replacement contract while allowing a change
    // of spelling on a case-insensitive volume, for files, folders and links.
    return ::renamex_np(NativePath(source), NativePath(destination), RENAME_EXCL) == 0;
#elif defined(__linux__)
    return NWindows::NFile::NDir::MyMoveFile_NoReplace(NativePath(source), NativePath(destination));
#else
#ifndef _WIN32
    const QFileInfo target(destination);
    if (target.exists() || target.isSymLink()) {
        errno = EEXIST;
        return false;
    }
#endif
    return NWindows::NFile::NDir::MyMoveFile(NativePath(source), NativePath(destination));
#endif
}

bool QtRemoveFileSystemEntry(const QString &path) {
    using namespace NWindows::NFile;
    const FString native = NativePath(path);
    NFind::CFileInfo info;
    if (!info.Find(native))
        return false;
    if (!info.IsDir())
        return NDir::DeleteFileAlways(native);
    if (info.IsOsSymLink())
        return NDir::RemoveDir(native);
    return NDir::RemoveDirWithSubItems(native);
}

bool QtCopyFileSystemLink(const QString &source, const QString &destination) {
    using namespace NWindows::NFile;
    const FString from = NativePath(source), to = NativePath(destination);
    NFind::CFileInfo info, occupied;
    if (!info.Find(from) || !info.IsOsSymLink())
        return false;
    if (occupied.Find(to)) {
        SetLastError(EEXIST);
        return false;
    }
    CByteBuffer raw;
    if (!NIO::GetReparseData(from, raw))
        return false;
#ifdef _WIN32
    // Reserve an empty object exclusively: SetReparseData also accepts existing
    // objects, which is unsuitable for a copy destination that changed meanwhile.
    if (info.IsDir()) {
        if (!NDir::CreateDir(to))
            return false;
    } else {
        NIO::COutFile file;
        if (!file.Create_NEW(to))
            return false;
        if (!file.Close()) {
            const DWORD error = GetLastError();
            QtRemoveFileSystemEntry(destination);
            SetLastError(error);
            return false;
        }
    }
    const bool created = NIO::SetReparseData(to, info.IsDir(), raw, DWORD(raw.Size()));
#else
    // Keep the original readlink bytes, including relative and non-UTF-8 targets.
    AString target;
    target.SetFrom_CalcLen(reinterpret_cast<const char *>(static_cast<const Byte *>(raw)),
                           raw.Size());
    const bool created = NIO::SetSymLink(to, target);
    if (!created)
        return false; // symlink is exclusive; a racing destination belongs to its creator.
#endif
    if (created && NDir::SetLinkFileTime(to, &info.CTime, &info.ATime, &info.MTime))
        return true;
    const DWORD error = GetLastError();
    // SetReparseData can leave its newly created empty file on a privilege error.
    QtRemoveFileSystemEntry(destination);
    SetLastError(error);
    return false;
}

#ifndef _WIN32
bool QtGetPosixLinkTarget(CFSTR path, UString &target) {
    CByteBuffer raw;
    if (!NWindows::NFile::NIO::GetReparseData(path, raw))
        return false;
    AString text;
    text.SetFrom_CalcLen(reinterpret_cast<const char *>(static_cast<const Byte *>(raw)),
                         raw.Size());
    if (ConvertUTF8ToUnicode(text, target))
        return true;
    SetLastError(EINVAL);
    return false;
}
bool QtSetPosixLink(const UString &from, const UString &to) {
    using namespace NWindows::NFile;
    const FString path = us2fs(from);
    NFind::CFileInfo info;
    if (!info.Find(path))
        return NIO::SetSymLink_UString(path, to);
    if (!info.IsOsSymLink()) {
        SetLastError(EEXIST); // POSIX has no reparse attribute on an empty plain inode.
        return false;
    }
    if (to.IsEmpty())
        return NDir::DeleteFileAlways(path);
    // POSIX replaces the link inode rather than a Windows reparse attribute.
    // Build a new link privately in the same parent, then use the source rename
    // adapter atomically. Failure preserves the old link and its target bytes.
    const QString parent = QFileInfo(QString::fromWCharArray(from.Ptr())).absolutePath();
    bool result = false;
    DWORD error = EACCES;
    {
        QTemporaryDir stage(QDir(parent).filePath(".sevenzip-link-XXXXXX"));
        if (stage.isValid()) {
            const FString staged = us2fs(UString(stage.filePath("link").toStdWString().c_str()));
            if (NIO::SetSymLink_UString(staged, to))
                result = NDir::MyMoveFile(staged, path);
            error = GetLastError();
        }
    }
    SetLastError(error); // Private stage cleanup must not replace the native error.
    return result;
}
#endif
