// Native memory/spill extraction stream adapted from 7-Zip ExtractCallback.h.
// Original baseline: b88c6bc09339de32f7d18bbab900686b244f42ba.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#pragma once
#include "7zip/Common/FileStreams.h"
#include "C/Alloc.h"
#include "Common/MyBuffer.h"
#include "Common/MyCom.h"
#include "Common/MyString.h"
#include "common/collections/MyVector.h"
#include <cstring>

#ifndef Z7_SFX

class CGrowBuf {
    Byte *_items;
    size_t _size;

    Z7_CLASS_NO_COPY(CGrowBuf)

  public:
    void Free() {
        MyFree(_items);
        _items = NULL;
        _size = 0;
    }

    // newSize >= keepSize
    bool ReAlloc_KeepData(size_t newSize, size_t keepSize) {
        void *buf = NULL;
        if (newSize) {
            buf = MyAlloc(newSize);
            if (!buf)
                return false;
        }
        if (keepSize)
            memcpy(buf, _items, keepSize);
        MyFree(_items);
        _items = (Byte *)buf;
        _size = newSize;
        return true;
    }

    CGrowBuf() : _items(NULL), _size(0) {}
    ~CGrowBuf() { MyFree(_items); }

    operator Byte *() { return _items; }
    operator const Byte *() const { return _items; }
    size_t Size() const { return _size; }
};

struct CVirtFile {
    CGrowBuf Data;

    UInt64 ExpectedSize; // size from props request. 0 if unknown
    size_t WrittenSize;  // size of written data in (Data) buffer
                         //   use (WrittenSize) only if
                         //   (CVirtFileSystem::_newVirtFileStream_IsReadyToWrite == false)
    UString BaseName;    // original name of file inside archive,
                         // It's not path. So any path separators
                         // should be treated as part of name (or as incorrect chars)
    UString AltStreamName;

    bool CTime_Defined;
    bool ATime_Defined;
    bool MTime_Defined;
    bool Attrib_Defined;

    // bool IsDir;
    bool IsAltStream;
    bool ColonWasUsed;
    DWORD Attrib;

    FILETIME CTime;
    FILETIME ATime;
    FILETIME MTime;

    CVirtFile()
        : CTime_Defined(false), ATime_Defined(false), MTime_Defined(false), Attrib_Defined(false),
          // IsDir(false),
          IsAltStream(false), ColonWasUsed(false) {}
};

/*
  We use CVirtFileSystem only for single file extraction:
  It supports the following cases and names:
     - "fileName" : single file
     - "fileName" item (main base file) and additional "fileName:altStream" items
     - "altStream" : single item without "fileName:" prefix.
  If file is flushed to disk, it uses Get_Correct_FsFile_Name(name).
*/

Z7_CLASS_IMP_NOQIB_1(CVirtFileSystem, ISequentialOutStream)
unsigned _numFlushed;

public:
bool IsAltStreamFile; // in:
                      // = true,  if extracting file is alt stream without "fileName:" prefix.
                      // = false, if extracting file is normal file, but additional
                      //          alt streams "fileName:altStream" items are possible.
private:
bool _newVirtFileStream_IsReadyToWrite; // it can non real file (if can't open alt stream)
bool _needWriteToRealFile;              // we need real writing to open file.
bool _wasSwitchedToFsMode;
bool _altStream_NeedRestore_Attrib_bool;
DWORD _altStream_NeedRestore_AttribVal;

CMyComPtr2<ISequentialOutStream, COutFileStream> _outFileStream;

public:
CObjectVector<CVirtFile> Files;
size_t MaxTotalAllocSize; // remain size, including Files.Back()
FString DirPrefix;        // files will be flushed to this FS directory.
UString FileName;         // name of file that will be extracted.
                  // it can be name of alt stream without "fileName:" prefix, if (IsAltStreamFile ==
                  // trye). we use that name to detect altStream part in "FileName:altStream".
CByteBuffer ZoneBuf;
int Index_of_MainExtractedFile_in_Files; // out: index in Files. == -1, if expected file was not
                                         // extracted
int Index_of_ZoneBuf_AltStream_in_Files; // out: index in Files. == -1, if no zonbuf alt stream

CVirtFileSystem() {
    _numFlushed = 0;
    IsAltStreamFile = false;
    _newVirtFileStream_IsReadyToWrite = false;
    _needWriteToRealFile = false;
    _wasSwitchedToFsMode = false;
    _altStream_NeedRestore_Attrib_bool = false;
    MaxTotalAllocSize = (size_t)0 - 1;
    Index_of_MainExtractedFile_in_Files = -1;
    Index_of_ZoneBuf_AltStream_in_Files = -1;
}

bool WasStreamFlushedToFS() const { return _wasSwitchedToFsMode; }

HRESULT CloseMemFile() {
    if (_wasSwitchedToFsMode)
        return FlushToDisk(true); // closeLast
    CVirtFile &file = Files.Back();
    if (file.Data.Size() != file.WrittenSize)
        file.Data.ReAlloc_KeepData(file.WrittenSize, file.WrittenSize);
    return S_OK;
}

HRESULT FlushToDisk(bool closeLast);
}
;

#endif
