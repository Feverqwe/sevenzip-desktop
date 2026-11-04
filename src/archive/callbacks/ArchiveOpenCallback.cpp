// ArchiveOpenCallback.cpp
// Copied from 7-Zip 9128b80e3a471108678e2b3ec3c985861c8d7b0f.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Adaptations: include owners and optional typed fscrypt key callback.

#include "7zip/UI/Common/StdAfx.h"

#include "Common/ComTry.h"

#include "Windows/FileName.h"
#include "Windows/PropVariant.h"
#include "Windows/System.h"

#include "7zip/Common/StreamUtils.h"

#include "archive/callbacks/ArchiveOpenCallback.h"
#include "archive/opening/archivecontainerpath.h"
#include <cerrno>

// #define DEBUG_VOLUMES

#ifdef DEBUG_VOLUMES
#include <stdio.h>
#endif

#ifdef DEBUG_VOLUMES
#define PRF(x) x
#else
#define PRF(x)
#endif

using namespace NWindows;

HRESULT COpenCallbackImp::Init2(const FString &folderPrefix, const FString &fileName) {
    Volumes.Init();
    FileNames.Clear();
    FileNames_WasUsed.Clear();
    FileSizes.Clear();
    _subArchiveMode = false;
    UString packageFolder = fs2us(folderPrefix);
    if (!packageFolder.IsEmpty() && IS_PATH_SEPAR(packageFolder.Back()))
        packageFolder.DeleteBack();
    _delayedPackageVolumes =
        IsSparseBundleName(fs2us(fileName)) ||
        (fs2us(fileName).IsEqualTo_NoCase(L"Info.plist") && IsSparseBundleName(packageFolder));
    _packageVolumePrefix.Empty();
    _packageRoot = folderPrefix;
    if (IsSparseBundleName(fs2us(fileName))) {
        _packageVolumePrefix = fs2us(fileName);
        _packageVolumePrefix += L'/';
        _packageRoot += fileName;
        _packageRoot.Add_PathSepar();
    }
    // TotalSize = 0;
    PasswordWasAsked = false;
    _folderPrefix = folderPrefix;
    if (!_fileInfo.Find_FollowLink(_folderPrefix + fileName)) {
        // throw 20121118;
        return GetLastError_noZero_HRESULT();
    }
    return S_OK;
}

Z7_COM7F_IMF(COpenCallbackImp::SetSubArchiveName(const wchar_t *name)) {
    _subArchiveMode = true;
    _subArchiveName = name;
    // TotalSize = 0;
    return S_OK;
}

Z7_COM7F_IMF(COpenCallbackImp::SetTotal(const UInt64 *files, const UInt64 *bytes)) {
    COM_TRY_BEGIN
    if (ReOpenCallback)
        return ReOpenCallback->SetTotal(files, bytes);
    if (!Callback)
        return S_OK;
    return Callback->Open_SetTotal(files, bytes);
    COM_TRY_END
}

Z7_COM7F_IMF(COpenCallbackImp::SetCompleted(const UInt64 *files, const UInt64 *bytes)) {
    COM_TRY_BEGIN
    if (ReOpenCallback)
        return ReOpenCallback->SetCompleted(files, bytes);
    if (!Callback)
        return S_OK;
    return Callback->Open_SetCompleted(files, bytes);
    COM_TRY_END
}

Z7_COM7F_IMF(COpenCallbackImp::GetProperty(PROPID propID, PROPVARIANT *value)) {
    COM_TRY_BEGIN
    NCOM::CPropVariant prop;
    if (_subArchiveMode)
        switch (propID) {
        case kpidName:
            prop = _subArchiveName;
            break;
        // case kpidSize:  prop = _subArchiveSize; break; // we don't use it now
        default:
            break;
        }
    else
        switch (propID) {
        case kpidName:
            prop = fs2us(_fileInfo.Name);
            break;
        case kpidIsDir:
            prop = _fileInfo.IsDir();
            break;
        case kpidSize:
            prop = _fileInfo.Size;
            break;
        case kpidAttrib:
            prop = (UInt32)_fileInfo.GetWinAttrib();
            break;
        case kpidPosixAttrib:
            prop = (UInt32)_fileInfo.GetPosixAttrib();
            break;
        case kpidCTime:
            PropVariant_SetFrom_FiTime(prop, _fileInfo.CTime);
            break;
        case kpidATime:
            PropVariant_SetFrom_FiTime(prop, _fileInfo.ATime);
            break;
        case kpidMTime:
            PropVariant_SetFrom_FiTime(prop, _fileInfo.MTime);
            break;
        default:
            break;
        }
    prop.Detach(value);
    return S_OK;
    COM_TRY_END
}

// ---------- CInFileStreamVol ----------

Z7_class_final(CInFileStreamVol) : public IInStream, public IStreamGetSize, public CMyUnknownImp {
    Z7_IFACES_IMP_UNK_3(IInStream, ISequentialInStream, IStreamGetSize)
  public:
    unsigned FileIndex;
    COpenCallbackImp *OpenCallbackImp;
    CMyComPtr<IArchiveOpenCallback> OpenCallbackRef;

    HRESULT EnsureOpen() { return OpenCallbackImp->Volumes.EnsureOpen(FileIndex); }

    ~CInFileStreamVol() {
        if (OpenCallbackRef)
            OpenCallbackImp->AtCloseFile(FileIndex);
    }
};

void CMultiStreams::InsertToList(unsigned index) {
    {
        CSubStream &s = Streams[index];
        s.Next = Head;
        s.Prev = -1;
    }
    if (Head != -1)
        Streams[(unsigned)Head].Prev = (int)index;
    else {
        // if (Tail != -1) throw 1;
        Tail = (int)index;
    }
    Head = (int)index;
    NumListItems++;
}

// s must bee in List
void CMultiStreams::RemoveFromList(CSubStream &s) {
    if (s.Next != -1)
        Streams[(unsigned)s.Next].Prev = s.Prev;
    else
        Tail = s.Prev;
    if (s.Prev != -1)
        Streams[(unsigned)s.Prev].Next = s.Next;
    else
        Head = s.Next;
    s.Next = -1; // optional
    s.Prev = -1; // optional
    NumListItems--;
}

void CMultiStreams::CloseFile(unsigned index) {
    CSubStream &s = Streams[index];
    if (s.Stream) {
        s.Stream.Release();
        RemoveFromList(s);
        // s.InFile->Close();
        // s.IsOpen = false;
#ifdef DEBUG_VOLUMES
        static int numClosing = 0;
        numClosing++;
        printf("\nCloseFile %u, total_closes = %u, num_open_files = %u\n", index, numClosing,
               NumListItems);
#endif
    }
}

void CMultiStreams::Init() {
    Head = -1;
    Tail = -1;
    NumListItems = 0;
    Streams.Clear();
}

CMultiStreams::CMultiStreams() : Head(-1), Tail(-1), NumListItems(0) {
    NumOpenFiles_AllowedMax = NSystem::Get_File_OPEN_MAX_Reduced_for_3_tasks();
    PRF(printf("\nNumOpenFiles_Limit = %u\n", NumOpenFiles_AllowedMax));
}

HRESULT CMultiStreams::PrepareToOpenNew() {
    if (NumListItems < NumOpenFiles_AllowedMax)
        return S_OK;
    if (Tail == -1)
        return E_FAIL;
    CMultiStreams::CSubStream &tailStream = Streams[(unsigned)Tail];
    RINOK(InStream_GetPos(tailStream.Stream, tailStream.LocalPos))
    CloseFile((unsigned)Tail);
    return S_OK;
}

HRESULT CMultiStreams::EnsureOpen(unsigned index) {
    CMultiStreams::CSubStream &s = Streams[index];
    if (s.Stream) {
        if ((int)index != Head) {
            RemoveFromList(s);
            InsertToList(index);
        }
    } else {
        RINOK(PrepareToOpenNew()) {
            CInFileStream *inFile = new CInFileStream;
            CMyComPtr<IInStream> inStreamTemp = inFile;
            if (!inFile->Open(s.Path))
                return GetLastError_noZero_HRESULT();
            s.FileSpec = inFile;
            s.Stream = s.FileSpec;
            InsertToList(index);
        }
        // s.IsOpen = true;
        if (s.LocalPos != 0) {
            RINOK(s.Stream->Seek((Int64)s.LocalPos, STREAM_SEEK_SET, &s.LocalPos))
        }
#ifdef DEBUG_VOLUMES
        static int numOpens = 0;
        numOpens++;
        printf("\n-- %u, ReOpen, total_reopens = %u, num_open_files = %u\n", index, numOpens,
               NumListItems);
#endif
    }
    return S_OK;
}

Z7_COM7F_IMF(CInFileStreamVol::Read(void *data, UInt32 size, UInt32 *processedSize)) {
    if (processedSize)
        *processedSize = 0;
    if (size == 0)
        return S_OK;
    RINOK(EnsureOpen())
    CMultiStreams::CSubStream &s = OpenCallbackImp->Volumes.Streams[FileIndex];
    PRF(printf("\n== %u, Read =%u \n", FileIndex, size));
    return s.Stream->Read(data, size, processedSize);
}

Z7_COM7F_IMF(CInFileStreamVol::Seek(Int64 offset, UInt32 seekOrigin, UInt64 *newPosition)) {
    // if (seekOrigin >= 3) return STG_E_INVALIDFUNCTION;
    RINOK(EnsureOpen())
    CMultiStreams::CSubStream &s = OpenCallbackImp->Volumes.Streams[FileIndex];
    PRF(printf("\n-- %u, Seek seekOrigin=%u Seek =%u\n", FileIndex, seekOrigin, (unsigned)offset));
    return s.Stream->Seek(offset, seekOrigin, newPosition);
}

Z7_COM7F_IMF(CInFileStreamVol::GetSize(UInt64 *size)) {
    RINOK(EnsureOpen())
    CMultiStreams::CSubStream &s = OpenCallbackImp->Volumes.Streams[FileIndex];
    return s.FileSpec->GetSize(size);
}

// from ArchiveExtractCallback.cpp
bool IsSafePath(const UString &path);

HRESULT COpenCallbackImp::ValidatePackageVolume(const UString &name) const {
    UString relative = name;
    if (!relative.IsPrefixedBy(_packageVolumePrefix))
        return E_INVALIDARG;
    relative.DeleteFrontal(_packageVolumePrefix.Len());
    const bool band = relative.IsPrefixedBy(L"bands/");
    if (band) {
        if (relative.Len() <= 6 || relative.Len() > 22)
            return E_INVALIDARG;
        for (unsigned i = 6; i < relative.Len(); ++i)
            if (!((relative[i] >= L'0' && relative[i] <= L'9') ||
                  (relative[i] >= L'a' && relative[i] <= L'f')))
                return E_INVALIDARG;
    } else if (relative != L"token")
        return E_INVALIDARG;
    NFile::NFind::CFileInfo info;
    FString root = _packageRoot;
    if (!root.IsEmpty() && IS_PATH_SEPAR(root.Back()))
        root.DeleteBack();
    if (!info.Find(root, false))
        return GetLastError_noZero_HRESULT();
    if (!info.IsDir() || info.IsOsSymLink())
        return E_INVALIDARG;
    if (band) {
        const FString directory = _packageRoot + us2fs(UString(L"bands"));
        if (!info.Find(directory, false))
            return GetLastError_noZero_HRESULT();
        if (!info.IsDir() || info.IsOsSymLink())
            return E_INVALIDARG;
    }
    return S_OK;
}

Z7_COM7F_IMF(COpenCallbackImp::GetStream(const wchar_t *name, IInStream **inStream)) {
    COM_TRY_BEGIN
    *inStream = NULL;

    if (_subArchiveMode && !_delayedPackageVolumes)
        return S_FALSE;
    if (Callback) {
        RINOK(Callback->Open_CheckBreak())
    }

    UString name2 = name;
    if (_delayedPackageVolumes) {
        RINOK(ValidatePackageVolume(name2))
    }

#ifndef Z7_SFX

#ifdef _WIN32
    name2.Replace(L'/', WCHAR_PATH_SEPARATOR);
#endif

    // if (!allowAbsVolPaths)
    if (!IsSafePath(name2))
        return S_FALSE;

#ifdef _WIN32
    /* WIN32 allows wildcards in Find() function
       and doesn't allow wildcard in File.Open()
       so we can work without the following wildcard check here */
    if (name2.Find(L'*') >= 0)
        return S_FALSE;
    {
        unsigned startPos = 0;
        if (name2.IsPrefixedBy_Ascii_NoCase("\\\\?\\"))
            startPos = 3;
        if (name2.Find(L'?', startPos) >= 0)
            return S_FALSE;
    }
#endif

#endif

    FString fullPath;
    if (!NFile::NName::GetFullPath(_folderPrefix, us2fs(name2), fullPath))
        return S_FALSE;
    if (_delayedPackageVolumes) {
        NFile::NFind::CFileInfo link;
        if (link.Find(fullPath, false) && link.IsOsSymLink())
            return E_INVALIDARG;
    }
    if (!_fileInfo.Find_FollowLink(fullPath)) {
        const auto error = GetLastError();
        // Only absence represents a sparse hole. Permission and transport
        // failures must reach the reader instead of silently becoming zeroes.
#ifdef _WIN32
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
#else
        if (error == ENOENT)
#endif
            return S_FALSE;
        return GetLastError_noZero_HRESULT();
    }
    if (_fileInfo.IsDir())
        return _delayedPackageVolumes ? E_INVALIDARG : S_FALSE;

    CMultiStreams::CSubStream s;

    {
        CInFileStream *inFile = new CInFileStream;
        CMyComPtr<IInStream> inStreamTemp = inFile;
        if (!inFile->Open(fullPath))
            return GetLastError_noZero_HRESULT();
        if (_delayedPackageVolumes) {
            // Sparsebundle owns a bounded band-handle cache. Returning an
            // independent input avoids growing CMultiStreams' volume inventory
            // for every remote-band eviction/reopen, and retains no UI pointer.
            *inStream = inStreamTemp.Detach();
            return S_OK;
        }
        RINOK(Volumes.PrepareToOpenNew())
        s.FileSpec = inFile;
        s.Stream = s.FileSpec;
        s.Path = fullPath;
        // s.Size = _fileInfo.Size;
        // s.IsOpen = true;
    }

    const unsigned fileIndex = Volumes.Streams.Add(s);
    Volumes.InsertToList(fileIndex);

    FileSizes.Add(_fileInfo.Size);
    FileNames.Add(name2);
    FileNames_WasUsed.Add(true);

    CInFileStreamVol *inFile = new CInFileStreamVol;
    CMyComPtr<IInStream> inStreamTemp = inFile;
    inFile->FileIndex = fileIndex;
    inFile->OpenCallbackImp = this;
    inFile->OpenCallbackRef = this;
    // TotalSize += _fileInfo.Size;
    *inStream = inStreamTemp.Detach();
    return S_OK;
    COM_TRY_END
}

#ifndef Z7_NO_CRYPTO
Z7_COM7F_IMF(COpenCallbackImp::GetFscryptKeys(BSTR *keys)) {
    COM_TRY_BEGIN
    if (ReOpenCallback) {
        CMyComPtr<IFscryptKeyRequest> request;
        ReOpenCallback->QueryInterface(IID_IFscryptKeyRequest, (void **)&request);
        if (request)
            return request->GetFscryptKeys(keys);
    }
#if defined(__GXX_RTTI) || defined(_CPPRTTI)
    if (auto *request = dynamic_cast<IFscryptKeyUI *>(Callback)) {
        PasswordWasAsked = true;
        return request->Open_FscryptKeys(keys);
    }
#endif
    return CryptoGetTextPassword(keys);
    COM_TRY_END
}

Z7_COM7F_IMF(COpenCallbackImp::CryptoGetTextPassword(BSTR *password)) {
    COM_TRY_BEGIN
    if (ReOpenCallback) {
        Z7_DECL_CMyComPtr_QI_FROM(ICryptoGetTextPassword, getTextPassword,
                                  ReOpenCallback) if (getTextPassword) return getTextPassword
            ->CryptoGetTextPassword(password);
    }
    if (!Callback)
        return E_NOTIMPL;
    PasswordWasAsked = true;
    return Callback->Open_CryptoGetTextPassword(password);
    COM_TRY_END
}
#endif

// IProgress
Z7_COM7F_IMF(COpenCallbackImp::SetTotal(const UInt64 /* total */)) { return S_OK; }

Z7_COM7F_IMF(COpenCallbackImp::SetCompleted(const UInt64 * /* completed */)) {
    // this call can be used for additional structure parsing after archive openning.
    // so we ignore completed
    // we just want to process open break
    if (!Callback)
        return S_OK;
    return Callback->Open_CheckBreak();
}
