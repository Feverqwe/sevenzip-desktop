// Adapted from 7-Zip CPP/7zip/UI/FileManager/FSFolder.cpp (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Source enumeration and properties use portable upstream filesystem services.
// Qt mutation services/callbacks remain a narrow integration boundary.
#include "filemanager/providers/FSFolder.h"
#include "7zip/PropID.h"
#include "Common/ComTry.h"
#include "Common/Defs.h"
#include "Common/StringConvert.h"
#include "Common/UTFConvert.h"
#include "Windows/FileIO.h"
#include "Windows/FileName.h"
#include "Windows/PropVariant.h"
#include "Windows/TimeUtils.h"
#include "filemanager/SysIconUtils.h"
#include "filemanager/panels/PanelSort.h"
#include "filemanager/providers/FSDrives.h"
#include "fileops/links/qtlinkservices.h"
#include "fileops/native/FileDir.h"
#include "localization/qtlanguageservices.h"
#include "platform/loader/DLL.h"
#include <QDir>
#include <QFile>
#ifndef _WIN32
#include <sys/stat.h>
#endif

#ifdef _WIN32
#ifdef __MINGW32_VERSION
#include <ddk/ntddk.h>
#elif !defined(Z7_OLD_WIN_SDK) && !defined(_M_IA64)
#include <winternl.h>
#endif
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0501
#ifdef _APISETFILE_
EXTERN_C_BEGIN
WINBASEAPI DWORD WINAPI GetCompressedFileSizeW(LPCWSTR lpFileName, LPDWORD lpFileSizeHigh);
EXTERN_C_END
#endif
#endif
#if defined(_WIN32_WINNT) && (_WIN32_WINNT >= 0x0500) && !defined(_M_IA64)
#define Z7_WIN_NTSTATUS NTSTATUS
#define Z7_WIN_IO_STATUS_BLOCK IO_STATUS_BLOCK
#else
typedef LONG Z7_WIN_NTSTATUS;
typedef struct {
    union {
        Z7_WIN_NTSTATUS Status;
        PVOID Pointer;
    } DUMMYUNIONNAME;
    ULONG_PTR Information;
} Z7_WIN_IO_STATUS_BLOCK;
#endif
#endif

#ifndef USE_UNICODE_FSTRING
int CompareFileNames_ForFolderList(const FChar *s1, const FChar *s2);
int CompareFileNames_ForFolderList(const FChar *s1, const FChar *s2) {
    return CompareFileNames_ForFolderList(fs2us(s1), fs2us(s2));
}
#endif

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

HRESULT CFsFolderStat::Enumerate() {
    if (Progress)
        RINOK(Progress->SetCompleted(NULL))
    Path.Add_PathSepar();
    const unsigned len = Path.Len();
    NWindows::NFile::NFind::CEnumerator enumerator;
    enumerator.SetDirPrefix(Path);
    NWindows::NFile::NFind::CFileInfo fi;
#ifndef _WIN32
    NWindows::NFile::NFind::CDirEntry entry;
#endif
    for (;;) {
#ifdef _WIN32
        if (!enumerator.Next(fi))
            break;
#else
        bool found;
        if (!enumerator.Next(entry, found))
            break;
        if (!found)
            break;
        if (!enumerator.Fill_FileInfo(entry, fi, false))
            return E_FAIL;
#endif
        if (fi.IsDir() && !fi.IsOsSymLink()) {
            NumFolders++;
            Path.DeleteFrom(len);
            Path += fi.Name;
            RINOK(Enumerate())
        } else {
            NumFiles++;
            Size += fi.Size;
        }
    }
    return S_OK;
}

#ifndef UNDER_CE
bool MyGetCompressedFileSizeW(CFSTR path, UInt64 &size) {
#ifdef _WIN32
    using namespace NWindows::NFile::NName;
    DWORD highPart;
    DWORD lowPart = INVALID_FILE_SIZE;
    IF_USE_MAIN_PATH {
        lowPart = ::GetCompressedFileSizeW(fs2us(path), &highPart);
        if (lowPart != INVALID_FILE_SIZE || ::GetLastError() == NO_ERROR) {
            size = ((UInt64)highPart << 32) | lowPart;
            return true;
        }
    }
#ifdef Z7_LONG_PATH
    if (USE_SUPER_PATH) {
        UString superPath;
        if (GetSuperPath(path, superPath, USE_MAIN_PATH)) {
            lowPart = ::GetCompressedFileSizeW(superPath, &highPart);
            if (lowPart != INVALID_FILE_SIZE || ::GetLastError() == NO_ERROR) {
                size = ((UInt64)highPart << 32) | lowPart;
                return true;
            }
        }
    }
#endif
#else
    // The source Windows compressed-allocation API has no POSIX equivalent.
    // Report unavailable so its original fi.Size fallback remains authoritative.
    (void)path;
    (void)size;
#endif
    return false;
}
#endif

CFSFolder::CFSFolder(QString path) {
    FString prefix = FsPath(QDir::cleanPath(std::move(path)));
    NWindows::NFile::NName::NormalizeDirPathPrefix(prefix);
    Init(prefix);
}

HRESULT CFSFolder::Init(const FString &path) {
    _path = path;
#ifdef _WIN32
    _findChangeNotification.FindFirst(_path, false,
                                      FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                          FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SIZE |
                                          FILE_NOTIFY_CHANGE_LAST_WRITE);
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
            // Original Windows Next(fi) ends the walk on an enumeration error.
            if (!enumerator.Next(entry, found) || !found)
                break;
            if (!enumerator.Fill_FileInfo(entry, fi, false))
                return E_FAIL;
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
    Int32 dummy;
    WasChanged(&dummy);
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

static CFSTR const kDescriptionFileName = FTEXT("descript.ion");

bool CFSFolder::LoadComments() {
    _comments.Clear();
    _commentsAreLoaded = true;
    NWindows::NFile::NIO::CInFile file;
    if (!file.Open(_path + kDescriptionFileName))
        return false;
    UInt64 len;
    if (!file.GetLength(len))
        return false;
    if (len >= (1 << 28))
        return false;
    AString s;
    char *p = s.GetBuf((unsigned)(size_t)len);
    size_t processedSize;
    if (!file.ReadFull(p, (unsigned)(size_t)len, processedSize))
        return false;
    s.ReleaseBuf_CalcLen((unsigned)(size_t)len);
    if (processedSize != len)
        return false;
    file.Close();
    UString unicodeString;
    if (!ConvertUTF8ToUnicode(s, unicodeString))
        return false;
    return _comments.ReadFromString(unicodeString);
}

bool CFSFolder::SaveComments() {
    AString utf;
    {
        UString unicode;
        _comments.SaveToString(unicode);
        ConvertUnicodeToUTF8(unicode, utf);
    }
    if (!utf.IsAscii())
        utf.Insert(0, "\xEF\xBB\xBF"
                      "\r\n");

    FString path = _path + kDescriptionFileName;
    NWindows::NFile::NIO::COutFile file;
#ifdef _WIN32
    // We must set same attrib. COutFile::CreateAlways can fail, if file has another attrib.
    DWORD attrib = FILE_ATTRIBUTE_NORMAL;
    {
        NWindows::NFile::NFind::CFileInfo fi;
        if (fi.Find(path))
            attrib = fi.Attrib;
    }
    if (!file.Create_ALWAYS_with_Attribs(path, attrib))
        return false;
    UInt32 processed;
    file.Write(utf, utf.Len(), processed);
#else
    // The portable source writer keeps existing POSIX permissions on truncation.
    if (!file.Create_ALWAYS(path))
        return false;
    size_t processed;
    file.write_full(utf, utf.Len(), processed);
#endif
    _commentsAreLoaded = false;
    return true;
}

bool CFSFolder::ReadFileInfo(CDirItem &di) {
    di.FileInfo_WasRequested = true;
#ifdef _WIN32
    BY_HANDLE_FILE_INFORMATION info;
    memset(&info, 0, sizeof(info));
    if (!NWindows::NFile::NIO::CFileBase::GetFileInformation(_path + GetRelPath(di), &info))
        return false;
    di.NumLinks = info.nNumberOfLinks;
    di.FileIndex = (((UInt64)info.nFileIndexHigh) << 32) + info.nFileIndexLow;
#else
    struct stat info;
    const FString path = _path + GetRelPath(di);
    if (::lstat(path.Ptr(), &info) != 0)
        return false;
    di.NumLinks = static_cast<UInt32>(info.st_nlink);
    di.FileIndex = static_cast<UInt64>(info.st_ino);
#endif
    di.FileInfo_Defined = true;
    return true;
}

void CFSFolder::ReadChangeTime(CDirItem &di) {
    di.ChangeTime_WasRequested = true;
#ifdef _WIN32
    typedef Z7_WIN_NTSTATUS(WINAPI * Func_NtQueryInformationFile)(
        HANDLE handle, Z7_WIN_IO_STATUS_BLOCK * io, void *ptr, LONG len,
        Z7_WIN_FILE_INFORMATION_CLASS cls);
    static Func_NtQueryInformationFile f_NtQueryInformationFile;
    static bool wasRequested = false;
    if (!wasRequested) {
        wasRequested = true;
        f_NtQueryInformationFile =
            Z7_GET_PROC_ADDRESS(Func_NtQueryInformationFile, ::GetModuleHandleW(L"ntdll.dll"),
                                "NtQueryInformationFile");
    }
    if (!f_NtQueryInformationFile)
        return;
    NWindows::NFile::NIO::CInFile file;
    if (!file.Open_for_ReadAttributes(_path + GetRelPath(di)))
        return;
    Z7_WIN_FILE_BASIC_INFORMATION fbi;
    Z7_WIN_IO_STATUS_BLOCK ioStatusBlock;
    const Z7_WIN_NTSTATUS status = f_NtQueryInformationFile(
        file.GetHandle(), &ioStatusBlock, &fbi, sizeof(fbi), Z7_WIN_FileBasicInformation);
    if (status != 0 || ioStatusBlock.Information != sizeof(fbi))
        return;
    di.ChangeTime.dwLowDateTime = fbi.ChangeTime.u.LowPart;
    di.ChangeTime.dwHighDateTime = (DWORD)fbi.ChangeTime.u.HighPart;
    di.ChangeTime_Defined = true;
#else
    struct stat info;
    const FString path = _path + GetRelPath(di);
    if (::lstat(path.Ptr(), &info) != 0)
        return;
    di.ChangeTime = ST_CTIME(info);
    di.ChangeTime_Defined = true;
#endif
}

Z7_COM7F_IMF(CFSFolder::GetNumberOfItems(UInt32 *numItems)) {
    *numItems = Files.Size();
    return S_OK;
}

Z7_COM7F_IMF(CFSFolder::CalcItemFullSize(UInt32 index, IProgress *progress)) {
    if (index >= Files.Size())
        return S_OK;
    CDirItem &fi = Files[index];
    if (!fi.IsDir() || fi.IsOsSymLink())
        return S_OK;
    CFsFolderStat stat(_path + GetRelPath(fi), progress);
    RINOK(stat.Enumerate())
    fi.Size = stat.Size;
    fi.NumFolders = stat.NumFolders;
    fi.NumFiles = stat.NumFiles;
    fi.FolderStat_Defined = true;
    return S_OK;
}

#ifdef USE_UNICODE_FSTRING
Z7_COM7F_IMF(CFSFolder::GetItemPrefix(UInt32 index, const wchar_t **name, unsigned *len)) {
    *name = NULL;
    *len = 0;
    /*
    if (index >= Files.Size())
      index = Streams[index - Files.Size()].Parent;
    */
    CDirItem &fi = Files[index];
    if (fi.Parent >= 0) {
        const FString &fo = Folders[fi.Parent];
        USE_UNICODE_FSTRING
        *name = fo;
        *len = fo.Len();
    }
    return S_OK;
}

Z7_COM7F_IMF(CFSFolder::GetItemName(UInt32 index, const wchar_t **name, unsigned *len)) {
    *name = NULL;
    *len = 0;
    if (index < Files.Size()) {
        CDirItem &fi = Files[index];
        *name = fi.Name;
        *len = fi.Name.Len();
        return S_OK;
    } else {
        // const CAltStream &ss = Streams[index - Files.Size()];
        // *name = ss.Name;
        // *len = ss.Name.Len();
        //
        // change it;
    }
    return S_OK;
}

Z7_COM7F_IMF2(UInt64, CFSFolder::GetItemSize(UInt32 index)) {
    /*
    if (index >= Files.Size())
      return Streams[index - Files.Size()].Size;
    */
    CDirItem &fi = Files[index];
    return fi.IsDir() ? 0 : fi.Size;
}
#endif

Z7_COM7F_IMF(CFSFolder::GetSystemIconIndex(UInt32 index, Int32 *iconIndex)) {
    *iconIndex = -1;
    if (index >= Files.Size())
        return E_INVALIDARG;
    const CDirItem &fi = Files[index];
#ifdef _WIN32
    return Shell_GetFileInfo_SysIconIndex_for_Path_return_HRESULT(_path + GetRelPath(fi), fi.Attrib,
                                                                  iconIndex);
#else
    // Qt transports the platform image-list index for the original path query.
    *iconIndex =
        g_Ext_to_Icon_Map.GetPathIconIndex(fs2us(_path + GetRelPath(fi)), fi.GetWinAttrib());
    return *iconIndex >= 0 ? S_OK : E_FAIL;
#endif
}

Z7_COM7F_IMF(CFSFolder::SetFlatMode(Int32 flatMode)) {
    _flatMode = IntToBool(flatMode);
    return S_OK;
}

// ---------- IArchiveGetRawProps (original FSFolder source bodies) ----------
Z7_COM7F_IMF(CFSFolder::GetNumRawProps(UInt32 *numProps)) {
#ifdef _WIN32
    *numProps = 1;
#else
    // A POSIX readlink string is not an NT reparse record.
    *numProps = 0;
#endif
    return S_OK;
}
Z7_COM7F_IMF(CFSFolder::GetRawPropInfo(UInt32 /* index */, BSTR *name, PROPID *propID)) {
    *name = NULL;
    *propID = kpidNtReparse;
    return S_OK;
}
Z7_COM7F_IMF(CFSFolder::GetParent(UInt32 /* index */, UInt32 * /* parent */,
                                  UInt32 * /* parentType */)) {
    return E_FAIL;
}
Z7_COM7F_IMF(CFSFolder::GetRawProp(UInt32 index, PROPID propID, const void **data, UInt32 *dataSize,
                                   UInt32 *propType)) {
    *data = NULL;
    *dataSize = 0;
    *propType = 0;
    if (index >= Files.Size())
        return E_INVALIDARG;
    if (propID == kpidNtReparse) {
        const CDirItem &fi = Files[index];
        const CByteBuffer &buf = fi.Reparse;
        if (buf.Size() == 0)
            return S_OK;
        *data = buf;
        *dataSize = (UInt32)buf.Size();
        *propType = NPropDataType::kRaw;
        return S_OK;
    }
    return S_OK;
}

Z7_COM7F_IMF(CFSFolder::GetProperty(UInt32 index, PROPID propID, PROPVARIANT *value)) {
    COM_TRY_BEGIN
    if (index >= Files.Size())
        return E_INVALIDARG;
    NWindows::NCOM::CPropVariant prop;
    CDirItem &fi = Files[index];
    switch (propID) {
    case kpidIsDir:
        prop = fi.IsDir();
        break;
    case kpidIsAltStream:
        prop = false;
        break;
    case kpidName:
        prop = fs2us(fi.Name);
        break;
    case kpidSize:
        if (!fi.IsDir() || fi.FolderStat_Defined)
            prop = fi.Size;
        break;
    case kpidPackSize:
#ifdef UNDER_CE
        prop = fi.Size;
#else
        if (!fi.PackSize_Defined) {
            fi.PackSize_Defined = true;
            if (fi.IsDir() || !MyGetCompressedFileSizeW(_path + GetRelPath(fi), fi.PackSize))
                fi.PackSize = fi.Size;
        }
        prop = fi.PackSize;
#endif
        break;
    case kpidAttrib:
        prop = (UInt32)fi.GetWinAttrib();
        break;
    case kpidCTime:
        PropVariant_SetFrom_FiTime(prop, fi.CTime);
        break;
    case kpidATime:
        PropVariant_SetFrom_FiTime(prop, fi.ATime);
        break;
    case kpidMTime:
        PropVariant_SetFrom_FiTime(prop, fi.MTime);
        break;
    case kpidLinks:
        if (!fi.FileInfo_WasRequested)
            ReadFileInfo(fi);
        if (fi.FileInfo_Defined)
            prop = fi.NumLinks;
        break;
    case kpidINode:
        if (!fi.FileInfo_WasRequested)
            ReadFileInfo(fi);
        if (fi.FileInfo_Defined)
            prop = fi.FileIndex;
        break;
    case kpidChangeTime:
        if (!fi.ChangeTime_WasRequested)
            ReadChangeTime(fi);
        if (fi.ChangeTime_Defined)
            PropVariant_SetFrom_FiTime(prop, fi.ChangeTime);
        break;
    case kpidComment: {
        if (!_commentsAreLoaded)
            LoadComments();
        UString comment;
        if (_comments.GetValue(fs2us(GetRelPath(fi)), comment)) {
            int pos = comment.Find((wchar_t)4);
            if (pos >= 0)
                comment.DeleteFrom((unsigned)pos);
            prop = comment;
        }
        break;
    }
    case kpidPrefix:
        if (fi.Parent >= 0)
            prop = fs2us(Folders[fi.Parent]);
        break;
    case kpidNumSubDirs:
        if (fi.IsDir() && fi.FolderStat_Defined)
            prop = fi.NumFolders;
        break;
    case kpidNumSubFiles:
        if (fi.IsDir() && fi.FolderStat_Defined)
            prop = fi.NumFiles;
        break;
    }
    prop.Detach(value);
    return S_OK;
    COM_TRY_END
}

static const Byte kProps[] = {kpidName,       kpidSize,       kpidMTime,       kpidCTime, kpidATime,
                              kpidChangeTime, kpidAttrib,     kpidPackSize,    kpidINode, kpidLinks,
                              kpidComment,    kpidNumSubDirs, kpidNumSubFiles, kpidPrefix};

Z7_COM7F_IMF(CFSFolder::GetNumberOfProperties(UInt32 *numProperties)) {
    *numProperties = Z7_ARRAY_SIZE(kProps);
    if (!_flatMode)
        (*numProperties)--;
    return S_OK;
}

IMP_IFolderFolder_GetProp(CFSFolder::GetPropertyInfo, kProps)

    Z7_COM7F_IMF(CFSFolder::GetFolderProperty(PROPID propID, PROPVARIANT *value)) {
    COM_TRY_BEGIN
    NWindows::NCOM::CPropVariant prop;
    switch (propID) {
    case kpidType:
        prop = "FSFolder";
        break;
    case kpidPath:
        prop = fs2us(_path);
        break;
    }
    prop.Detach(value);
    return S_OK;
    COM_TRY_END
}

static inline CFSTR GetExtensionPtr(const FString &name) {
    const int dotPos = name.ReverseFind_Dot();
    return name.Ptr((dotPos < 0) ? name.Len() : (unsigned)dotPos);
}

Z7_COM7F_IMF2(Int32, CFSFolder::CompareItems(UInt32 index1, UInt32 index2, PROPID propID,
                                             Int32 /* propIsRaw */)) {
    /*
    const CAltStream *ss1 = NULL;
    const CAltStream *ss2 = NULL;
    if (index1 >= Files.Size()) { ss1 = &Streams[index1 - Files.Size()]; index1 = ss1->Parent; }
    if (index2 >= Files.Size()) { ss2 = &Streams[index2 - Files.Size()]; index2 = ss2->Parent; }
    */
    CDirItem &fi1 = Files[index1];
    CDirItem &fi2 = Files[index2];

    switch (propID) {
    case kpidName: {
        // Retain the source comparison while converting each loaded name once.
        const auto key = [](CDirItem &item) {
            if (!item.NameSortKey)
                item.NameSortKey = QString::fromWCharArray(fs2us(item.Name)).toStdU16String();
            return item.NameSortKey->c_str();
        };
        const int comp = CompareFileNames_ForFolderList(key(fi1), key(fi2));
        /*
        if (comp != 0)
          return comp;
        if (!ss1)
          return ss2 ? -1 : 0;
        if (!ss2)
          return 1;
        return MyStringCompareNoCase(ss1->Name, ss2->Name);
        */
        return comp;
    }
    case kpidSize:
        return MyCompare(
            /* ss1 ? ss1->Size : */ fi1.Size,
            /* ss2 ? ss2->Size : */ fi2.Size);
    case kpidAttrib:
        return MyCompare(fi1.GetWinAttrib(), fi2.GetWinAttrib());
    case kpidCTime:
        return Compare_FiTime(&fi1.CTime, &fi2.CTime);
    case kpidATime:
        return Compare_FiTime(&fi1.ATime, &fi2.ATime);
    case kpidMTime:
        return Compare_FiTime(&fi1.MTime, &fi2.MTime);
    case kpidIsDir: {
        const bool isDir1 = /* ss1 ? false : */ fi1.IsDir();
        const bool isDir2 = /* ss2 ? false : */ fi2.IsDir();
        if (isDir1 == isDir2)
            return 0;
        return isDir1 ? -1 : 1;
    }
    case kpidPackSize: {
#ifdef UNDER_CE
        return MyCompare(fi1.Size, fi2.Size);
#else
        // PackSize can be undefined here
        return MyCompare(
            /* ss1 ? ss1->PackSize : */ fi1.PackSize,
            /* ss2 ? ss2->PackSize : */ fi2.PackSize);
#endif
    }

#ifdef FS_SHOW_LINKS_INFO
    case kpidINode: {
#ifndef UNDER_CE
        if (!fi1.FileInfo_WasRequested)
            ReadFileInfo(fi1);
        if (!fi2.FileInfo_WasRequested)
            ReadFileInfo(fi2);
        return MyCompare(fi1.FileIndex, fi2.FileIndex);
#endif
    }
    case kpidLinks: {
#ifndef UNDER_CE
        if (!fi1.FileInfo_WasRequested)
            ReadFileInfo(fi1);
        if (!fi2.FileInfo_WasRequested)
            ReadFileInfo(fi2);
        return MyCompare(fi1.NumLinks, fi2.NumLinks);
#endif
    }
#endif

    case kpidComment: {
        // change it !
        UString comment1, comment2;
        _comments.GetValue(fs2us(GetRelPath(fi1)), comment1);
        _comments.GetValue(fs2us(GetRelPath(fi2)), comment2);
        return MyStringCompareNoCase(comment1, comment2);
    }
    case kpidPrefix:
        if (fi1.Parent == fi2.Parent)
            return 0;
        if (fi1.Parent < 0)
            return -1;
        if (fi2.Parent < 0)
            return 1;
        return CompareFileNames_ForFolderList(Folders[fi1.Parent], Folders[fi2.Parent]);
    case kpidExtension:
        return CompareFileNames_ForFolderList(GetExtensionPtr(fi1.Name), GetExtensionPtr(fi2.Name));
    }

    return 0;
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
        _findChangeNotification.FindNext();
        wasChangedMain = true;
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

void CFSFolder::GetAbsPath(const QString &name, QString &absPath) const {
    FString sourcePath;
    GetAbsPath(name.toStdWString().c_str(), sourcePath);
    absPath = QtPath(sourcePath);
}

bool CFSFolder::CreateFolder(const QString &name) {
    if (CreateFolder(name.toStdWString().c_str(), nullptr) == S_OK)
        return true;
    QString absPath;
    GetAbsPath(name, absPath);
    ErrorMessage =
        QCoreApplication::translate("FSFolder", "Could not create folder: %1").arg(absPath);
    return false;
}

bool CFSFolder::CreateFile(const QString &name) {
    if (CreateFile(name.toStdWString().c_str(), nullptr) != S_OK) {
        QString absPath;
        GetAbsPath(name, absPath);
        ErrorMessage =
            QCoreApplication::translate("FSFolder", "Could not create file: %1").arg(absPath);
        return false;
    }
    return true;
}

bool CFSFolder::Rename(UInt32 index, const QString &newName) {
    if (Rename(index, newName.toStdWString().c_str(), nullptr) != S_OK) {
        ErrorMessage =
            QCoreApplication::translate("FSFolder", "Could not rename item: %1").arg(newName);
        return false;
    }
    return true;
}

class CMutationProgress final : public IProgress, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_1(IProgress)
    CFSFolder &folder_;

  public:
    explicit CMutationProgress(CFSFolder &folder) : folder_(folder) {}
    Z7_COM7F_IMF(SetTotal(UInt64 total)) override {
        return !folder_.SetTotal || folder_.SetTotal(total) ? S_OK : E_ABORT;
    }
    Z7_COM7F_IMF(SetCompleted(const UInt64 *completed)) override {
        return !folder_.SetCompleted || !completed || folder_.SetCompleted(*completed) ? S_OK
                                                                                       : E_ABORT;
    }
};

bool CFSFolder::Delete(const UInt32 *indices, UInt32 numItems) {
    ErrorMessage.clear();
    CMyComPtr<IProgress> progress = new CMutationProgress(*this);
    if (Delete(indices, numItems, progress) == S_OK)
        return true;
    if (ErrorMessage.isEmpty())
        ErrorMessage = QCoreApplication::translate("FSFolder", "Could not delete selected items.");
    return false;
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
#ifdef _WIN32
    if (!NWindows::NFile::NDir::MyMoveFile(fullPrefix + fi.Name, fullPrefix + us2fs(newName)))
#else
    if (!QtRenameFileSystemEntryWithoutReplacing(QtPath(fullPrefix + fi.Name),
                                                 QtPath(fullPrefix + us2fs(newName))))
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
Z7_COM7F_IMF(CFSFolder::SetProperty(UInt32 index, PROPID propID, const PROPVARIANT *value,
                                    IProgress * /* progress */)) {
    if (index >= Files.Size())
        return E_INVALIDARG;
    CDirItem &fi = Files[index];
    if (fi.Parent >= 0)
        return E_NOTIMPL;
    switch (propID) {
    case kpidComment: {
        UString filename = fs2us(fi.Name);
        filename.Trim();
        if (value->vt == VT_EMPTY)
            _comments.DeletePair(filename);
        else if (value->vt == VT_BSTR) {
            CTextPair pair;
            pair.ID = filename;
            pair.ID.Trim();
            pair.Value.SetFromBstr(value->bstrVal);
            pair.Value.Trim();
            if (pair.Value.IsEmpty())
                _comments.DeletePair(filename);
            else
                _comments.AddPair(pair);
        } else
            return E_INVALIDARG;
        SaveComments();
        break;
    }
    default:
        return E_NOTIMPL;
    }
    return S_OK;
}
// These two unavailable bodies are also the original upstream implementation.
Z7_COM7F_IMF(CFSFolder::CopyFrom(Int32, const wchar_t *, const wchar_t *const *, UInt32,
                                 IProgress *)) {
    return E_NOTIMPL;
}
Z7_COM7F_IMF(CFSFolder::CopyFromFile(UInt32, const wchar_t *, IProgress *)) { return E_NOTIMPL; }
} // namespace NFsFolder
