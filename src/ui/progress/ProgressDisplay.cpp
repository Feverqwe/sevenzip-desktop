// Progress display adapted from 7-Zip UI/FileManager/ProgressDialog2.cpp.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "7zip/UI/FileManager/ProgressDialog2Res.h"
#include "7zip/UI/GUI/ExtractRes.h"
#include "Common/IntToString.h"
#include "filemanager/views/qtlistview.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "ui/dialogs/qtmodalresult.h"
#include "ui/progress/ProgressDialog2.h"
#include <QLabel>

using namespace NWindows;

static const UInt32 kLangIDs[] = {
    IDT_PROGRESS_ELAPSED,   IDT_PROGRESS_REMAINING, IDT_PROGRESS_TOTAL,  IDT_PROGRESS_SPEED,
    IDT_PROGRESS_PROCESSED, IDT_PROGRESS_RATIO,     IDT_PROGRESS_ERRORS, IDB_PAUSE};
static const UInt32 kLangIDs_Colon[] = {IDT_PROGRESS_PACKED, IDT_PROGRESS_FILES};

#define UNDEFINED_VAL ((UInt64)(Int64) - 1)
#define INIT_AS_UNDEFINED(v) v = UNDEFINED_VAL;
#define IS_UNDEFINED_VAL(v) ((v) == UNDEFINED_VAL)
#define IS_DEFINED_VAL(v) ((v) != UNDEFINED_VAL)

#define UINT_TO_STR_2(val)                                                                         \
    {                                                                                              \
        s[0] = (wchar_t)('0' + (val) / 10);                                                        \
        s[1] = (wchar_t)('0' + (val) % 10);                                                        \
        s += 2;                                                                                    \
    }

void GetTimeString(UInt64 timeValue, wchar_t *s) {
    UInt64 hours = timeValue / 3600;
    UInt32 seconds = (UInt32)(timeValue - hours * 3600);
    UInt32 minutes = seconds / 60;
    seconds %= 60;
    if (hours > 99) {
        ConvertUInt64ToString(hours, s);
        for (; *s != 0; s++)
            ;
    } else {
        UInt32 hours32 = (UInt32)hours;
        UINT_TO_STR_2(hours32)
    }
    *s++ = ':';
    UINT_TO_STR_2(minutes)
    *s++ = ':';
    UINT_TO_STR_2(seconds)
    *s = 0;
}

static void ReduceString(UString &s, unsigned size) {
    if (s.Len() <= size)
        return;
    s.Delete(size / 2, s.Len() - size);
    s.Insert(size / 2, L" ... ");
}

static void ConvertSizeToString(UInt64 v, wchar_t *s) {
    Byte c = 0;
    if (v >= ((UInt64)100000 << 20)) {
        v >>= 30;
        c = 'G';
    } else if (v >= ((UInt64)100000 << 10)) {
        v >>= 20;
        c = 'M';
    } else if (v >= ((UInt64)100000 << 0)) {
        v >>= 10;
        c = 'K';
    }
    ConvertUInt64ToString(v, s);
    if (c != 0) {
        s += MyStringLen(s);
        *s++ = ' ';
        *s++ = c;
        *s++ = 'B';
        *s++ = 0;
    }
}

static void GetChangedString(const UString &newStr, UString &prevStr, bool &hasChanged) {
    hasChanged = !(prevStr == newStr);
    if (hasChanged)
        prevStr = newStr;
}

static unsigned GetPower32(UInt32 val) {
    const unsigned kStart = 32;
    UInt32 mask = ((UInt32)1 << (kStart - 1));
    for (unsigned i = kStart;; i--) {
        if (i == 0 || (val & mask) != 0)
            return i;
        mask >>= 1;
    }
}

static unsigned GetPower64(UInt64 val) {
    UInt32 high = (UInt32)(val >> 32);
    if (high == 0)
        return GetPower32((UInt32)val);
    return GetPower32(high) + 32;
}

static UInt64 MyMultAndDiv(UInt64 mult1, UInt64 mult2, UInt64 divider) {
    unsigned pow1 = GetPower64(mult1);
    unsigned pow2 = GetPower64(mult2);
    while (pow1 + pow2 > 64) {
        if (pow1 > pow2) {
            pow1--;
            mult1 >>= 1;
        } else {
            pow2--;
            mult2 >>= 1;
        }
        divider >>= 1;
    }
    UInt64 res = mult1 * mult2;
    if (divider != 0)
        res /= divider;
    return res;
}

static const unsigned kTitleFileNameSizeLimit = 36;
static const unsigned kCurrentFileNameSizeLimit = 82;

bool CProgressDialog::OnInit() {
    for (UInt32 id : kLangIDs) {
        if (auto *label = qobject_cast<QLabel *>(GetItem(id)))
            QtBindResourceText(label, id, label->text());
        else
            QtBindLangText(GetItem(id), id);
    }
    for (UInt32 id : kLangIDs_Colon) {
        GetItem(id)->setProperty("languageColon", true);
        QString caption = static_cast<QLabel *>(GetItem(id))->text();
        QtBindResourceText(GetItem(id), id, caption);
    }
    QtBindStandardResourceText(GetItem(IDCANCEL), IDCANCEL, "Cancel");
    _hwndForTaskbar = MainWindow;
    if (!_hwndForTaskbar)
        _hwndForTaskbar = parentWidget();
    if (!_hwndForTaskbar)
        _hwndForTaskbar = this;
    INIT_AS_UNDEFINED(_progressBar_Range)
    INIT_AS_UNDEFINED(_progressBar_Pos)
    INIT_AS_UNDEFINED(_prevPercentValue)
    INIT_AS_UNDEFINED(_prevElapsedSec)
    INIT_AS_UNDEFINED(_prevRemainingSec)
    INIT_AS_UNDEFINED(_prevSpeed)
    _prevSpeed_MoveBits = 0;
    _prevTime = GetTickCount();
    _elapsedTime = 0;
    INIT_AS_UNDEFINED(_totalBytes_Prev)
    INIT_AS_UNDEFINED(_processed_Prev)
    INIT_AS_UNDEFINED(_packed_Prev)
    INIT_AS_UNDEFINED(_ratio_Prev)
    _filesStr_Prev.Empty();
    _filesTotStr_Prev.Empty();
    m_ProgressBar.Attach(GetItem(IDC_PROGRESS1));
    _messageList = static_cast<CListView *>(GetItem(IDL_PROGRESS_MESSAGES));
    _wasCreated = true;
    _dialogCreatedEvent.Set();
    GetItemText(IDB_PAUSE, _pause_String);
    LangString(IDS_CONTINUE, _continue_String);
    LangString(IDS_PROGRESS_PAUSED, _paused_String);
    SetText(_title);
    SetPauseText();
    if (_transport && !_transport->CanPause())
        GetItem(IDB_PAUSE)->setEnabled(false);
    _messageList->InsertColumn(0, L"", 40);
    _messageList->InsertColumn(1, L"", 460);
    _messageList->SetColumnWidthAuto(0);
    _messageList->SetColumnWidthAuto(1);
    EnableErrorsControls(false);
    GetItemSizes(2, _buttonSizeX, _buttonSizeY);
    _numReduceSymbols = kCurrentFileNameSizeLimit;
    NormalizeSize(true);
    if (!ShowCompressionInfo) {
        HideItem(IDT_PROGRESS_PACKED);
        HideItem(IDT_PROGRESS_PACKED_VAL);
        HideItem(IDT_PROGRESS_RATIO);
        HideItem(IDT_PROGRESS_RATIO_VAL);
    }
    if (IconID >= 0) {
        QIcon icon = LoadIcon(IconID);
        SetIcon(icon);
    }
    _timer = startTimer(200);
    CheckNeedClose();
    SetTaskbarProgressState();
    GetItem(IDCANCEL)->setFocus();
    return true;
}

bool CProgressDialog::OnSize(int /*xSize*/, int /*ySize*/) {
    // Qt layouts own geometry; retain the source width-to-string reduction.
    const int xSizeClient = GetItem(IDC_PROGRESS1)->width();
    unsigned i;
    for (i = 800; i > 40; i = i * 9 / 10)
        if (Units_To_Pixels_X((int)i) <= xSizeClient)
            break;
    _numReduceSymbols = i / 4;
    return false;
}

void CProgressDialog::SetProgressRange(UInt64 range) {
    if (range == _progressBar_Range)
        return;
    _progressBar_Range = range;
    INIT_AS_UNDEFINED(_progressBar_Pos)
    _progressConv.Init(range);
    m_ProgressBar.SetRange32(0, _progressConv.Count(range));
}

void CProgressDialog::SetProgressPos(UInt64 pos) {
    if (pos >= _progressBar_Range || pos <= _progressBar_Pos ||
        pos - _progressBar_Pos >= (_progressBar_Range >> 10)) {
        m_ProgressBar.SetPos(_progressConv.Count(pos));
        if (_taskbarList && _hwndForTaskbar) {
#ifdef _WIN32
            _taskbarList->SetProgressValue(GetNativeTaskbarWindow(_hwndForTaskbar), pos,
                                           _progressBar_Range);
#else
            _taskbarList->SetProgressValue(_hwndForTaskbar, pos, _progressBar_Range);
#endif
        }
        _progressBar_Pos = pos;
    }
}

void CProgressDialog::ShowSize(unsigned id, UInt64 val, UInt64 &prev) {
    if (val == prev)
        return;
    prev = val;
    wchar_t s[40];
    s[0] = 0;
    if (IS_DEFINED_VAL(val))
        ConvertSizeToString(val, s);
    SetItemText(id, s);
}

void CProgressDialog::UpdateStatInfo(bool showAll) {
    UInt64 total, completed, totalFiles, completedFiles, inSize, outSize;
    bool filesProgressMode;
    bool titleFileName_Changed;
    bool curFilePath_Changed;
    bool status_Changed;
    unsigned numErrors;
    {
        NSynchronization::CCriticalSectionLock lock(Sync._cs);
        total = Sync._totalBytes;
        completed = Sync._completedBytes;
        totalFiles = Sync._totalFiles;
        completedFiles = Sync._curFiles;
        inSize = Sync._inSize;
        outSize = Sync._outSize;
        filesProgressMode = Sync._filesProgressMode;
        GetChangedString(Sync._titleFileName, _titleFileName, titleFileName_Changed);
        GetChangedString(Sync._filePath, _filePath, curFilePath_Changed);
        GetChangedString(Sync._status, _status, status_Changed);
        if (_isDir != Sync._isDir) {
            curFilePath_Changed = true;
            _isDir = Sync._isDir;
        }
        numErrors = Sync.Messages.Size();
    }
    UInt32 curTime = GetTickCount();
    const UInt64 progressTotal = filesProgressMode ? totalFiles : total;
    const UInt64 progressCompleted = filesProgressMode ? completedFiles : completed;
    {
        if (IS_UNDEFINED_VAL(progressTotal)) {
            // SetPos(0);
            // SetRange(progressCompleted);
            const int percent = TransportPercent.load();
            if (percent >= 0) {
                SetProgressRange(100);
                SetProgressPos(UInt64(percent));
            }
        } else {
            if (_progressBar_Pos != 0 || progressCompleted != 0 ||
                (_progressBar_Range == 0 && progressTotal != 0)) {
                SetProgressRange(progressTotal);
                SetProgressPos(progressCompleted);
            }
        }
    }
    ShowSize(IDT_PROGRESS_TOTAL_VAL, total, _totalBytes_Prev);
    _elapsedTime += (curTime - _prevTime);
    _prevTime = curTime;
    UInt64 elapsedSec = _elapsedTime / 1000;
    bool elapsedChanged = false;
    if (elapsedSec != _prevElapsedSec) {
        _prevElapsedSec = elapsedSec;
        elapsedChanged = true;
        wchar_t s[40];
        GetTimeString(elapsedSec, s);
        SetItemText(IDT_PROGRESS_ELAPSED_VAL, s);
    }
    bool needSetTitle = false;
    if (elapsedChanged || showAll) {
        if (numErrors > _numPostedMessages) {
            UpdateMessagesDialog();
            wchar_t s[32];
            ConvertUInt64ToString(numErrors, s);
            SetItemText(IDT_PROGRESS_ERRORS_VAL, s);
            if (!_errorsWereDisplayed) {
                _errorsWereDisplayed = true;
                EnableErrorsControls(true);
                SetTaskbarProgressState();
            }
        }
        if (progressCompleted != 0) {
            if (IS_UNDEFINED_VAL(progressTotal)) {
                if (IS_DEFINED_VAL(_prevRemainingSec)) {
                    INIT_AS_UNDEFINED(_prevRemainingSec)
                    SetItemText(IDT_PROGRESS_REMAINING_VAL, L"");
                }
            } else {
                UInt64 remainingTime = 0;
                if (progressCompleted < progressTotal)
                    remainingTime = MyMultAndDiv(_elapsedTime, progressTotal - progressCompleted,
                                                 progressCompleted);
                UInt64 remainingSec = remainingTime / 1000;
                if (remainingSec != _prevRemainingSec) {
                    _prevRemainingSec = remainingSec;
                    wchar_t s[40];
                    GetTimeString(remainingSec, s);
                    SetItemText(IDT_PROGRESS_REMAINING_VAL, s);
                }
            }
            {
                const UInt64 elapsedTime = (_elapsedTime == 0) ? 1 : _elapsedTime;
                // 22.02: progressCompleted can be for number of files
                UInt64 v = (completed * 1000) / elapsedTime;
                Byte c = 0;
                unsigned moveBits = 0;
                if (v >= ((UInt64)10000 << 10)) {
                    moveBits = 20;
                    c = 'M';
                } else if (v >= ((UInt64)10000 << 0)) {
                    moveBits = 10;
                    c = 'K';
                }
                v >>= moveBits;
                if (moveBits != _prevSpeed_MoveBits || v != _prevSpeed) {
                    _prevSpeed_MoveBits = moveBits;
                    _prevSpeed = v;
                    wchar_t s[40];
                    ConvertUInt64ToString(v, s);
                    unsigned pos = MyStringLen(s);
                    s[pos++] = ' ';
                    if (moveBits != 0)
                        s[pos++] = c;
                    s[pos++] = 'B';
                    s[pos++] = '/';
                    s[pos++] = 's';
                    s[pos++] = 0;
                    SetItemText(IDT_PROGRESS_SPEED_VAL, s);
                }
            }
        }
        {
            UInt64 percent = 0;
            const int transportPercent = TransportPercent.load();
            if (IS_UNDEFINED_VAL(progressTotal) && transportPercent >= 0)
                percent = UInt64(transportPercent);
            {
                if (IS_DEFINED_VAL(progressTotal)) {
                    percent = progressCompleted * 100;
                    if (progressTotal != 0)
                        percent /= progressTotal;
                }
            }
            if (percent != _prevPercentValue) {
                _prevPercentValue = percent;
                needSetTitle = true;
            }
        }
        {
            wchar_t s[64];
            ConvertUInt64ToString(completedFiles, s);
            if (_filesStr_Prev != s) {
                _filesStr_Prev = s;
                SetItemText(IDT_PROGRESS_FILES_VAL, s);
            }
            s[0] = 0;
            if (IS_DEFINED_VAL(totalFiles)) {
                MyStringCopy(s, L" / ");
                ConvertUInt64ToString(totalFiles, s + MyStringLen(s));
            }
            if (_filesTotStr_Prev != s) {
                _filesTotStr_Prev = s;
                SetItemText(IDT_PROGRESS_FILES_TOTAL, s);
            }
        }
        const UInt64 packSize = CompressingMode ? outSize : inSize;
        const UInt64 unpackSize = CompressingMode ? inSize : outSize;
        if (IS_UNDEFINED_VAL(unpackSize) && IS_UNDEFINED_VAL(packSize)) {
            ShowSize(IDT_PROGRESS_PROCESSED_VAL, completed, _processed_Prev);
            ShowSize(IDT_PROGRESS_PACKED_VAL, UNDEFINED_VAL, _packed_Prev);
        } else {
            ShowSize(IDT_PROGRESS_PROCESSED_VAL, unpackSize, _processed_Prev);
            ShowSize(IDT_PROGRESS_PACKED_VAL, packSize, _packed_Prev);
            if (IS_DEFINED_VAL(packSize) && IS_DEFINED_VAL(unpackSize) && unpackSize != 0) {
                wchar_t s[32];
                const UInt64 ratio = packSize * 100 / unpackSize;
                if (_ratio_Prev != ratio) {
                    _ratio_Prev = ratio;
                    wchar_t *dest = ConvertUInt64ToString(ratio, s);
                    dest[0] = '%';
                    dest[1] = 0;
                    SetItemText(IDT_PROGRESS_RATIO_VAL, s);
                }
            }
        }
    }
    if (needSetTitle || titleFileName_Changed)
        SetTitleText();
    if (status_Changed) {
        UString s = _status;
        ReduceString(s, _numReduceSymbols);
        SetItemText(IDT_PROGRESS_STATUS, s);
    }
    if (curFilePath_Changed) {
        UString s1, s2;
        if (_isDir)
            s1 = _filePath;
        else {
            int slashPos = _filePath.ReverseFind_PathSepar();
            if (slashPos >= 0) {
                s1.SetFrom(_filePath, (unsigned)(slashPos + 1));
                s2 = _filePath.Ptr((unsigned)(slashPos + 1));
            } else
                s2 = _filePath;
        }
        ReduceString(s1, _numReduceSymbols);
        ReduceString(s2, _numReduceSymbols);
        s1.Add_LF();
        s1 += s2;
        SetItemText(IDT_PROGRESS_FILE_NAME, s1);
    }
}

void CProgressDialog::SetTitleText() {
    UString s;
    if (Sync.Get_Paused()) {
        s += _paused_String;
        s.Add_Space();
    }
    if (IS_DEFINED_VAL(_prevPercentValue)) {
        s.Add_UInt64(_prevPercentValue);
        s.Add_Char('%');
    }
    s.Add_Space();
    {
        unsigned len = s.Len();
        s += MainAddTitle;
        AddToTitle(s);
        s.DeleteFrom(len);
    }
    s += _title;
    if (!_titleFileName.IsEmpty()) {
        UString fileName = _titleFileName;
        ReduceString(fileName, kTitleFileNameSizeLimit);
        s.Add_Space();
        s += fileName;
    }
    SetText(s);
}
