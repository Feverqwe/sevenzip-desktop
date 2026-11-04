// Native Split/Combine workers extracted from 7-Zip PanelSplitFile.cpp.
// Upstream 0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17.
// Copyright (C) 1999-2026 Igor Pavlov. GNU LGPL 2.1 or later.
// Existing Qt I/O/progress/output adapters preserve the native algorithms.
#include "fileops/volumes/nativevolumeworker.h"
#include "7zip/UI/FileManager/PropertyNameRes.h"
#include "7zip/UI/FileManager/resource.h"
#include "Common/StringConvert.h"
#include "Windows/FileFind.h"
#include "common/memory/midallocbuffer.h"
#include "filemanager/panels/PanelInfo.h"
#include "fileops/volumes/qtvolumeio.h"
#include <QtGlobal>

using namespace NWindows;
using namespace NFile;

static const char *const g_Message_FileWriteError =
    QT_TRANSLATE_NOOP("SevenZipDiagnostics", "File write error");

void CVolSeqName::SetNumDigits(UInt64 numVolumes) {
    ChangedPart = "000";
    while (numVolumes > 999) {
        numVolumes /= 10;
        ChangedPart.Add_Char('0');
    }
}

bool CVolSeqName::ParseName(const UString &name) {
    if (name.Len() < 2)
        return false;
    if (name.Back() != L'1' || name[name.Len() - 2] != L'0')
        return false;

    unsigned pos = name.Len() - 2;
    for (; pos > 0 && name[pos - 1] == '0'; pos--)
        ;
    UnchangedPart.SetFrom(name, pos);
    ChangedPart = name.Ptr(pos);
    return true;
}

UString CVolSeqName::GetNextName() {
    for (int i = (int)ChangedPart.Len() - 1; i >= 0; i--) {
        const wchar_t c = ChangedPart[i];
        if (c != L'9') {
            ChangedPart.ReplaceOneCharAtPos((unsigned)i, (wchar_t)(c + 1));
            break;
        }
        ChangedPart.ReplaceOneCharAtPos((unsigned)i, L'0');
        if (i == 0)
            ChangedPart.InsertAtFront(L'1');
    }
    return UnchangedPart + ChangedPart;
}

class CPreAllocOutFile {
    UInt64 _preAllocSize;

  public:
    NIO::COutFile File;
    UInt64 Written;

    CPreAllocOutFile() : _preAllocSize(0), Written(0) {}

    ~CPreAllocOutFile() { SetCorrectFileLength(); }

    void PreAlloc(UInt64 preAllocSize) {
        _preAllocSize = 0;
        if (File.SetLength(preAllocSize))
            _preAllocSize = preAllocSize;
        QtVolumeSeekToBegin(File);
    }

    bool Write(const void *data, UInt32 size, UInt32 &processedSize) throw() {
        bool res = QtVolumeWrite(File, data, size, processedSize);
        Written += processedSize;
        return res;
    }

    void Close() {
        SetCorrectFileLength();
        Written = 0;
        _preAllocSize = 0;
        File.Close();
    }

    void SetCorrectFileLength() {
        if (Written < _preAllocSize) {
            File.SetLength(Written);
            _preAllocSize = 0;
        }
    }
};

static const UInt32 kBufSize = (1 << 20);

HRESULT CThreadSplit::ProcessVirt() {
    NIO::CInFile inFile;
    if (!inFile.Open(FilePath))
        return GetLastError_noZero_HRESULT();

    CPreAllocOutFile outFile;

    CMyBuffer buffer;
    if (!buffer.Allocate(kBufSize))
        return E_OUTOFMEMORY;

    CVolSeqName seqName;
    seqName.SetNumDigits(NumVolumes);

    UInt64 length;
    if (!inFile.GetLength(length))
        return GetLastError_noZero_HRESULT();

    QtVolumeProgress &sync = Sync;
    sync.Set_NumBytesTotal(length);

    UInt64 pos = 0;
    UInt64 prev = 0;
    UInt64 numFiles = 0;
    unsigned volIndex = 0;

    for (;;) {
        UInt64 volSize;
        if (volIndex < VolumeSizes.Size())
            volSize = VolumeSizes[volIndex];
        else
            volSize = VolumeSizes.Back();

        UInt32 needSize = kBufSize;
        {
            const UInt64 rem = volSize - outFile.Written;
            if (needSize > rem)
                needSize = (UInt32)rem;
        }
        UInt32 processedSize;
        if (!QtVolumeRead(inFile, buffer, needSize, processedSize))
            return GetLastError_noZero_HRESULT();
        if (processedSize == 0)
            return S_OK;
        needSize = processedSize;

        if (outFile.Written == 0) {
            FString name = VolBasePath;
            name.Add_Dot();
            name += us2fs(seqName.GetNextName());
            sync.Set_FilePath(fs2us(name));
            if (!CreateOutput(outFile.File, name)) {
                const HRESULT res = GetLastError_noZero_HRESULT();
                AddErrorPath(name);
                return res;
            }
            UInt64 expectSize = volSize;
            if (pos < length) {
                const UInt64 rem = length - pos;
                if (expectSize > rem)
                    expectSize = rem;
            }
            outFile.PreAlloc(expectSize);
        }

        if (!outFile.Write(buffer, needSize, processedSize))
            return GetLastError_noZero_HRESULT();
        if (needSize != processedSize)
            throw g_Message_FileWriteError;

        pos += processedSize;

        if (outFile.Written == volSize) {
            outFile.Close();
            sync.Set_NumFilesCur(++numFiles);
            if (volIndex < VolumeSizes.Size())
                volIndex++;
        }

        if (pos - prev >= ((UInt32)1 << 22) || outFile.Written == 0) {
            RINOK(sync.Set_NumBytesCur(pos))
            prev = pos;
        }
    }
}

HRESULT CThreadCombine::ProcessVirt() {
    NIO::COutFile outFile;
    if (!CreateOutput(outFile, OutputPath)) {
        const HRESULT res = GetLastError_noZero_HRESULT();
        AddErrorPath(OutputPath);
        return res;
    }

    QtVolumeProgress &sync = Sync;
    sync.Set_NumBytesTotal(TotalSize);

    CMyBuffer bufferObject;
    if (!bufferObject.Allocate(kBufSize))
        return E_OUTOFMEMORY;
    Byte *buffer = (Byte *)(void *)bufferObject;
    UInt64 pos = 0;
    FOR_VECTOR(i, Names) {
        NIO::CInFile inFile;
        const FString nextName = InputDirPrefix + Names[i];
        if (!inFile.Open(nextName)) {
            const HRESULT res = GetLastError_noZero_HRESULT();
            AddErrorPath(nextName);
            return res;
        }
        sync.Set_FilePath(fs2us(nextName));
        for (;;) {
            UInt32 processedSize;
            if (!QtVolumeRead(inFile, buffer, kBufSize, processedSize)) {
                const HRESULT res = GetLastError_noZero_HRESULT();
                AddErrorPath(nextName);
                return res;
            }
            if (processedSize == 0)
                break;
            const UInt32 needSize = processedSize;
            if (!QtVolumeWrite(outFile, buffer, needSize, processedSize)) {
                const HRESULT res = GetLastError_noZero_HRESULT();
                AddErrorPath(OutputPath);
                return res;
            }
            if (needSize != processedSize)
                throw g_Message_FileWriteError;
            pos += processedSize;
            RINOK(sync.Set_NumBytesCur(pos))
        }
    }
    return S_OK;
}

static void AddInfoFileName(UString &dest, const UString &name) {
    dest += "\n  ";
    dest += name;
}

UString CCombineVolumePlan::GetInfo() const {
    UString info;
    AddValuePair2(info, IDS_PROP_FILES, Names.Size(), TotalSize);

    info.Add_LF();
    info += fs2us(InputDirPrefix);

    unsigned i;
    for (i = 0; i < Names.Size() && i < 2; i++)
        AddInfoFileName(info, fs2us(Names[i]));
    if (i != Names.Size()) {
        if (i + 1 != Names.Size())
            AddInfoFileName(info, L"...");
        AddInfoFileName(info, fs2us(Names.Back()));
    }

    return info;
}

void CCombineVolumePlan::LoadParts(const UString &srcPath, const UString &itemName,
                                   CVolSeqName &volSeqName) {
    Names.Clear();
    UString nextName = itemName;
    TotalSize = 0;
    for (;;) {
        NFind::CFileInfo fileInfo;
        if (!fileInfo.Find(us2fs(srcPath + nextName)) || fileInfo.IsDir())
            break;
        Names.Add(us2fs(nextName));
        TotalSize += fileInfo.Size;
        nextName = volSeqName.GetNextName();
    }
}

UString CVolSeqName::GetCombinedName() const {
    UString outName = UnchangedPart;
    while (!outName.IsEmpty()) {
        if (outName.Back() != L'.')
            break;
        outName.DeleteBack();
    }
    if (outName.IsEmpty())
        outName = "file";

    return outName;
}

bool QtVolumeWorker::CreateOutput(NWindows::NFile::NIO::COutFile &file, const FString &path) {
    return createOutput ? createOutput(file, path) : file.Create_NEW(path);
}
