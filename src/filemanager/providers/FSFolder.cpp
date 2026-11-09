// Adapted from 7-Zip CPP/7zip/UI/FileManager/FSFolder.cpp (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Source enumeration and navigation use portable upstream filesystem services.
// Qt mutation services/callbacks remain a narrow integration boundary.
#include "filemanager/providers/FSFolder.h"
#include "7zip/PropID.h"
#include "Common/ComTry.h"
#include "Common/StringConvert.h"
#include "Windows/FileIO.h"
#include "Windows/FileName.h"
#include "Windows/PropVariant.h"
#include "filemanager/providers/FSDrives.h"
#include "fileops/links/qtlinkservices.h"
#include "fileops/native/FileDir.h"
#include "localization/qtlanguageservices.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace NFsFolder {
namespace {
FString FsPath(const QString &path) {
    const auto wide = QDir::toNativeSeparators(path).toStdWString();
    return us2fs(wide.c_str());
}
QString QtPath(const FString &path) {
    return QDir::fromNativeSeparators(QString::fromWCharArray(fs2us(path)));
}
} // namespace

CFSFolder::CFSFolder(QString path) {
    FString prefix = FsPath(QDir::cleanPath(std::move(path)));
    NWindows::NFile::NName::NormalizeDirPathPrefix(prefix);
    Init(prefix);
}

#ifdef _WIN32
static void StartWatchingChanges(NWindows::NFile::NFind::CFindChangeNotification &watch,
                                 const FString &path) {
    watch.FindFirst(path, false,
                    FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                        FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SIZE |
                        FILE_NOTIFY_CHANGE_LAST_WRITE);
}
#endif

HRESULT CFSFolder::Init(const FString &path) {
    _path = path;
#ifdef _WIN32
    StartWatchingChanges(_findChangeNotification, _path);
    if (!_findChangeNotification.IsHandleAllocated()) {
        const HRESULT lastError = GetLastError_noZero_HRESULT();
        NWindows::NFile::NFind::CFindFile findFile;
        NWindows::NFile::NFind::CFileInfo fi;
        FString path2 = _path;
        path2.Add_Char('*');
        if (!findFile.FindFirst(path2, fi))
            return lastError;
    }
#else
    // The Windows source validates through notifications/FindFirst. POSIX has
    // no equivalent notification handle, so use the unchanged native stat service.
    if (!NWindows::NFile::NFind::DoesDirExist_FollowLink(_path))
        return E_FAIL;
#endif
    return S_OK;
}

HRESULT CFSFolder::LoadSubItems(int dirItem, const FString &relPrefix) {
    const unsigned startIndex = Folders.Size();
    {
        NWindows::NFile::NFind::CEnumerator enumerator;
        enumerator.SetDirPrefix(_path + relPrefix);
        CDirItem fi;
        fi.FolderStat_Defined = false;
        fi.NumFolders = 0;
        fi.NumFiles = 0;
        fi.Parent = dirItem;
        for (;;) {
            if (CheckBreak && !CheckBreak(QtPath(_path + relPrefix)))
                return E_ABORT;
#ifdef _WIN32
            if (!enumerator.Next(fi))
                break;
#else
            bool found;
            NWindows::NFile::NFind::CDirEntry entry;
            if (!enumerator.Next(entry, found))
                return GetLastError_noZero_HRESULT();
            if (!found)
                break;
            if (!enumerator.Fill_FileInfo(entry, fi, false))
                return GetLastError_noZero_HRESULT();
#endif
            if (fi.IsDir()) {
                fi.Size = 0;
                bool recurse = _flatMode;
#ifdef _WIN32
                // Preserve the Qt workflow's no-follow recursive listing policy.
                recurse = recurse && !fi.Has_Attrib_ReparsePoint();
#endif
                if (recurse)
                    Folders.Add(relPrefix + fi.Name + FCHAR_PATH_SEPARATOR);
            }
            fi.NameSortKey.reset();
            fi.PackSize_Defined = false;
            fi.FileInfo_Defined = false;
            fi.FileInfo_WasRequested = false;
            fi.FileIndex = 0;
            fi.NumLinks = 0;
            fi.ChangeTime_Defined = false;
            fi.ChangeTime_WasRequested = false;
            fi.PackSize = fi.Size;
            fi.Reparse.Free();
#ifdef _WIN32
            if (fi.HasReparsePoint()) {
                fi.FileInfo_WasRequested = true;
                BY_HANDLE_FILE_INFORMATION info;
                if (NWindows::NFile::NIO::GetReparseData(_path + relPrefix + fi.Name, fi.Reparse,
                                                         &info)) {
                    fi.NumLinks = info.nNumberOfLinks;
                    fi.FileIndex = (((UInt64)info.nFileIndexHigh) << 32) + info.nFileIndexLow;
                    fi.FileInfo_Defined = true;
                }
            }
#endif
            Files.Add(fi);
            if (SetCompleted && !SetCompleted(Files.Size()))
                return E_ABORT;
        }
    }
    if (!_flatMode)
        return S_OK;
    const unsigned endIndex = Folders.Size();
    for (unsigned i = startIndex; i < endIndex; i++) {
        // Upstream continues after an unreadable child; only the Qt cancellation
        // adapter must stop the current scan immediately.
        if (LoadSubItems((int)i, Folders[i]) == E_ABORT)
            return E_ABORT;
    }
    return S_OK;
}

Z7_COM7F_IMF(CFSFolder::LoadItems()) {
    COM_TRY_BEGIN
#ifdef _WIN32
    const bool directoryExists = QFileInfo(QDir::cleanPath(QtPath(_path))).isDir();
    if (!directoryExists)
        _findChangeNotification.Close();
#endif
    Int32 dummy;
    WasChanged(&dummy);
#ifdef _WIN32
    if (directoryExists && !_findChangeNotification.IsHandleAllocated())
        StartWatchingChanges(_findChangeNotification, _path);
#endif
    Clear();
    HRESULT result;
    try {
        result = LoadSubItems(-1, FString());
    } catch (...) {
        Clear();
        throw;
    }
    if (result != S_OK)
        Clear();
    else
        _commentsAreLoaded = false;
    return result;
    COM_TRY_END
}

Z7_COM7F_IMF(CFSFolder::GetNumberOfItems(UInt32 *numItems)) {
    *numItems = Files.Size();
    return S_OK;
}

Z7_COM7F_IMF(CFSFolder::SetFlatMode(Int32 flatMode)) {
    _flatMode = IntToBool(flatMode);
    return S_OK;
}

HRESULT CFSFolder::BindToFolderSpec(CFSTR name, IFolderFolder **resultFolder) {
    *resultFolder = NULL;
    CFSFolder *folderSpec = new CFSFolder;
    CMyComPtr<IFolderFolder> subFolder = folderSpec;
    RINOK(folderSpec->Init(_path + name + FCHAR_PATH_SEPARATOR))
    *resultFolder = subFolder.Detach();
    return S_OK;
}

Z7_COM7F_IMF(CFSFolder::BindToFolder(UInt32 index, IFolderFolder **resultFolder)) {
    COM_TRY_BEGIN
    *resultFolder = NULL;
    if (index >= Files.Size())
        return E_INVALIDARG;
    const CDirItem &fi = Files[index];
    if (!fi.IsDir())
        return E_INVALIDARG;
    return BindToFolderSpec(GetRelPath(fi), resultFolder);
    COM_TRY_END
}

Z7_COM7F_IMF(CFSFolder::BindToFolder(const wchar_t *name, IFolderFolder **resultFolder)) {
    COM_TRY_BEGIN
    return BindToFolderSpec(us2fs(name), resultFolder);
    COM_TRY_END
}

Z7_COM7F_IMF(CFSFolder::BindToParentFolder(IFolderFolder **resultFolder)) {
    COM_TRY_BEGIN
    *resultFolder = NULL;
    if (_path.IsEmpty())
        return E_INVALIDARG;
#ifdef _WIN32
    if (NWindows::NFile::NName::IsDriveRootPath_SuperAllowed(_path)) {
        CFSDrives *drivesFolderSpec = new CFSDrives;
        CMyComPtr<IFolderFolder> drivesFolder = drivesFolderSpec;
        drivesFolderSpec->Init(false, NWindows::NFile::NName::IsSuperPath(_path));
        *resultFolder = drivesFolder.Detach();
        return S_OK;
    }
#else
    if (_path == FSTRING_PATH_SEPARATOR)
        return S_OK;
#endif
    int pos = _path.ReverseFind_PathSepar();
    if (pos < 0 || pos != (int)_path.Len() - 1)
        return E_FAIL;
    FString parentPath = _path.Left((unsigned)pos);
    pos = parentPath.ReverseFind_PathSepar();
    parentPath.DeleteFrom((unsigned)(pos + 1));
#ifdef _WIN32
    if (NWindows::NFile::NName::IsDrivePath_SuperAllowed(parentPath))
#else
    if (!parentPath.IsEmpty() && parentPath[0] == FCHAR_PATH_SEPARATOR)
#endif
    {
        CFSFolder *parentFolderSpec = new CFSFolder;
        CMyComPtr<IFolderFolder> parentFolder = parentFolderSpec;
        if (parentFolderSpec->Init(parentPath) == S_OK) {
            *resultFolder = parentFolder.Detach();
            return S_OK;
        }
    }
    return S_OK;
    COM_TRY_END
}

Z7_COM7F_IMF(CFSFolder::WasChanged(Int32 *wasChanged)) {
    bool wasChangedMain = false;
#ifdef _WIN32
    for (;;) {
        if (!_findChangeNotification.IsHandleAllocated())
            break;
        DWORD waitResult = ::WaitForSingleObject(_findChangeNotification, 0);
        if (waitResult != WAIT_OBJECT_0)
            break;
        wasChangedMain = true;
        if (!_findChangeNotification.FindNext()) {
            _findChangeNotification.Close();
            break;
        }
    }
#endif
    *wasChanged = BoolToInt(wasChangedMain);
    return S_OK;
}

Z7_COM7F_IMF(CFSFolder::Clone(IFolderFolder **resultFolder)) {
    CFSFolder *fsFolderSpec = new CFSFolder;
    CMyComPtr<IFolderFolder> folderNew = fsFolderSpec;
    fsFolderSpec->Init(_path);
    *resultFolder = folderNew.Detach();
    return S_OK;
}

void CFSFolder::AddPath(const QString &path) {
    const QFileInfo info(path);
    CDirItem item;
    if (!item.Find(FsPath(path), false))
        item.Name = FsPath(info.fileName());
    const QString relative = QDir(QtPath(_path)).relativeFilePath(info.absolutePath());
    if (relative != ".")
        item.Parent = Folders.Add(FsPath(relative + '/'));
    Files.Add(item);
}

FString CFSFolder::GetRelPath(const CDirItem &item) const {
    if (item.Parent < 0)
        return item.Name;
    return Folders[item.Parent] + item.Name;
}

void CFSFolder::GetAbsPath(const wchar_t *name, FString &absPath) const {
    absPath.Empty();
    if (!NWindows::NFile::NName::IsAbsolutePath(name))
        absPath += _path;
    absPath += us2fs(name);
}

Z7_COM7F_IMF(CFSFolder::CreateFolder(const wchar_t *name, IProgress * /* progress */)) {
    FString absPath;
    GetAbsPath(name, absPath);
    if (NWindows::NFile::NDir::CreateDir(absPath))
        return S_OK;
#ifdef _WIN32
    if (::GetLastError() != ERROR_ALREADY_EXISTS)
#else
    if (errno != EEXIST)
#endif
        if (NWindows::NFile::NDir::CreateComplexDir(absPath))
            return S_OK;
    return GetLastError_noZero_HRESULT();
}

Z7_COM7F_IMF(CFSFolder::CreateFile(const wchar_t *name, IProgress * /* progress */)) {
    FString absPath;
    GetAbsPath(name, absPath);
    NWindows::NFile::NIO::COutFile outFile;
    if (!outFile.Create_NEW(absPath))
        return GetLastError_noZero_HRESULT();
    return S_OK;
}

Z7_COM7F_IMF(CFSFolder::Rename(UInt32 index, const wchar_t *newName, IProgress * /* progress */)) {
    if (index >= Files.Size())
        return E_NOTIMPL;
    const CDirItem &fi = Files[index];
    FString fullPrefix = _path;
    if (fi.Parent >= 0)
        fullPrefix += Folders[fi.Parent];
    const FString sourcePath = fullPrefix + fi.Name;
    const FString targetPath = fullPrefix + us2fs(newName);
    const QFileInfo source(QtPath(sourcePath)), target(QtPath(targetPath));
    if (source.fileName() == target.fileName()) {
        const QString sourceParent = QFileInfo(source.path()).canonicalFilePath();
        if (!sourceParent.isEmpty() &&
            sourceParent == QFileInfo(target.path()).canonicalFilePath() &&
            (source.exists() || source.isSymLink()))
            return S_OK;
    }
#ifdef _WIN32
    if (!NWindows::NFile::NDir::MyMoveFile(sourcePath, targetPath))
#else
    if (!QtRenameFileSystemEntryWithoutReplacing(QtPath(sourcePath), QtPath(targetPath)))
#endif
        return GetLastError_noZero_HRESULT();
    return S_OK;
}

Z7_COM7F_IMF(CFSFolder::Delete(const UInt32 *indices, UInt32 numItems, IProgress *progress)) {
    if (!progress)
        return E_INVALIDARG;
    RINOK(progress->SetTotal(numItems))
    for (UInt32 i = 0; i < numItems; i++) {
        UInt32 index = indices[i];
        if (index >= Files.Size())
            return E_INVALIDARG;
        bool result = true;
        {
            const CDirItem &fi = Files[index];
            const FString fullPath = _path + GetRelPath(fi);
            const QString path = QtPath(fullPath);
            if (CheckBreak && !CheckBreak(path))
                return E_ABORT;
            if (fi.IsOsSymLink())
                result = QtRemoveFileSystemEntry(path);
            else if (fi.IsDir()) {
                bool canceled = false;
                result =
                    NWindows::NFile::NDir::RemoveDirWithSubItems(path, [&](const QString &item) {
                        if (CheckBreak && !CheckBreak(item)) {
                            canceled = true;
                            return false;
                        }
                        return true;
                    });
                if (canceled)
                    return E_ABORT;
            } else
                result = NWindows::NFile::NDir::DeleteFileAlways(path);
        }
        if (!result) {
            ErrorMessage = QCoreApplication::translate("FSFolder", "Could not delete: ") +
                           QtPath(_path + GetRelPath(Files[index]));
            return GetLastError_noZero_HRESULT();
        }
        const UInt64 completed = i;
        RINOK(progress->SetCompleted(&completed))
    }
    return S_OK;
}

// CopyTo is implemented with the upstream copy state in FSFolderCopy.cpp.
// These two unavailable bodies are also the original upstream implementation.
Z7_COM7F_IMF(CFSFolder::CopyFrom(Int32, const wchar_t *, const wchar_t *const *, UInt32,
                                 IProgress *)) {
    return E_NOTIMPL;
}
Z7_COM7F_IMF(CFSFolder::CopyFromFile(UInt32, const wchar_t *, IProgress *)) { return E_NOTIMPL; }
} // namespace NFsFolder
