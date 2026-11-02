// Windows/FileDir.h
// Adapted from 7-Zip 0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.

#ifndef ZIP7_INC_WINDOWS_FILE_DIR_H
#define ZIP7_INC_WINDOWS_FILE_DIR_H

#include "Common/MyString.h"

#include "Windows/FileIO.h"
#include <QString>
#include <functional>
#include <memory>
class QtOwnedTempLease;

namespace NWindows {
namespace NFile {
namespace NDir {

bool GetWindowsDir(FString &path);
bool GetSystemDir(FString &path);

/*
WIN32 API : SetFileTime() doesn't allow to set zero timestamps in file
but linux : allows unix time = 0 in filesystem
*/
/*
SetDirTime() can be used to set time for file or for dir.
If path is symbolic link, SetDirTime() will follow symbolic link,
and it will set timestamps of symbolic link's target file or dir.
*/
bool SetDirTime(CFSTR path, const CFiTime *cTime, const CFiTime *aTime, const CFiTime *mTime);

/*
SetLinkFileTime() doesn't follow symbolic link,
and it sets timestamps for symbolic link file itself.
If (path) is not symbolic link, it still can work (at least in some new OS versions).
*/
bool SetLinkFileTime(CFSTR path, const CFiTime *cTime, const CFiTime *aTime, const CFiTime *mTime);

#ifdef _WIN32

bool SetFileAttrib(CFSTR path, DWORD attrib);

/*
  Some programs store posix attributes in high 16 bits of windows attributes field.
  Also some programs use additional flag markers: 0x8000 or 0x4000.
  SetFileAttrib_PosixHighDetect() tries to detect posix field, and it extracts only attribute
  bits that are related to current system only.
*/
#else

int my_chown(CFSTR path, uid_t owner, gid_t group);
int my_chown_Link(CFSTR path, uid_t owner, gid_t group);

#endif

bool SetFileAttrib_PosixHighDetect(CFSTR path, DWORD attrib);

#ifndef _WIN32
#define PROGRESS_CONTINUE 0
#define PROGRESS_CANCEL 1
// #define PROGRESS_STOP       2
// #define PROGRESS_QUIET      3
#endif
Z7_PURE_INTERFACES_BEGIN
DECLARE_INTERFACE(ICopyFileProgress) {
    // in: total, current: include all/processed alt streams.
    // it returns PROGRESS_CONTINUE or PROGRESS_CANCEL.
    virtual DWORD CopyFileProgress(UInt64 total, UInt64 current) = 0;
};
Z7_PURE_INTERFACES_END

bool MyMoveFile(CFSTR existFileName, CFSTR newFileName);
// (progress == NULL) is allowed
bool MyMoveFile_with_Progress(CFSTR oldFile, CFSTR newFile, ICopyFileProgress *progress);
// Publish a completed temporary file without deleting the previous destination.
// Cross-volume copying stages beside the destination before atomic replacement.
// published records replacement even if later temporary-file cleanup fails.
bool MyPublishFile_with_Progress(CFSTR oldFile, CFSTR newFile, ICopyFileProgress *progress,
                                 bool replaceExisting, bool *published = nullptr);

#ifndef UNDER_CE
bool MyCreateHardLink(CFSTR newFileName, CFSTR existFileName);
#endif

bool RemoveDir(CFSTR path);
bool CreateDir(CFSTR path);

/* CreateComplexDir returns true, if directory can contain files after the call (two cases):
    1) the directory already exists (network shares and drive paths are supported)
    2) the directory was created
  path can be WITH or WITHOUT trailing path separator. */

bool CreateComplexDir(CFSTR path);

bool DeleteFileAlways(CFSTR name);
bool DeleteFileAlways(const QString &path);
bool RemoveDirWithSubItems(const QString &path,
                           const std::function<bool(const QString &)> &checkBreak = {});
bool RemoveDirWithSubItems(const FString &path);
#ifdef _WIN32
bool RemoveDirAlways_if_Empty(const FString &path);
#else
#define RemoveDirAlways_if_Empty RemoveDir
#endif

bool MyGetFullPathName(CFSTR path, FString &resFullPath);
bool GetFullPathAndSplit(CFSTR path, FString &resDirPrefix, FString &resFileName);
bool GetOnlyDirPrefix(CFSTR path, FString &resDirPrefix);

#ifndef UNDER_CE

bool SetCurrentDir(CFSTR path);
bool GetCurrentDir(FString &resultPath);

#endif

bool MyGetTempPath(FString &resultPath);

bool CreateTempFile2(CFSTR prefix, bool addRandom, AString &postfix, NIO::COutFile *outFile);
// Prepare an owner's path before creation so cleanup can precede further allocation.
bool CreateTempFile2(CFSTR prefix, bool addRandom, AString &postfix, NIO::COutFile *outFile,
                     FString &createdPath);

class CTempFile MY_UNCOPYABLE {
    bool _mustBeDeleted;
    FString _path;
    std::shared_ptr<QtOwnedTempLease> _ownedLease;
    void DisableDeleting() { _mustBeDeleted = false; }

  public:
    CTempFile() : _mustBeDeleted(false) {}
    ~CTempFile() { Remove(); }
    const FString &GetPath() const Z7_lifetimebound { return _path; }
    bool Create(CFSTR pathPrefix, NIO::COutFile *outFile); // pathPrefix is not folder prefix
    const std::shared_ptr<QtOwnedTempLease> &OwnedLease() const { return _ownedLease; }
    bool CreateRandomInTempFolder(CFSTR namePrefix, NIO::COutFile *outFile);
    bool Remove();
    // bool MoveTo(CFSTR name, bool deleteDestBefore);
    bool MoveTo(CFSTR name, bool deleteDestBefore, ICopyFileProgress *progress,
                bool *published = nullptr);
};

class CTempDir MY_UNCOPYABLE {
    bool _mustBeDeleted;
    FString _path;
    std::shared_ptr<QtOwnedTempLease> _ownedLease;

  public:
    CTempDir() : _mustBeDeleted(false) {}
    ~CTempDir() { Remove(); }
    const FString &GetPath() const Z7_lifetimebound { return _path; }
    void DisableDeleting() { _mustBeDeleted = false; }
    const std::shared_ptr<QtOwnedTempLease> &OwnedLease() const { return _ownedLease; }
    bool Create(CFSTR namePrefix);
    bool Remove();
};

#if !defined(UNDER_CE)
class CCurrentDirRestorer MY_UNCOPYABLE {
    FString _path;

  public:
    bool NeedRestore;

    CCurrentDirRestorer() : NeedRestore(true) { GetCurrentDir(_path); }
    ~CCurrentDirRestorer() {
        if (!NeedRestore)
            return;
        FString s;
        if (GetCurrentDir(s))
            if (s != _path)
                SetCurrentDir(_path);
    }
};
#endif

} // namespace NDir
} // namespace NFile
} // namespace NWindows

#endif
