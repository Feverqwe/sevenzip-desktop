// Device-image export adapted from 7-Zip UI/FileManager/FSDrives.cpp.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "C/Alloc.h"
#include "Common/Defs.h"
#include "Common/StringConvert.h"
#include "Windows/FileIO.h"
#include "Windows/FileName.h"
#include "filemanager/providers/FSDrives.h"

using namespace NWindows;
using namespace NFile;

struct CPhysTempBuffer {
    void *buffer;
    CPhysTempBuffer() : buffer(NULL) {}
    ~CPhysTempBuffer() { MidFree(buffer); }
};

static HRESULT CopyFileSpec(CFSTR fromPath, CFSTR toPath, bool writeToDisk, UInt64 fileSize,
                            UInt32 bufferSize, UInt64 progressStart, IProgress *progress) {
    NIO::CInFile inFile;
    if (!inFile.Open(fromPath))
        return GetLastError_noZero_HRESULT();
    if (fileSize == (UInt64)(Int64)-1) {
        if (!inFile.GetLength(fileSize))
            return GetLastError_noZero_HRESULT();
    }

    NIO::COutFile outFile;
    if (writeToDisk) {
        if (!outFile.Open(toPath, FILE_SHARE_WRITE, OPEN_EXISTING, 0))
            return GetLastError_noZero_HRESULT();
    } else if (!outFile.Create_NEW(toPath))
        return GetLastError_noZero_HRESULT();

    CPhysTempBuffer tempBuffer;
    tempBuffer.buffer = MidAlloc(bufferSize);
    if (!tempBuffer.buffer)
        return E_OUTOFMEMORY;

    for (UInt64 pos = 0; pos < fileSize;) {
        {
            const UInt64 progressCur = progressStart + pos;
            RINOK(progress->SetCompleted(&progressCur))
        }
        const UInt64 rem = fileSize - pos;
        UInt32 curSize = (UInt32)MyMin(rem, (UInt64)bufferSize);
        UInt32 processedSize;
        if (!inFile.Read(tempBuffer.buffer, curSize, processedSize))
            return GetLastError_noZero_HRESULT();
        if (processedSize == 0)
            break;
        curSize = processedSize;
        if (writeToDisk) {
            const UInt32 kMask = 0x1FF;
            curSize = (curSize + kMask) & ~kMask;
            if (curSize > bufferSize)
                return E_FAIL;
        }
        if (!outFile.Write(tempBuffer.buffer, curSize, processedSize))
            return GetLastError_noZero_HRESULT();
        if (curSize != processedSize)
            return E_FAIL;
        pos += curSize;
    }

    return S_OK;
}

HRESULT CFSDrives::GetFileSize(unsigned index, UInt64 &fileSize) const {
#ifdef Z7_DEVICE_FILE
    NIO::CInFile inFile;
    if (!inFile.Open(_drives[index].GetDeviceFileIoName()))
        return GetLastError_noZero_HRESULT();
    if (inFile.SizeDefined) {
        fileSize = inFile.Size;
        return S_OK;
    }
#else
    UNUSED_VAR(index)
#endif
    fileSize = 0;
    return E_FAIL;
}

Z7_COM7F_IMF(CFSDrives::CopyTo(Int32 moveMode, const UInt32 *indices, UInt32 numItems,
                               Int32 /* includeAltStreams */, Int32 /* replaceAltStreamColon */,
                               const wchar_t *path, IFolderOperationsExtractCallback *callback)) {
    if (numItems == 0)
        return S_OK;
    if (moveMode)
        return E_NOTIMPL;
    if (!_volumeMode)
        return E_NOTIMPL;

    UInt64 totalSize = 0;
    UInt32 i;
    for (i = 0; i < numItems; i++) {
        const CDriveInfo &di = _drives[indices[i]];
        if (di.KnownSize)
            totalSize += di.DriveSize;
    }
    RINOK(callback->SetTotal(totalSize))
    RINOK(callback->SetNumFiles(numItems))

    const FString destPath = us2fs(path);
    if (destPath.IsEmpty())
        return E_INVALIDARG;

    const bool isAltDest = NName::IsAltPathPrefix(destPath);
    const bool isDirectPath = (!isAltDest && !IsPathSepar(destPath.Back()));

    if (isDirectPath) {
        if (numItems > 1)
            return E_INVALIDARG;
    }

    UInt64 completedSize = 0;
    RINOK(callback->SetCompleted(&completedSize))
    for (i = 0; i < numItems; i++) {
        const unsigned index = indices[i];
        const CDriveInfo &di = _drives[index];
        FString destPath2 = destPath;

        if (!isDirectPath) {
            FString destName = di.Name;
            if (!destName.IsEmpty() && destName.Back() == ':') {
                destName.DeleteBack();
                AddExt(destName, index);
            }
            destPath2 += destName;
        }

        const FString srcPath = di.GetDeviceFileIoName();

        UInt64 fileSize = 0;
        if (GetFileSize(index, fileSize) != S_OK) {
            return E_FAIL;
        }
        if (!di.KnownSize) {
            totalSize += fileSize;
            RINOK(callback->SetTotal(totalSize))
        }

        Int32 writeAskResult;
        CMyComBSTR destPathResult;
        RINOK(callback->AskWrite(fs2us(srcPath), BoolToInt(false), NULL, &fileSize,
                                 fs2us(destPath2), &destPathResult, &writeAskResult))

        if (!IntToBool(writeAskResult)) {
            if (totalSize >= fileSize)
                totalSize -= fileSize;
            RINOK(callback->SetTotal(totalSize))
            continue;
        }

        RINOK(callback->SetCurrentFilePath(fs2us(srcPath)))

        const UInt32 kBufferSize = (4 << 20);
        const UInt32 bufferSize = (di.DriveType == DRIVE_REMOVABLE) ? (18 << 10) * 4 : kBufferSize;
        RINOK(CopyFileSpec(srcPath, us2fs(destPathResult), false, fileSize, bufferSize,
                           completedSize, callback))
        completedSize += fileSize;
    }

    return S_OK;
}

Z7_COM7F_IMF(CFSDrives::CopyFrom(Int32 /* moveMode */, const wchar_t * /* fromFolderPath */,
                                 const wchar_t *const * /* itemsPaths */, UInt32 /* numItems */,
                                 IProgress * /* progress */)) {
    return E_NOTIMPL;
}

Z7_COM7F_IMF(CFSDrives::CopyFromFile(UInt32 /* index */, const wchar_t * /* fullFilePath */,
                                     IProgress * /* progress */)) {
    return E_NOTIMPL;
}

Z7_COM7F_IMF(CFSDrives::CreateFolder(const wchar_t * /* name */, IProgress * /* progress */)) {
    return E_NOTIMPL;
}

Z7_COM7F_IMF(CFSDrives::CreateFile(const wchar_t * /* name */, IProgress * /* progress */)) {
    return E_NOTIMPL;
}

Z7_COM7F_IMF(CFSDrives::Rename(UInt32 /* index */, const wchar_t * /* newName */,
                               IProgress * /* progress */)) {
    return E_NOTIMPL;
}

Z7_COM7F_IMF(CFSDrives::Delete(const UInt32 * /* indices */, UInt32 /* numItems */,
                               IProgress * /* progress */)) {
    return E_NOTIMPL;
}

Z7_COM7F_IMF(CFSDrives::SetProperty(UInt32 /* index */, PROPID /* propID */,
                                    const PROPVARIANT * /* value */, IProgress * /* progress */)) {
    return E_NOTIMPL;
}
