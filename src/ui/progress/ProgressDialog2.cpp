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
#include "ui/dialogs/qtmodalresult.h"
#include "ui/lifecycle/qttaskpresentation.h"
#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QMetaObject>
#include <QPushButton>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimerEvent>

using namespace NWindows;

extern bool g_DisableUserQuestions;

static const unsigned kCloseMessage = 0x8000 + 1; // Source WM_APP + 1.
static const unsigned kCreateDelay = 500;         // Desktop source, not Windows CE.

static unsigned GetNumDigits(unsigned val) {
    unsigned i = 0;
    for (;;) {
        i++;
        val /= 10;
        if (val == 0)
            return i;
    }
}

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

void CProgressDialog::EnableErrorsControls(bool enable) { ShowMessages(enable); }

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
    if (_progressBar_Range == ~UInt64(0)) {
        m_ProgressBar.SetRange32(0, paused ? 1 : 0);
        m_ProgressBar.SetPos(0);
    }
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
    if (_messageList->InsertItem(itemIndex, sz) == (int)itemIndex) {
        _messageList->SetSubItem(itemIndex, 1, message);
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
                _messageList->SetColumnWidthAuto(0);
                _messageList->SetColumnWidthAuto(1);
                _numAutoSizeMessages = _numPostedMessages;
            }
    }
}

static void ListView_GetSelected(CListView &listView, CUIntVector &vector) {
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
    ListView_GetSelected(*_messageList, indexes);
    UString s;
    unsigned numIndexes = indexes.Size();
    if (numIndexes == 0)
        numIndexes = (unsigned)_messageList->topLevelItemCount();
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
        if (_waitCloseByCancelButton)
            SetText(_title);
        else
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
    // A stopped checkpoint can wake safely. Clear the dialog's paused state
    // without asking the transport to resume.
    if (Sync.Get_Paused()) {
        Sync.Set_Paused(false);
        SetTitleText();
    }
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
    QtTaskPresentation::BeginWork(*this);
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
    if (!MessagesDisplayed && !DeferResultReview)
        if (!DisableUserQuestions)
            MessageBox(wndParent,
                       QtDiagnosticText(QT_TRANSLATE_NOOP("SevenZipDiagnostics", "Progress Error")),
                       L"7-Zip", true);
    return res;
}

bool CProgressDialog::OnExternalCloseMessage() {
    if (!DeferResultReview)
        QtTaskPresentation::SetPhase(*this, QtOperationPhase::Reviewing);
    SetTaskbarProgressState(TBPF_NOPROGRESS);
    UpdateStatInfo(true);
    HideItem(IDC_PROGRESS1);
    ShowProgressPercent({});
    SetText(_title);
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
    const HRESULT result = fm.Result.value_or(Sync.Get_Stopped() ? E_ABORT : S_OK);
    const bool isError = !fm.ErrorMessage.Message.IsEmpty() ||
                         (result != E_ABORT && (fm.HadErrors || FAILED(result)));
    const bool cancelled = !isError && result == E_ABORT;
    const bool hasSuccessReport = result == S_OK && !isError && !fm.Partial && !thereAreMessages &&
                                  !fm.OkMessage.Message.IsEmpty();
    if (!DisableUserQuestions && !DeferResultReview &&
        (isError || cancelled || fm.Partial || thereAreMessages || hasSuccessReport)) {
        _waitCloseByCancelButton = true;
        UpdateMessagesDialog();
        const UString &detail = isError ? fm.ErrorMessage.Message : fm.OkMessage.Message;
        if (!detail.IsEmpty())
            AddMessage(detail);
        else if (isError && _messageStrings.IsEmpty())
            AddMessage(HResultToMessage(result == S_OK ? E_FAIL : result));
        EnableErrorsControls(!_messageStrings.IsEmpty());
        const char *summary =
            isError     ? QT_TRANSLATE_NOOP("ProgressRedesign", "Operation failed")
            : cancelled ? QT_TRANSLATE_NOOP("ProgressRedesign",
                                            "Operation cancelled. Some changes may have completed.")
            : hasSuccessReport
                ? QT_TRANSLATE_NOOP("ProgressRedesign", "Operation completed")
                : QT_TRANSLATE_NOOP("ProgressRedesign", "Completed with skipped items or messages");
        QtBindLocalText(GetItem(IDT_PROGRESS_STATUS), "ProgressRedesign", summary);
        GetItem(IDT_PROGRESS_STATUS)->show();
        return true;
    }
    MessagesDisplayed =
        !DeferResultReview || !(isError || cancelled || fm.Partial || thereAreMessages);
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
            DismissStopQuestion();
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
            QtTaskPresentation::ResultReviewed();
            MessagesDisplayed = true;
            End(IDCLOSE);
            return true;
        }
        if (_cancelWasPressed)
            return true;
        {
            const bool paused = Sync.Get_Paused();
            const bool canPause = !_transport || _transport->CanPause();
            if (!paused && canPause)
                OnPauseButton();
            _inCancelMessageBox = true;
            const int res = AskCancel(this, _title);
            _inCancelMessageBox = false;
            if ((res != IDYES || _externalCloseMessageWasReceived) && canPause &&
                Sync.Get_Paused() != paused)
                OnPauseButton();
            if (_externalCloseMessageWasReceived) {
                OnExternalCloseMessage();
                return true;
            }
            if (res != IDYES)
                return true;
            _cancelWasPressed = true;
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
    HideItem(IDB_PAUSE);
    GetItem(IDCANCEL)->setEnabled(false);
    QtBindLocalText(GetItem(IDCANCEL), "ProgressRedesign",
                    QT_TRANSLATE_NOOP("ProgressRedesign", "Stopping…"));
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

void CProgressDialog::SetOperationResult(HRESULT result, bool partial) {
    NSynchronization::CCriticalSectionLock lock(Sync._cs);
    Sync.FinalMessage.Result = result;
    Sync.FinalMessage.Partial = partial;
}

void CProgressDialog::RetireStopQuestion() { DismissStopQuestion(); }

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
    sync.FinalMessage.Result = Result;
    sync.FinalMessage.Partial = FinalMessage.Partial;
    sync.FinalMessage.HadErrors = FinalMessage.HadErrors;
}
UString HResultToMessage(HRESULT errorCode) {
    if (errorCode == E_OUTOFMEMORY)
        return LangString(IDS_MEM_ERROR);
    else
        return NError::MyFormatMessage(errorCode);
}
