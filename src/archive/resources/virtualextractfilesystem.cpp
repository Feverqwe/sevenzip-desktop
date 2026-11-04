// Native memory/spill extraction stream adapted from 7-Zip ExtractCallback.cpp.
// Original baseline: b88c6bc09339de32f7d18bbab900686b244f42ba.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "archive/resources/virtualextractfilesystem.h"
#include "7zip/Common/StreamUtils.h"
#include "Common/MyLinux.h"
#include "Common/StringConvert.h"
#include "Windows/ErrorMsg.h"
#include "Windows/FileFind.h"
#include "archive/callbacks/ArchiveExtractCallback.h"
#include "archive/operations/ExtractingFilePath.h"
#include "fileops/native/FileDir.h"

#ifndef Z7_SFX
using namespace NWindows;
using namespace NFile;
using namespace NFind;

Z7_COM7F_IMF(CVirtFileSystem::Write(const void *data, UInt32 size, UInt32 *processedSize)) {
    if (processedSize)
        *processedSize = 0;
    if (size == 0)
        return S_OK;
    if (!_wasSwitchedToFsMode) {
        CVirtFile &file = Files.Back();
        const size_t rem = file.Data.Size() - file.WrittenSize;
        bool useMem = true;
        if (rem < size) {
            UInt64 b = 0;
            if (file.Data.Size() == 0)
                b = file.ExpectedSize;
            UInt64 a = (UInt64)file.WrittenSize + size;
            if (b < a)
                b = a;
            a = (UInt64)file.Data.Size() * 2;
            if (b < a)
                b = a;
            useMem = false;
            if (b <= MaxTotalAllocSize)
                useMem = file.Data.ReAlloc_KeepData((size_t)b, file.WrittenSize);
        }

#if 0 // 1 for debug : FLUSHING TO FS
    useMem = false;
#endif

        if (useMem) {
            memcpy(file.Data + file.WrittenSize, data, size);
            file.WrittenSize += size;
            if (processedSize)
                *processedSize = (UInt32)size;
            return S_OK;
        }
        _wasSwitchedToFsMode = true;
    }

    if (!_newVirtFileStream_IsReadyToWrite) // we check for _newVirtFileStream_IsReadyToWrite to
                                            // optimize execution
    {
        RINOK(FlushToDisk(false))
    }

    if (_needWriteToRealFile)
        return _outFileStream.Interface()->Write(data, size, processedSize);
    if (processedSize)
        *processedSize = size;
    return S_OK;
}

HRESULT CVirtFileSystem::FlushToDisk(bool closeLast) {
    while (_numFlushed < Files.Size()) {
        CVirtFile &file = Files[_numFlushed];
        const FString basePath = DirPrefix + us2fs(Get_Correct_FsFile_Name(file.BaseName));
        FString path = basePath;

        if (file.ColonWasUsed) {
            if (ZoneBuf.Size() != 0 && Is_ZoneId_StreamName(file.AltStreamName)) {
                // it's expected that
                // CArchiveExtractCallback::GetStream() have excluded
                // ZoneId alt stream from extraction already.
                // But we exclude alt stream extraction here too.
                _numFlushed++;
                continue;
            }
            path.Add_Colon();
            path += us2fs(Get_Correct_FsFile_Name(file.AltStreamName));
        }

        if (!_newVirtFileStream_IsReadyToWrite) {
#ifdef _WIN32
            if (file.ColonWasUsed) {
                NFind::CFileInfo parentFi;
                if (parentFi.Find(basePath) && parentFi.IsReadOnly()) {
                    _altStream_NeedRestore_Attrib_bool = true;
                    _altStream_NeedRestore_AttribVal = parentFi.Attrib;
                    NDir::SetFileAttrib(basePath,
                                        parentFi.Attrib & ~(DWORD)FILE_ATTRIBUTE_READONLY);
                }
            }
#endif
            _outFileStream.Create_if_Empty();
            _needWriteToRealFile = _outFileStream->Create_NEW(path);
            if (!_needWriteToRealFile) {
                if (!file.ColonWasUsed)
                    return GetLastError_noZero_HRESULT(); // it's main file and we can't ignore such
                                                          // error.
                                                          // (file.ColonWasUsed == true)
                                                          // So it's additional alt stream.
                // And we ignore file creation error for additional alt stream.
                // ShowErrorMessage(UString("Can't create file ") + fs2us(path));
            }
            _newVirtFileStream_IsReadyToWrite = true;
            // _openFilePath = path;
            HRESULT hres = S_OK;
            if (_needWriteToRealFile)
                hres = WriteStream(_outFileStream, file.Data, file.WrittenSize);
            // we free allocated memory buffer after data flushing:
            file.WrittenSize = 0;
            file.Data.Free();
            RINOK(hres)
        }

        if (_numFlushed == Files.Size() - 1 && !closeLast)
            break;

        if (_needWriteToRealFile) {
            if (file.CTime_Defined || file.ATime_Defined || file.MTime_Defined) {
                CFiTime cTime, aTime, mTime;
#ifdef _WIN32
                if (file.CTime_Defined)
                    cTime = file.CTime;
                if (file.ATime_Defined)
                    aTime = file.ATime;
                if (file.MTime_Defined)
                    mTime = file.MTime;
                const bool cDefined = file.CTime_Defined;
                const bool aDefined = file.ATime_Defined;
                const bool mDefined = file.MTime_Defined;
#else
                const bool cDefined = file.CTime_Defined && FILETIME_To_timespec(file.CTime, cTime);
                const bool aDefined = file.ATime_Defined && FILETIME_To_timespec(file.ATime, aTime);
                const bool mDefined = file.MTime_Defined && FILETIME_To_timespec(file.MTime, mTime);
#endif
                _outFileStream->SetTime(cDefined ? &cTime : NULL, aDefined ? &aTime : NULL,
                                        mDefined ? &mTime : NULL);
            }
            _outFileStream->Close();
        }

        _numFlushed++;
        _newVirtFileStream_IsReadyToWrite = false;

        if (_needWriteToRealFile) {
            if (!file.ColonWasUsed && ZoneBuf.Size() != 0)
                WriteZoneFile_To_BaseFile(path, ZoneBuf);
            if (file.Attrib_Defined)
                NDir::SetFileAttrib_PosixHighDetect(path, file.Attrib);
            // _openFilePath.Empty();
            _needWriteToRealFile = false;
        }

#ifdef _WIN32
        if (_altStream_NeedRestore_Attrib_bool) {
            _altStream_NeedRestore_Attrib_bool = false;
            NDir::SetFileAttrib(basePath, _altStream_NeedRestore_AttribVal);
        }
#endif
    }
    return S_OK;
}

#endif
