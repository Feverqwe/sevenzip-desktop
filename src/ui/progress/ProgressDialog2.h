// Adapted from 7-Zip CPP/7zip/UI/FileManager/ProgressDialog2.h (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Source presentation/modal/thread/cancel bodies;
// Qt and external-engine services are narrow integration boundaries.
#ifndef ZIP7_INC_PROGRESS_DIALOG_2_H
#define ZIP7_INC_PROGRESS_DIALOG_2_H

#include "Common/MyCom.h"
#include "Windows/ErrorMsg.h"
#include "Windows/Thread.h"
#include "ui/progress/progresssync.h"
#include "ui/progress/qtprogresscontrols.h"
#include "ui/progress/qtprogresstransport.h"
#include <QPointer>
#include <QStringList>
#include <atomic>
#ifdef _WIN32
#include "7zip/UI/FileManager/MyWindowsNew.h"
#else
#include "ui/progress/qtprogresswait.h"
#include "ui/progress/qttaskbarprogress.h"
#endif

class CListView;
void GetTimeString(UInt64 timeValue, wchar_t *s);
UString HResultToMessage(HRESULT errorCode);

// Source dialog owner. Application consumers migrate in P0-10.1/P0-10.2.
class CProgressDialog : public CQtProgressDialog {
    bool _isDir;
    std::atomic<bool> _wasCreated;
    std::atomic<bool> _needClose;
    bool _errorsWereDisplayed;
    bool _waitCloseByCancelButton;
    bool _cancelWasPressed;
    bool _inCancelMessageBox;
    bool _externalCloseMessageWasReceived;

  public:
    bool WaitMode;
    bool MessagesDisplayed;
    bool CompressingMode;
    bool ShowCompressionInfo;
    // A composed operation reviews its settled result after publication/rollback.
    bool DeferResultReview = false;
    bool CancellationMayLeaveChanges = true;

  private:
    unsigned _numPostedMessages;
    unsigned _numAutoSizeMessages;
    unsigned _numMessages;
    UString _titleFileName;
    UString _filePath;
    UString _status;
    UString _pause_String;
    UString _continue_String;
    UString _paused_String;
    int _buttonSizeX;
    int _buttonSizeY;
    int _timer;
    UString _title;

    class CU64ToI32Converter {
        unsigned _numShiftBits;
        UInt64 _range;

      public:
        CU64ToI32Converter() : _numShiftBits(0), _range(1) {}
        void Init(UInt64 range) {
            _range = range;
            // Windows CE doesn't like big number for ProgressBar.
            for (_numShiftBits = 0; range >= ((UInt32)1 << 15); _numShiftBits++)
                range >>= 1;
        }
        int Count(UInt64 val) {
            int res = (int)(val >> _numShiftBits);
            if (val == _range)
                res++;
            return res;
        }
    };
    CU64ToI32Converter _progressConv;
    UInt64 _progressBar_Pos;
    UInt64 _progressBar_Range;
    CQtProgressBar m_ProgressBar;
    CListView *_messageList = nullptr;
    UStringVector _messageStrings;
#ifdef _WIN32
    CMyComPtr<ITaskbarList3> _taskbarList;
#else
    std::unique_ptr<CQtTaskbarProgress> _taskbarList;
#endif
    // Guarded Qt window lifetime; convert to actual HWND only at the Win32 call.
    QPointer<QWidget> _hwndForTaskbar;
    UInt32 _prevTime;
    UInt64 _elapsedTime;
    UInt64 _prevPercentValue;
    UInt64 _prevElapsedSec;
    UInt64 _prevRemainingSec;
    UInt64 _totalBytes_Prev;
    UInt64 _processed_Prev;
    UInt64 _packed_Prev;
    UInt64 _ratio_Prev;
    UString _filesStr_Prev;
    UString _filesTotStr_Prev;
    unsigned _numReduceSymbols;
    unsigned _prevSpeed_MoveBits;
    UInt64 _prevSpeed;
    NWindows::NSynchronization::CManualResetEvent _createDialogEvent;
    NWindows::NSynchronization::CManualResetEvent _dialogCreatedEvent;

    void AddToTitle(const wchar_t *s);
    void SetTaskbarProgressState(TBPFLAG tbpFlags) {
        if (_taskbarList && _hwndForTaskbar) {
#ifdef _WIN32
            _taskbarList->SetProgressState(GetNativeTaskbarWindow(_hwndForTaskbar), tbpFlags);
#else
            _taskbarList->SetProgressState(_hwndForTaskbar, tbpFlags);
#endif
        }
    }
    void SetTaskbarProgressState();
    void SetProgressRange(UInt64 range);
    void SetProgressPos(UInt64 pos);
    void ShowSize(unsigned id, UInt64 val, UInt64 &prev);
    void UpdateStatInfo(bool showAll);
    bool OnSize(int xSize, int ySize);
    void SetTitleText();
    void SetPauseText();
    void OnPauseButton();
    bool OnButtonClicked(unsigned buttonID);
    bool OnMessage(unsigned message);
    bool OnExternalCloseMessage();
    void CheckNeedClose();
    void PostMsg(unsigned message);
    void EnableErrorsControls(bool enable);
    void AddMessageDirect(const wchar_t *message, bool needNumber);
    void AddMessage(const wchar_t *message);
    void UpdateMessagesDialog();
    void CopyToClipboard();
    void OnOK();
    void OnCancel();
    bool OnTimer();
    bool OnNotify(QKeyEvent *event);
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;
    void timerEvent(QTimerEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

  protected:
    // Percentage-only transports lack byte counters. Do not invent
    // byte/file totals; apply only to the progress/title presentation fallback.
    std::atomic<int> TransportPercent{-1};
    IQtProgressTransport *_transport = nullptr;
    virtual HRESULT SetPausedTransport(bool paused) {
        return _transport ? _transport->SetPaused(paused) : S_OK;
    }
#ifndef _WIN32
    CQtProgressStartWait _startWait;
#endif

  public:
    CProgressSync Sync;
    int IconID;
    QPointer<QWidget> MainWindow;
    UString MainTitle;
    UString MainAddTitle;
    // Explicit application question policy instead of the original FM global.
    bool DisableUserQuestions = false;
    void AttachTransport(IQtProgressTransport *transport) { _transport = transport; }
    void SetTransportPercent(int percent) {
        if (percent >= -1 && percent <= 100)
            TransportPercent.store(percent);
    }
    explicit CProgressDialog(QWidget *parent = nullptr, const UString &title = UString());
    ~CProgressDialog() override;
    bool OnInit();
    void WaitCreating() {
        _createDialogEvent.Set();
#ifndef _WIN32
        _startWait.SignalRequested();
#endif
        _dialogCreatedEvent.Lock();
    }
    qintptr Create(const UString &title, NWindows::CThread &thread, QWidget *wndParent = nullptr);
    void ProcessWasFinished();
    void SetOperationResult(HRESULT result, bool partial = false);
    void RetireStopQuestion();
    struct ResultPresentation {
        QString title;
        HRESULT result = S_OK;
        QStringList diagnostics;
        QString source;
        QString destination;
        bool partial = false;
    };
    static void PresentResult(QWidget *parent, const ResultPresentation &request);
    virtual void ProcessWasFinished_GuiVirt() {}
    void accept() override;
    void reject() override;
};

class CProgressCloser {
    CProgressDialog *_p;

  public:
    CProgressCloser(CProgressDialog &p Z7_lifetimebound) : _p(&p) {}
    ~CProgressCloser() { _p->ProcessWasFinished(); }
};

class CProgressThreadVirt : public CProgressDialog {
    static THREAD_FUNC_DECL MyThreadFunction(void *param);

  protected:
    FStringVector ErrorPaths;
    CProgressFinalMessage FinalMessage;
    virtual HRESULT ProcessVirt() = 0;

  public:
    HRESULT Result;
    bool ThreadFinishedOK;
    void Process();
    void AddErrorPath(const FString &path) { ErrorPaths.Add(path); }
    HRESULT Create(const UString &title, QWidget *parentWindow = nullptr);
    CProgressThreadVirt() : Result(E_FAIL), ThreadFinishedOK(false) {}
    CProgressMessageBoxPair &GetMessagePair(bool isError) Z7_lifetimebound {
        return isError ? FinalMessage.ErrorMessage : FinalMessage.OkMessage;
    }
};

#endif
