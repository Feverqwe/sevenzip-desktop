// Adapted from 7-Zip CPP/7zip/UI/FileManager/ProgressDialog2.h (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#pragma once

#include "Common/MyString.h"
#include "Windows/Synchronization.h"
#include <optional>

struct CProgressMessageBoxPair {
    UString Title;
    UString Message;
};

struct CProgressFinalMessage {
    CProgressMessageBoxPair ErrorMessage;
    CProgressMessageBoxPair OkMessage;
    std::optional<HRESULT> Result;
    bool Partial = false;
    bool HadErrors = false;

    bool ThereIsMessage() const {
        return !ErrorMessage.Message.IsEmpty() || !OkMessage.Message.IsEmpty() || Partial ||
               HadErrors || (Result && FAILED(*Result));
    }
};

class CProgressSync {
    bool _stopped;
    bool _paused;
    NWindows::NSynchronization::CAutoResetEvent _pauseWake;

  public:
    bool _filesProgressMode;
    bool _isDir;
    UInt64 _totalBytes;
    UInt64 _completedBytes;
    UInt64 _totalFiles;
    UInt64 _curFiles;
    UInt64 _inSize;
    UInt64 _outSize;

    UString _titleFileName;
    UString _status;
    UString _filePath;

    UStringVector Messages;
    CProgressFinalMessage FinalMessage;

    NWindows::NSynchronization::CCriticalSection _cs;

    CProgressSync();
    bool Get_Stopped() {
        NWindows::NSynchronization::CCriticalSectionLock lock(_cs);
        return _stopped;
    }
    void Set_Stopped(bool val) {
        NWindows::NSynchronization::CCriticalSectionLock lock(_cs);
        _stopped = val;
        if (_pauseWake.IsCreated())
            _pauseWake.Set();
    }
    bool Get_Paused();
    void Set_Paused(bool val) {
        NWindows::NSynchronization::CCriticalSectionLock lock(_cs);
        _paused = val;
        if (_pauseWake.IsCreated())
            _pauseWake.Set();
    }
    // Configure before starting the one checkpoint thread. Native dialog
    // callbacks keep the original polling mode, including multiple callers.
    WRes EnableSingleWorkerWake();
    void Set_FilesProgressMode(bool filesProgressMode) {
        NWindows::NSynchronization::CCriticalSectionLock lock(_cs);
        _filesProgressMode = filesProgressMode;
    }
    HRESULT CheckStop();
    void Clear_Stop_Status();
    HRESULT ScanProgress(UInt64 numFiles, UInt64 totalSize, const FString &fileName,
                         bool isDir = false);
    HRESULT Set_NumFilesTotal(UInt64 val);
    void Set_NumBytesTotal(UInt64 val);
    void Set_NumFilesCur(UInt64 val);
    HRESULT Set_NumBytesCur(const UInt64 *val);
    HRESULT Set_NumBytesCur(UInt64 val);
    void Set_Ratio(const UInt64 *inSize, const UInt64 *outSize);
    void Set_TitleFileName(const UString &fileName);
    void Set_Status(const UString &s);
    HRESULT Set_Status2(const UString &s, const wchar_t *path, bool isDir = false);
    void Set_FilePath(const wchar_t *path, bool isDir = false);
    void AddError_Message(const wchar_t *message);
    void AddError_Message_Name(const wchar_t *message, const wchar_t *name);
    // void AddError_Code_Name(DWORD systemError, const wchar_t *name);
    void AddError_Code_Name(HRESULT systemError, const wchar_t *name);
    bool ThereIsMessage() const { return !Messages.IsEmpty() || FinalMessage.ThereIsMessage(); }
};
