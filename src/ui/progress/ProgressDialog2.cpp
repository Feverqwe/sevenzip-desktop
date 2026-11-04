// Adapted from 7-Zip CPP/7zip/UI/FileManager/ProgressDialog2.cpp (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "ui/progress/ProgressDialog2.h"
#include "7zip/UI/FileManager/ProgressDialog2Res.h"
#include "7zip/UI/GUI/ExtractRes.h"
#include "Common/IntToString.h"
#include "Common/StringConvert.h"
#include "filemanager/views/qtlistview.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "settings/ViewSettings.h"
#include "ui/diagnostics/qtdiagnostictext.h"
#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMetaObject>
#include <QPushButton>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimerEvent>

using namespace NWindows;

extern bool g_DisableUserQuestions;

static const unsigned kCloseMessage = 0x8000 + 1; // Source WM_APP + 1.
static const unsigned kCreateDelay = 500;         // Desktop source, not Windows CE.
#ifndef _WIN32
static const unsigned IDCANCEL = 2;
static const unsigned IDCLOSE = 8;
static const unsigned IDYES = 6;
#endif

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

static unsigned GetNumDigits(unsigned val) {
    unsigned i = 0;
    for (;;) {
        i++;
        val /= 10;
        if (val == 0)
            return i;
    }
}

static const unsigned kTitleFileNameSizeLimit = 36;
static const unsigned kCurrentFileNameSizeLimit = 82;

CProgressDialog::CProgressDialog(QWidget *parent, const UString &title)
    : CQtProgressDialog(parent), _isDir(false), _wasCreated(false), _needClose(false),
      _errorsWereDisplayed(false), _waitCloseByCancelButton(false), _cancelWasPressed(false),
      _inCancelMessageBox(false), _externalCloseMessageWasReceived(false), WaitMode(false),
      MessagesDisplayed(false), CompressingMode(true), ShowCompressionInfo(true),
      _numPostedMessages(0), _numAutoSizeMessages(0), _numMessages(0), _timer(0), _title(title),
      IconID(-1), MainWindow(nullptr) {
    // GUI Parse1 sets the original process policy before constructing
    // workers. Keep the existing per-operation Qt override for other callers.
    DisableUserQuestions = g_DisableUserQuestions;
    if (_dialogCreatedEvent.Create() != S_OK)
        throw 1334987;
    if (_createDialogEvent.Create() != S_OK)
        throw 1334987;
#ifdef _WIN32
    CoCreateInstance(CLSID_TaskbarList, NULL, CLSCTX_INPROC_SERVER, IID_ITaskbarList3,
                     (void **)&_taskbarList);
    if (_taskbarList)
        _taskbarList->HrInit();
#else
    _taskbarList.reset(new CQtTaskbarProgress);
    if (_taskbarList->HrInit() != S_OK)
        _taskbarList.reset();
#endif
    connect(static_cast<QPushButton *>(GetItem(IDB_PAUSE)), &QPushButton::clicked, this,
            &CProgressDialog::OnPauseButton);
    connect(static_cast<QPushButton *>(GetItem(IDCANCEL)), &QPushButton::clicked, this,
            [this] { OnButtonClicked(IDCANCEL); });
    GetItem(IDL_PROGRESS_MESSAGES)->installEventFilter(this);
}

CProgressDialog::~CProgressDialog() {
    SetTaskbarProgressState(TBPF_NOPROGRESS);
    AddToTitle(L"");
}

void CProgressDialog::AddToTitle(const wchar_t *s) {
    if (MainWindow)
        MainWindow->setWindowTitle(ToQString((UString)s + MainTitle));
}

void CProgressDialog::SetTaskbarProgressState() {
    if (_taskbarList && _hwndForTaskbar) {
        TBPFLAG tbpFlags;
        if (Sync.Get_Paused())
            tbpFlags = TBPF_PAUSED;
        else
            tbpFlags = _errorsWereDisplayed ? TBPF_ERROR : TBPF_NORMAL;
        SetTaskbarProgressState(tbpFlags);
    }
}

void CProgressDialog::EnableErrorsControls(bool enable) {
    ShowItem_Bool(IDT_PROGRESS_ERRORS, enable);
    ShowItem_Bool(IDT_PROGRESS_ERRORS_VAL, enable);
    ShowItem_Bool(IDL_PROGRESS_MESSAGES, enable);
}

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
    _messageList.Attach(GetItem(IDL_PROGRESS_MESSAGES));
    _wasCreated = true;
    _dialogCreatedEvent.Set();
    GetItemText(IDB_PAUSE, _pause_String);
    LangString(IDS_CONTINUE, _continue_String);
    LangString(IDS_PROGRESS_PAUSED, _paused_String);
    SetText(_title);
    SetPauseText();
    if (_transport && !_transport->CanPause())
        GetItem(IDB_PAUSE)->setEnabled(false);
    _messageList.InsertColumn(0, L"", 40);
    _messageList.InsertColumn(1, L"", 460);
    _messageList.SetColumnWidthAuto(0);
    _messageList.SetColumnWidthAuto(1);
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

void CProgressDialog::SetPauseText() {
    SetItemText(IDB_PAUSE, Sync.Get_Paused() ? _continue_String : _pause_String);
    SetTitleText();
}

void CProgressDialog::OnPauseButton() {
    bool paused = !Sync.Get_Paused();
    Sync.Set_Paused(paused);
    const HRESULT result = SetPausedTransport(paused);
    if (result != S_OK) {
        Sync.Set_Paused(!paused);
        Sync.AddError_Code_Name(result, nullptr);
        return;
    }
    UInt32 curTime = GetTickCount();
    if (paused)
        _elapsedTime += (curTime - _prevTime);
    SetTaskbarProgressState();
    _prevTime = curTime;
    SetPauseText();
}

void CProgressDialog::AddMessageDirect(const wchar_t *message, bool needNumber) {
    wchar_t sz[16];
    sz[0] = 0;
    if (needNumber)
        ConvertUInt32ToString(_numMessages + 1, sz);
    const unsigned itemIndex = _messageStrings.Size();
    if (_messageList.InsertItem(itemIndex, sz) == (int)itemIndex) {
        _messageList.SetSubItem(itemIndex, 1, message);
        _messageStrings.Add(message);
    }
}

void CProgressDialog::AddMessage(const wchar_t *message) {
    UString s = message;
    bool needNumber = true;
    while (!s.IsEmpty()) {
        const int pos = s.Find(L'\n');
        if (pos < 0)
            break;
        AddMessageDirect(s.Left((unsigned)pos), needNumber);
        needNumber = false;
        s.DeleteFrontal((unsigned)pos + 1);
    }
    AddMessageDirect(s, needNumber);
    _numMessages++;
}

void CProgressDialog::UpdateMessagesDialog() {
    UStringVector messages;
    {
        NSynchronization::CCriticalSectionLock lock(Sync._cs);
        const unsigned num = Sync.Messages.Size();
        if (num > _numPostedMessages) {
            messages.ClearAndReserve(num - _numPostedMessages);
            for (unsigned i = _numPostedMessages; i < num; i++)
                messages.AddInReserved(Sync.Messages[i]);
            _numPostedMessages = num;
        }
    }
    if (!messages.IsEmpty()) {
        FOR_VECTOR(i, messages)
        AddMessage(messages[i]);
        if (_numPostedMessages < 1000000 || _numAutoSizeMessages < 100)
            if (_numAutoSizeMessages < 100 ||
                GetNumDigits(_numPostedMessages) > GetNumDigits(_numAutoSizeMessages)) {
                _messageList.SetColumnWidthAuto(0);
                _messageList.SetColumnWidthAuto(1);
                _numAutoSizeMessages = _numPostedMessages;
            }
    }
}

static void ListView_GetSelected(CQtProgressListView &listView, CUIntVector &vector) {
    vector.Clear();
    int index = -1;
    for (;;) {
        index = listView.GetNextSelectedItem(index);
        if (index < 0)
            break;
        vector.Add((unsigned)index);
    }
}

void CProgressDialog::CopyToClipboard() {
    CUIntVector indexes;
    ListView_GetSelected(_messageList, indexes);
    UString s;
    unsigned numIndexes = indexes.Size();
    if (numIndexes == 0)
        numIndexes = (unsigned)_messageList.GetItemCount();
    for (unsigned i = 0; i < numIndexes; i++) {
        const unsigned index = (i < indexes.Size() ? indexes[i] : i);
        s += _messageStrings[index];
        {
            s +=
#ifdef _WIN32
                "\r\n"
#else
                "\n"
#endif
                ;
        }
    }
    ClipboardSetText(s);
}

void CProgressDialog::showEvent(QShowEvent *event) {
    if (!_wasCreated)
        OnInit();
    OnSize(width(), height());
    CQtProgressDialog::showEvent(event);
}
void CProgressDialog::resizeEvent(QResizeEvent *event) {
    if (_wasCreated)
        OnSize(event->size().width(), event->size().height());
    CQtProgressDialog::resizeEvent(event);
}
void CProgressDialog::changeEvent(QEvent *event) {
    if (event->type() == QEvent::LanguageChange && _wasCreated) {
        LangString(IDB_PAUSE, _pause_String);
        LangString(IDS_CONTINUE, _continue_String);
        LangString(IDS_PROGRESS_PAUSED, _paused_String);
        SetPauseText();
        OnSize(width(), height());
    }
    CQtProgressDialog::changeEvent(event);
}
void CProgressDialog::timerEvent(QTimerEvent *event) {
    if (event->timerId() == _timer) {
        OnTimer();
        return;
    }
    CQtProgressDialog::timerEvent(event);
}
bool CProgressDialog::eventFilter(QObject *watched, QEvent *event) {
    if (watched == GetItem(IDL_PROGRESS_MESSAGES) && event->type() == QEvent::KeyPress &&
        OnNotify(static_cast<QKeyEvent *>(event)))
        return true;
    return CQtProgressDialog::eventFilter(watched, event);
}
bool CProgressDialog::OnTimer() {
    if (Sync.Get_Paused())
        return true;
    CheckNeedClose();
    UpdateStatInfo(false);
    return true;
}
bool CProgressDialog::OnNotify(QKeyEvent *key) {
    const bool control = key->modifiers().testFlag(Qt::ControlModifier) ||
                         key->modifiers().testFlag(Qt::MetaModifier);
    switch (key->key()) {
    case Qt::Key_A:
        if (control) {
            static_cast<CListView *>(GetItem(IDL_PROGRESS_MESSAGES))->selectAll();
            return true;
        }
        break;
    case Qt::Key_Insert:
    case Qt::Key_C:
        if (control) {
            CopyToClipboard();
            return true;
        }
        break;
    }
    return false;
}
void CProgressDialog::OnOK() {}
void CProgressDialog::OnCancel() {
    Sync.Set_Stopped(true);
    if (_transport)
        _transport->Cancel();
}
void CProgressDialog::accept() { OnOK(); }
void CProgressDialog::reject() { OnButtonClicked(IDCANCEL); }

struct CWaitCursor {
    CWaitCursor() { QApplication::setOverrideCursor(Qt::WaitCursor); }
    ~CWaitCursor() { QApplication::restoreOverrideCursor(); }
};

qintptr CProgressDialog::Create(const UString &title, NWindows::CThread &thread,
                                QWidget *wndParent) {
    qintptr res = 0;
    try {
        if (WaitMode) {
            CWaitCursor waitCursor;
#ifdef _WIN32
            HANDLE h[] = {thread, _createDialogEvent};
            const DWORD res2 = WaitForMultipleObjects(Z7_ARRAY_SIZE(h), h, FALSE, kCreateDelay);
            if (res2 == WAIT_OBJECT_0 && !Sync.ThereIsMessage())
                return 0;
#else
            const auto res2 = _startWait.Wait(kCreateDelay);
            if (res2 == CQtProgressStartWait::EResult::Finished && !Sync.ThereIsMessage()) {
                thread.Wait_Close();
                return 0;
            }
#endif
        }
        _title = title;
        // Desktop resource has only IDD_PROGRESS; source UNDER_CE sizing is absent.
        res = CQtProgressDialog::CreateModal(wndParent);
    } catch (...) {
        _wasCreated = true;
        _dialogCreatedEvent.Set();
    }
    thread.Wait_Close();
    if (!MessagesDisplayed)
        if (!DisableUserQuestions)
            MessageBox(wndParent,
                       QtDiagnosticText(QT_TRANSLATE_NOOP("SevenZipDiagnostics", "Progress Error")),
                       L"7-Zip", true);
    return res;
}

bool CProgressDialog::OnExternalCloseMessage() {
    SetTaskbarProgressState(TBPF_NOPROGRESS);
    UpdateStatInfo(true);
    SetItemText(IDCANCEL, LangString(IDS_CLOSE));
    QtBindLangText(GetItem(IDCANCEL), IDS_CLOSE);
    HideItem(IDB_PAUSE);
    // QDialogButtonBox rebuilds its focus chain when action buttons disappear.
    SetDefaultButton(IDCANCEL);
    ProcessWasFinished_GuiVirt();
    bool thereAreMessages;
    CProgressFinalMessage fm;
    {
        NSynchronization::CCriticalSectionLock lock(Sync._cs);
        thereAreMessages = !Sync.Messages.IsEmpty();
        fm = Sync.FinalMessage;
    }
    const bool isError = !fm.ErrorMessage.Message.IsEmpty();
    if (isError || !thereAreMessages) {
        MessagesDisplayed = true;
        CProgressMessageBoxPair &message = isError ? fm.ErrorMessage : fm.OkMessage;
        if (!message.Message.IsEmpty()) {
            if (message.Title.IsEmpty())
                message.Title = "7-Zip";
            if (!DisableUserQuestions)
                MessageBox(this, message.Message, message.Title, isError);
        }
    }
    if (!DisableUserQuestions)
        if (thereAreMessages && !_cancelWasPressed) {
            _waitCloseByCancelButton = true;
            UpdateMessagesDialog();
            return true;
        }
    End(0);
    return true;
}

bool CProgressDialog::OnMessage(unsigned message) {
    switch (message) {
    case kCloseMessage: {
        if (_timer) {
            killTimer(_timer);
            _timer = 0;
        }
        if (_inCancelMessageBox) {
            _externalCloseMessageWasReceived = true;
            break;
        }
        return OnExternalCloseMessage();
    }
    }
    return false; // Other native UI messages are dispatched by Qt.
}
void CProgressDialog::PostMsg(unsigned message) {
    QMetaObject::invokeMethod(this, [this, message] { OnMessage(message); }, Qt::QueuedConnection);
}
bool CProgressDialog::OnButtonClicked(unsigned buttonID) {
    switch (buttonID) {
    case IDCANCEL:
        if (_waitCloseByCancelButton) {
            MessagesDisplayed = true;
            End(IDCLOSE);
            break;
        }
        if (_cancelWasPressed)
            return true;
        {
            const bool paused = Sync.Get_Paused();
            const bool canPause = !_transport || _transport->CanPause();
            if (!paused && canPause)
                OnPauseButton();
            _inCancelMessageBox = true;
            const int res = AskCancel(this, LangString(IDS_PROGRESS_ASK_CANCEL), _title);
            _inCancelMessageBox = false;
            if (res == IDYES)
                _cancelWasPressed = true;
            if (canPause && Sync.Get_Paused() != paused)
                OnPauseButton();
            if (_externalCloseMessageWasReceived) {
                OnExternalCloseMessage();
                return true;
            }
            if (!_cancelWasPressed)
                return true;
            MessagesDisplayed = true;
        }
        break;
    case IDB_PAUSE:
        OnPauseButton();
        return true;
    default:
        return false;
    }
    // Original CModalDialog::OnButtonClicked dispatches IDCANCEL to OnCancel.
    OnCancel();
    return true;
}
void CProgressDialog::CheckNeedClose() {
    if (_needClose) {
        PostMsg(kCloseMessage);
        _needClose = false;
    }
}
void CProgressDialog::ProcessWasFinished() {
    if (!WaitMode)
        WaitCreating();
    if (_wasCreated)
        PostMsg(kCloseMessage);
    else
        _needClose = true;
#ifndef _WIN32
    // FileFolderPluginOpen has its own native worker entry point, not the
    // CProgressThreadVirt entry point. Wake the same finished latch;
    // Create still joins the native thread before releasing its owner.
    _startWait.SignalFinished();
#endif
}

THREAD_FUNC_DECL CProgressThreadVirt::MyThreadFunction(void *param) {
    auto *p = static_cast<CProgressThreadVirt *>(param);
    try {
        p->Process();
        p->ThreadFinishedOK = true;
    } catch (...) {
        p->Result = E_FAIL;
    }
#ifndef _WIN32
    p->_startWait.SignalFinished();
#endif
    return 0;
}
HRESULT CProgressThreadVirt::Create(const UString &title, QWidget *parentWindow) {
    NWindows::CThread thread;
    const WRes wres = thread.Create(MyThreadFunction, this);
    if (wres != 0)
        return HRESULT_FROM_WIN32(wres);
    CProgressDialog::Create(title, thread, parentWindow);
    return S_OK;
}
static void AddMessageToString(UString &dest, const UString &src) {
    if (!src.IsEmpty()) {
        if (!dest.IsEmpty())
            dest.Add_LF();
        dest += src;
    }
}
void CProgressThreadVirt::Process() {
    CProgressCloser closer(*this);
    UString m;
    try {
        Result = ProcessVirt();
    } catch (const wchar_t *s) {
        m = s;
    } catch (const UString &s) {
        m = s;
    } catch (const char *s) {
        m = QtDiagnosticText(s);
    } catch (int v) {
        m = QtDiagnosticText(QT_TRANSLATE_NOOP("SevenZipDiagnostics", "Error #%1"), (unsigned)v);
    } catch (...) {
        m = QtDiagnosticText(QT_TRANSLATE_NOOP("SevenZipDiagnostics", "Error"));
    }
    if (Result != E_ABORT) {
        if (m.IsEmpty() && Result != S_OK)
            m = HResultToMessage(Result);
    }
    AddMessageToString(m, FinalMessage.ErrorMessage.Message);
    {
        FOR_VECTOR(i, ErrorPaths) {
            if (i >= 32)
                break;
            AddMessageToString(m, fs2us(ErrorPaths[i]));
        }
    }
    CProgressSync &sync = Sync;
    NSynchronization::CCriticalSectionLock lock(sync._cs);
    if (m.IsEmpty()) {
        if (!FinalMessage.OkMessage.Message.IsEmpty())
            sync.FinalMessage.OkMessage = FinalMessage.OkMessage;
    } else {
        sync.FinalMessage.ErrorMessage.Message = m;
        if (Result == S_OK)
            Result = E_FAIL;
    }
}
UString HResultToMessage(HRESULT errorCode) {
    if (errorCode == E_OUTOFMEMORY)
        return LangString(IDS_MEM_ERROR);
    else
        return NError::MyFormatMessage(errorCode);
}
