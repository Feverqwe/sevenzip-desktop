// Atomic publication adapter for completed native archive outputs.
// Native move reference: 7-Zip CPP/Windows/FileDir.cpp,
// 9128b80e3a471108678e2b3ec3c985861c8d7b0f, Igor Pavlov,
// LGPL-2.1-or-later; Qt adapts atomic/cross-volume publication.
#if defined(_WIN32) && !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0A00
#endif

#include "Common/StringConvert.h"
#include "Windows/FileName.h"
#include "Windows/StdAfx.h"
#include "fileops/native/FileDir.h"
#include "platform/temp/qtownedtempregistry.h"
#include <QDateTime>
#include <QFile>
#include <QScopeGuard>
#include <QTemporaryFile>
#include <memory>
#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#endif

#ifndef _UNICODE
extern bool g_IsNT;
#endif

using namespace NWindows;
using namespace NFile;
using namespace NName;

namespace NWindows {
namespace NFile {
namespace NDir {

#ifdef _WIN32
static bool PublishOverOpenFile(CFSTR oldFile, CFSTR newFile) {
    DWORD error = ERROR_ACCESS_DENIED;
    const auto restoreError = qScopeGuard([&] { ::SetLastError(error); });
    NIO::CInFile source;
    if (!source.Open_for_FileRenameInformation(oldFile)) {
        error = ::GetLastError();
        return false;
    }
    FString fullPath;
    if (!MyGetFullPathName(newFile, fullPath)) {
        error = ::GetLastError();
        return false;
    }
    const UString path = fs2us(fullPath);
    const DWORD bytes =
        DWORD(offsetof(FILE_RENAME_INFO, FileName) + (size_t(path.Len()) + 1) * sizeof(wchar_t));
    const auto storage = std::make_unique<BYTE[]>(bytes);
    auto *info = reinterpret_cast<FILE_RENAME_INFO *>(storage.get());
    info->Flags = FILE_RENAME_FLAG_REPLACE_IF_EXISTS | FILE_RENAME_FLAG_POSIX_SEMANTICS;
    info->FileNameLength = path.Len() * sizeof(wchar_t);
    memcpy(info->FileName, path.Ptr(), info->FileNameLength);
    // The old read generation remains valid; subsequent opens see the new one.
    const bool published =
        ::SetFileInformationByHandle(source.GetHandle(), FileRenameInfoEx, info, bytes) != 0;
    error = published ? ERROR_SUCCESS : ::GetLastError();
    return published;
}
#endif

static bool AtomicPublish(CFSTR oldFile, CFSTR newFile, bool replaceExisting) {
#ifdef _WIN32
    const DWORD flags = replaceExisting ? MOVEFILE_REPLACE_EXISTING : 0;
    IF_USE_MAIN_PATH_2(oldFile, newFile) {
        if (::MoveFileExW(fs2us(oldFile), fs2us(newFile), flags))
            return true;
    }
#ifdef Z7_LONG_PATH
    if (USE_SUPER_PATH_2) {
        UString source, destination;
        if (GetSuperPaths(oldFile, newFile, source, destination, USE_MAIN_PATH_2) &&
            ::MoveFileExW(source, destination, flags))
            return true;
    }
#endif
    if (replaceExisting && ::GetLastError() == ERROR_ACCESS_DENIED)
        return PublishOverOpenFile(oldFile, newFile);
    return false;
#elif defined(__APPLE__)
    return renamex_np(oldFile, newFile, replaceExisting ? 0 : RENAME_EXCL) == 0;
#else
    return (replaceExisting
                ? rename(oldFile, newFile)
                : renameat2(AT_FDCWD, oldFile, AT_FDCWD, newFile, RENAME_NOREPLACE)) == 0;
#endif
}

bool MyPublishFile_with_Progress(CFSTR oldFile, CFSTR newFile, ICopyFileProgress *progress,
                                 bool replaceExisting, bool *published) {
    if (published)
        *published = false;
    if (AtomicPublish(oldFile, newFile, replaceExisting)) {
        if (published)
            *published = true;
        return true;
    }
#ifdef _WIN32
    if (::GetLastError() != ERROR_NOT_SAME_DEVICE)
        return false;
#else
    if (errno != EXDEV)
        return false;
#endif
    // Adapt the source move's copy/progress loop to a destination-side temporary
    // file. Only a completed copy is published, using the same atomic policy.
    DWORD error = 0;
    const auto restoreError = qScopeGuard([&] {
        if (error)
            ::SetLastError(error);
    });
    QFile input(QString::fromWCharArray(fs2us(oldFile)));
    QTemporaryFile output(QString::fromWCharArray(fs2us(newFile)) + ".XXXXXX");
    std::shared_ptr<QtOwnedTempLease> stageLease;
    const auto copy = [&] {
        if (!input.open(QIODevice::ReadOnly) || !output.open())
            return false;
        stageLease = QtOwnedTempRegistry().RegisterCreated(output.fileName());
        if (!stageLease)
            return false;
        const UInt64 total = UInt64(input.size());
        UInt64 completed = 0;
        QByteArray block(1 << 16, Qt::Uninitialized);
        for (;;) {
            if (progress && progress->CopyFileProgress(total, completed) != PROGRESS_CONTINUE) {
#ifdef _WIN32
                error = ERROR_REQUEST_ABORTED;
#else
                error = EINTR;
#endif
                return false;
            }
            const qint64 size = input.read(block.data(), block.size());
            if (size < 0 || (size && output.write(block.constData(), size) != size))
                return false;
            if (!size)
                break;
            completed += UInt64(size);
        }
        if (!output.setPermissions(input.permissions()))
            return false;
        if (!output.setFileTime(input.fileTime(QFileDevice::FileModificationTime),
                                QFileDevice::FileModificationTime))
            return false;
        return output.flush();
    };
    const bool copied = copy();
    if (!copied && !error) {
        const bool denied = input.error() == QFileDevice::PermissionsError ||
                            output.error() == QFileDevice::PermissionsError;
#ifdef _WIN32
        error = denied ? ERROR_ACCESS_DENIED : ERROR_WRITE_FAULT;
#else
        error = denied ? EACCES : EIO;
#endif
    }
    input.close();
    output.close();
    if (!copied)
        return false;
    const FString stage = us2fs(output.fileName().toStdWString().c_str());
    if (!AtomicPublish(stage, newFile, replaceExisting)) {
        error = ::GetLastError();
        return false;
    }
    if (published)
        *published = true;
    const bool removed = DeleteFileAlways(oldFile);
    if (!removed)
        error = ::GetLastError();
    return removed;
}

} // namespace NDir
} // namespace NFile
} // namespace NWindows
