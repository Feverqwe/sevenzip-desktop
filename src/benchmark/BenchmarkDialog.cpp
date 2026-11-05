// BenchmarkDialog.cpp

// Adapted from 7-Zip 0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "7zip/UI/Common/StdAfx.h"
#include "7zip/UI/FileManager/resourceGui.h"
#include "Common/Defs.h"
#include "Common/IntToString.h"
#include "Common/MyException.h"
#include "Common/StringConvert.h"
#include "Common/StringToInt.h"
#include "Windows/Synchronization.h"
#include "Windows/System.h"
#include "Windows/SystemInfo.h"
#include "Windows/Thread.h"
#include "archive/compression/MethodProps.h"
#include "filemanager/HelpUtils.h"
#include "localization/LangUtils.h"
#include "platform/cpu/CpuArch.h"

#include "7zip/MyVersion.h"

#include "benchmark/Bench.h"

#include "7zip/UI/GUI/BenchmarkDialogRes.h"
#include "benchmark/BenchmarkDialog.h"
#include "benchmark/qtbenchmarkcontrols.h"
#include "benchmark/qtbenchmarkservices.h"
#include "ui/diagnostics/qtdiagnostictext.h"
#include "ui/lifecycle/qttaskpresentation.h"
#include <QMessageBox>
#include <cwchar>

using namespace NWindows;

#define kHelpTopic "fm/benchmark.htm"

static const quintptr kTimerID = 4;
static const UINT kTimerElapse = 1000; // 1000

// use PRINT_ITER_TIME to show time of each iteration in log box
// #define PRINT_ITER_TIME

static const unsigned kRatingVector_NumBundlesMax = 20;

enum MyBenchMessages { k_Message_Finished = 0x8001 };

enum My_Message_WPARAM {
    k_Msg_WPARM_Thread_Finished = 0,
    k_Msg_WPARM_Iter_Finished,
    k_Msg_WPARM_Enc1_Finished
};

struct CBenchPassResult {
    CTotalBenchRes Enc;
    CTotalBenchRes Dec;
#ifdef PRINT_ITER_TIME
    DWORD Ticks;
#endif
    // CBenchInfo EncInfo; // for debug
    // CBenchPassResult() {};
};

struct CTotalBenchRes2 : public CTotalBenchRes {
    UInt64 UnpackSize;

    void Init() {
        CTotalBenchRes::Init();
        UnpackSize = 0;
    }

    void SetFrom_BenchInfo(const CBenchInfo &info) {
        NumIterations2 = 1;
        Generate_From_BenchInfo(info);
        UnpackSize = info.Get_UnpackSize_Full();
    }

    void Update_With_Res2(const CTotalBenchRes2 &r) {
        Update_With_Res(r);
        UnpackSize += r.UnpackSize;
    }
};

struct CSyncData {
    UInt32 NumPasses_Finished;
#ifdef PRINT_ITER_TIME
    DWORD TotalTicks;
#endif
    int RatingVector_DeletedIndex;
    // UInt64 RatingVector_NumDeleted;

    bool BenchWasFinished; // all passes were finished
    bool NeedPrint_Freq;
    bool NeedPrint_RatingVector;
    bool NeedPrint_Enc_1;
    bool NeedPrint_Enc;
    bool NeedPrint_Dec_1;
    bool NeedPrint_Dec;
    bool NeedPrint_Tot; // intermediate Total was updated after current pass

    // UInt64 NumEncProgress; // for debug
    // UInt64 NumDecProgress; // for debug
    // CBenchInfo EncInfo; // for debug

    CTotalBenchRes2 Enc_BenchRes_1;
    CTotalBenchRes2 Enc_BenchRes;

    CTotalBenchRes2 Dec_BenchRes_1;
    CTotalBenchRes2 Dec_BenchRes;

    void Init();
};

void CSyncData::Init() {
    NumPasses_Finished = 0;

    // NumEncProgress = 0;
    // NumDecProgress = 0;

    Enc_BenchRes.Init();
    Enc_BenchRes_1.Init();
    Dec_BenchRes.Init();
    Dec_BenchRes_1.Init();

#ifdef PRINT_ITER_TIME
    TotalTicks = 0;
#endif

    RatingVector_DeletedIndex = -1;
    // RatingVector_NumDeleted = 0;

    BenchWasFinished = NeedPrint_Freq = NeedPrint_RatingVector = NeedPrint_Enc_1 = NeedPrint_Enc =
        NeedPrint_Dec_1 = NeedPrint_Dec = NeedPrint_Tot = false;
}

struct CBenchProgressSync {
    bool Exit; // GUI asks BenchThread to Exit, and BenchThread reads that variable
    bool TextWasChanged;

    UInt32 NumThreads;
    UInt64 DictSize;
    UInt32 NumPasses_Limit;
    int Level;

    AString Text;

    /* BenchFinish_Task_HRESULT    - for result from benchmark code
       BenchFinish_Thread_HRESULT  - for Exceptions and service errors
               these arreos must be shown even if user escapes benchmark */
    HRESULT BenchFinish_Task_HRESULT;
    HRESULT BenchFinish_Thread_HRESULT;

    UInt32 NumFreqThreadsPrev;
    UString FreqString_Sync;
    UString FreqString_GUI;

    // must be written by benchmark thread, read by GUI thread */
    CRecordVector<CBenchPassResult> RatingVector;
    CSyncData sd;

    NWindows::NSynchronization::CCriticalSection CS;

    CBenchProgressSync() { NumPasses_Limit = 1; }

    void Init();

    void SendExit() {
        NWindows::NSynchronization::CCriticalSectionLock lock(CS);
        Exit = true;
    }
};

void CBenchProgressSync::Init() {
    Exit = false;

    BenchFinish_Task_HRESULT = S_OK;
    BenchFinish_Thread_HRESULT = S_OK;

    sd.Init();
    RatingVector.Clear();

    NumFreqThreadsPrev = 0;
    FreqString_Sync.Empty();
    FreqString_GUI.Empty();

    Text.Empty();
    TextWasChanged = true;
}

class CBenchmarkDialog;

struct CThreadBenchmark {
    CBenchmarkDialog *BenchmarkDialog;
    DECL_EXTERNAL_CODECS_LOC_VARS_DECL
    // HRESULT Result;

    HRESULT Process();
    static THREAD_FUNC_DECL MyThreadFunction(void *param) {
        /* ((CThreadBenchmark *)param)->Result = */
        ((CThreadBenchmark *)param)->Process();
        return 0;
    }
};

class CBenchmarkDialog : public CQtBenchmarkWindow {
    bool _finishTime_WasSet;

    bool WasStopped_in_GUI;
    bool ExitWasAsked_in_GUI;
    bool NeedRestart;

    bool RamSize_Defined;
    HRESULT StartResult = S_OK;

  public:
    bool TotalMode;

  private:
    CQtBenchmarkCombo m_Dictionary;
    CQtBenchmarkCombo m_NumThreads;
    CQtBenchmarkCombo m_NumPasses;
    CQtBenchmarkEdit _consoleEdit;
    quintptr _timer;

    UInt32 _startTime;
    UInt32 _finishTime;

    size_t RamSize;
    size_t RamSize_Limit;

    UInt32 NumPasses_Finished_Prev;

    UString ElapsedSec_Prev;

    void InitSyncNew() {
        NumPasses_Finished_Prev = (UInt32)(Int32)-1;
        ElapsedSec_Prev.Empty();
        Sync.Init();
    }

    virtual void OnInit() Z7_override;
    virtual void OnDestroy() Z7_override;
    virtual void OnMessage(UINT message, quintptr wParam) Z7_override;
    virtual void OnCommand(unsigned code, unsigned itemID) Z7_override;
    virtual void OnHelp() Z7_override;
    virtual void OnCancel() Z7_override;
    virtual void OnTimer(quintptr timerID) Z7_override;
    virtual void OnButtonClicked(unsigned buttonID) Z7_override;

    void Disable_Stop_Button();
    void OnStopButton();
    void RestartBenchmark();
    void StartBenchmark();

    void UpdateGui();

    void PrintTime();
    void PrintRating(UInt64 rating, UINT controlID);
    void PrintUsage(UInt64 usage, UINT controlID);
    void PrintBenchRes(const CTotalBenchRes2 &info, const UINT ids[]);

    UInt32 GetNumberOfThreads();
    size_t OnChangeDictionary();

    void SetItemText_Number(unsigned itemID, UInt64 val, const wchar_t *post = NULL);
    void Print_MemUsage(UString &s, UInt64 memUsage) const;
    bool IsMemoryUsageOK(UInt64 memUsage) const { return memUsage + (1 << 20) <= RamSize_Limit; }

    void MyKillTimer();

    void SendExit_Status(const wchar_t *message) {
        SetItemText(IDT_BENCH_ERROR_MESSAGE, message);
        Sync.SendExit();
    }

  public:
    CBenchProgressSync Sync;

    CObjectVector<CProperty> Props;
    IQtBenchmarkServices *Services = nullptr;

    AString Bench2Text; // The Qt console consumes the report as UTF-8 on every platform.

    NWindows::CThread _thread;
    CThreadBenchmark _threadBenchmark;

    CBenchmarkDialog()
        : WasStopped_in_GUI(false), ExitWasAsked_in_GUI(false), NeedRestart(false),
          TotalMode(false), _timer(0) {}

    ~CBenchmarkDialog() Z7_DESTRUCTOR_override;

    bool PostMsg_Finish(quintptr wparam) { return PostMsg(k_Message_Finished, wparam); }

    HRESULT OperationResult() const {
        if (StartResult != S_OK)
            return StartResult;
        if (Sync.BenchFinish_Thread_HRESULT != S_OK)
            return Sync.BenchFinish_Thread_HRESULT;
        if (Sync.BenchFinish_Task_HRESULT != S_OK)
            return Sync.BenchFinish_Task_HRESULT;
        const bool completed =
            TotalMode ? Sync.sd.NumPasses_Finished != 0 : Sync.sd.BenchWasFinished;
        return completed ? S_OK : E_ABORT;
    }

    qintptr Create(QWidget *wndParent = NULL) {
        return CQtBenchmarkWindow::Create(TotalMode ? IDD_BENCH_TOTAL : IDD_BENCH, wndParent);
    }
    void MessageBoxError(const wchar_t *message) {
        QMessageBox::critical(this, QStringLiteral("7-Zip"), QString::fromWCharArray(message));
    }
    void MessageBoxError_Status(LPCWSTR message) {
        UString s = QtDiagnosticText(QT_TRANSLATE_NOOP("SevenZipDiagnostics", "ERROR: "));
        s += message;
        MessageBoxError(s);
        SetItemText(IDT_BENCH_ERROR_MESSAGE, s);
    }
};

UString HResultToMessage(HRESULT errorCode);

#ifdef Z7_LANG
static const UInt32 kLangIDs[] = {IDT_BENCH_DICTIONARY,
                                  IDT_BENCH_MEMORY,
                                  IDT_BENCH_NUM_THREADS,
                                  IDT_BENCH_SIZE,
                                  IDT_BENCH_RATING_LABEL,
                                  IDT_BENCH_USAGE_LABEL,
                                  IDT_BENCH_RPU_LABEL,
                                  IDG_BENCH_COMPRESSING,
                                  IDG_BENCH_DECOMPRESSING,
                                  IDG_BENCH_TOTAL_RATING,
                                  IDT_BENCH_CURRENT,
                                  IDT_BENCH_RESULTING,
                                  IDT_BENCH_ELAPSED,
                                  IDT_BENCH_PASSES,
                                  IDB_STOP,
                                  IDB_RESTART};

static const UInt32 kLangIDs_RemoveColon[] = {IDT_BENCH_SPEED};

#endif

static const wchar_t *const kProcessingString = L"...";
static const wchar_t *const kGB = L" GB";
static const wchar_t *const kMB = L" MB";
static const wchar_t *const kKB = L" KB";
// static LPCTSTR const kMIPS = TEXT(" MIPS");
static const wchar_t *const kKBs = L" KB/s";

static const unsigned kMinDicLogSize = 18;

static const UInt32 kMinDicSize = (UInt32)1 << kMinDicLogSize;
static const size_t kMaxDicSize = (size_t)1 << (22 + sizeof(size_t) / 4 * 5);
// static const size_t kMaxDicSize = (size_t)1 << 16;
/*
#ifdef MY_CPU_64BIT
  (UInt32)(Int32)-1; // we can use it, if we want 4 GB buffer
  // (UInt32)15 << 28;
#else
  (UInt32)1 << 27;
#endif
*/

static int ComboBox_Add_UInt32(CQtBenchmarkCombo &cb, UInt32 v) {
    WCHAR s[16];
    ConvertUInt32ToString(v, s);
    return (int)cb.AddString_SetItemData(s, (qintptr)v);
}

void CBenchmarkDialog::OnInit() {
#ifdef Z7_LANG
    LangSetWindowText(*this, IDD_BENCH);
    LangSetDlgItems(*this, kLangIDs, Z7_ARRAY_SIZE(kLangIDs));
    LangSetDlgItems_RemoveColon(*this, kLangIDs_RemoveColon, Z7_ARRAY_SIZE(kLangIDs_RemoveColon));
    LangSetDlgItemText(*this, IDT_BENCH_CURRENT2, IDT_BENCH_CURRENT);
    LangSetDlgItemText(*this, IDT_BENCH_RESULTING2, IDT_BENCH_RESULTING);
#endif

    InitSyncNew();

    if (TotalMode) {
        _consoleEdit.Attach(GetItem(IDE_BENCH2_EDIT));
        // The Qt presentation adapter assigns the platform fixed-width font.
    }

    UInt32 numCPUs = 1;     // process threads
    UInt32 numCPUs_Sys = 1; // system threads

    {
        NSystem::CProcessAffinity threadsInfo;
        threadsInfo.InitST();
#ifndef Z7_ST
#ifdef _WIN32
        threadsInfo.Get_and_return_NumProcessThreads_and_SysThreads(numCPUs, numCPUs_Sys);
#else
        // Copy System.h's original normalization around the POSIX query adapter.
        UInt32 num1 = 0, num2 = 0;
        if (Services->GetProcessAffinity(threadsInfo)) {
            num1 = threadsInfo.GetNumProcessThreads();
            num2 = threadsInfo.GetNumSystemThreads();
        }
        if (num1 == 0)
            num1 = NSystem::GetNumberOfProcessors();
        if (num1 == 0)
            num1 = 1;
        if (num2 < num1)
            num2 = num1;
        numCPUs = num1;
        numCPUs_Sys = num2;
#endif
#endif

        AString s("/ ");
        s.Add_UInt32(numCPUs);
        s += GetProcessThreadsInfo(threadsInfo);
        SetItemTextA(IDT_BENCH_HARDWARE_THREADS, s);

        {
            AString s2;
            GetSysInfo(s, s2);
            SetItemTextA(IDT_BENCH_SYS1, s);
            if (s != s2 && !s2.IsEmpty())
                SetItemTextA(IDT_BENCH_SYS2, s2);

            GetCpuName_MultiLine(s, s2); // s2==registers
            SetItemTextA(IDT_BENCH_CPU, s);
        }
        {
            GetOsInfoText(s);
            s += " : ";
            AddCpuFeatures(s);
            SetItemTextA(IDT_BENCH_CPU_FEATURE, s);
        }

        s = "7-Zip " MY_VERSION_CPU;
        SetItemTextA(IDT_BENCH_VER, s);
    }

    // ----- Num Threads ----------

    UInt32 numThreads = Sync.NumThreads;
    if (numThreads == (UInt32)(Int32)-1)
        numThreads = numCPUs;
    numThreads &= ~(UInt32)1;
    if (numThreads == 0)
        numThreads = 1;
    numThreads = MyMin(numThreads, (UInt32)(1u << 14));

    m_NumThreads.Attach(GetItem(IDC_BENCH_NUM_THREADS));
    if (numCPUs_Sys == 0)
        numCPUs_Sys = 1;
    const UInt32 numTheads_Combo = numCPUs_Sys * 2;
    UInt32 v = 1;
    int cur = 0;
    for (; v <= numTheads_Combo;) {
        int index = ComboBox_Add_UInt32(m_NumThreads, v);
        const UInt32 vNext = v + (v < 2 ? 1 : 2);
        if (v <= numThreads)
            if (numThreads < vNext || vNext > numTheads_Combo) {
                if (v != numThreads)
                    index = ComboBox_Add_UInt32(m_NumThreads, numThreads);
                cur = index;
            }
        v = vNext;
    }
    m_NumThreads.SetCurSel(cur);
    Sync.NumThreads = GetNumberOfThreads();

    // ----- Dictionary ----------

    m_Dictionary.Attach(GetItem(IDC_BENCH_DICTIONARY));

    RamSize = (UInt64)(sizeof(size_t)) << 29;
    RamSize_Defined = Services->GetRamSize(RamSize);

#ifdef UNDER_CE
    const UInt32 kNormalizedCeSize = (16 << 20);
    if (RamSize > kNormalizedCeSize && RamSize < (33 << 20))
        RamSize = kNormalizedCeSize;
#endif
    RamSize_Limit = RamSize / 16 * 15;

    if (Sync.DictSize == (UInt64)(Int64)-1) {
        unsigned dicSizeLog = 25;
#ifdef UNDER_CE
        dicSizeLog = 20;
#endif
        if (RamSize_Defined)
            for (; dicSizeLog > kBenchMinDicLogSize; dicSizeLog--)
                if (IsMemoryUsageOK(GetBenchMemoryUsage(Sync.NumThreads, Sync.Level,
                                                        (UInt64)1 << dicSizeLog, TotalMode)))
                    break;
        Sync.DictSize = (UInt64)1 << dicSizeLog;
    }

    if (Sync.DictSize < kMinDicSize)
        Sync.DictSize = kMinDicSize;
    if (Sync.DictSize > kMaxDicSize)
        Sync.DictSize = kMaxDicSize;

    cur = 0;
    for (unsigned i = (kMinDicLogSize - 1) * 2; i <= (32 - 1) * 2; i++) {
        const size_t dict = (size_t)(2 + (i & 1)) << (i / 2);
        // if (i == (32 - 1) * 2) dict = kMaxDicSize;
        wchar_t s[32];
        const wchar_t *post;
        UInt32 d;
        if (dict >= ((UInt32)1 << 31)) {
            d = (UInt32)(dict >> 30);
            post = kGB;
        } else if (dict >= ((UInt32)1 << 21)) {
            d = (UInt32)(dict >> 20);
            post = kMB;
        } else {
            d = (UInt32)(dict >> 10);
            post = kKB;
        }
        ConvertUInt32ToString(d, s);
        std::wcscat(s, post);
        const int index = (int)m_Dictionary.AddString(s);
        m_Dictionary.SetItemData(index, (qintptr)dict);
        if (dict <= Sync.DictSize)
            cur = index;
        if (dict >= kMaxDicSize)
            break;
    }
    m_Dictionary.SetCurSel(cur);

    // ----- Num Passes ----------

    m_NumPasses.Attach(GetItem(IDC_BENCH_NUM_PASSES));
    cur = 0;
    v = 1;
    for (;;) {
        int index = ComboBox_Add_UInt32(m_NumPasses, v);
        const bool isLast = (v >= 10000000);
        UInt32 vNext = v * 10;
        if (v < 2)
            vNext = 2;
        else if (v < 5)
            vNext = 5;
        else if (v < 10)
            vNext = 10;

        if (v <= Sync.NumPasses_Limit)
            if (isLast || Sync.NumPasses_Limit < vNext) {
                if (v != Sync.NumPasses_Limit)
                    index = ComboBox_Add_UInt32(m_NumPasses, Sync.NumPasses_Limit);
                cur = index;
            }
        v = vNext;
        if (isLast)
            break;
    }
    m_NumPasses.SetCurSel(cur);

    if (TotalMode)
        NormalizeSize(true);
    else
        NormalizePosition();

    RestartBenchmark();
}

UInt32 CBenchmarkDialog::GetNumberOfThreads() {
    return (UInt32)m_NumThreads.GetItemData_of_CurSel();
}

#define UINT_TO_STR_3(s, val)                                                                      \
    {                                                                                              \
        s[0] = (wchar_t)('0' + (val) / 100);                                                       \
        s[1] = (wchar_t)('0' + (val) % 100 / 10);                                                  \
        s[2] = (wchar_t)('0' + (val) % 10);                                                        \
        s += 3;                                                                                    \
        s[0] = 0;                                                                                  \
    }

static WCHAR *NumberToDot3(UInt64 val, WCHAR *s Z7_lifetimebound) {
    s = ConvertUInt64ToString(val / 1000, s);
    const UInt32 rem = (UInt32)(val % 1000);
    *s++ = '.';
    UINT_TO_STR_3(s, rem)
    return s;
}

void CBenchmarkDialog::SetItemText_Number(unsigned itemID, UInt64 val, const wchar_t *post) {
    wchar_t s[64];
    ConvertUInt64ToString(val, s);
    if (post)
        std::wcscat(s, post);
    SetItemText(itemID, s);
}

static void AddSize_MB(UString &s, UInt64 size) {
    s.Add_UInt64((size + (1 << 20) - 1) >> 20);
    s += kMB;
}

void CBenchmarkDialog::Print_MemUsage(UString &s, UInt64 memUsage) const {
    AddSize_MB(s, memUsage);
    if (RamSize_Defined) {
        s += " / ";
        AddSize_MB(s, RamSize);
    }
}

size_t CBenchmarkDialog::OnChangeDictionary() {
    const size_t dict = (size_t)m_Dictionary.GetItemData_of_CurSel();
    const UInt64 memUsage = GetBenchMemoryUsage(GetNumberOfThreads(), Sync.Level, dict,
                                                false); // totalBench mode

    UString s;
    Print_MemUsage(s, memUsage);

#ifdef Z7_LARGE_PAGES
    {
        AString s2;
        Add_LargePages_String(s2);
        if (!s2.IsEmpty()) {
            s.Add_Space();
            s += s2;
        }
    }
#endif

    SetItemText(IDT_BENCH_MEMORY_VAL, s);

    return dict;
}

static const UInt32 g_IDs[] = {
    IDT_BENCH_COMPRESS_SIZE1,   IDT_BENCH_COMPRESS_SIZE2,   IDT_BENCH_COMPRESS_USAGE1,
    IDT_BENCH_COMPRESS_USAGE2,  IDT_BENCH_COMPRESS_SPEED1,  IDT_BENCH_COMPRESS_SPEED2,
    IDT_BENCH_COMPRESS_RATING1, IDT_BENCH_COMPRESS_RATING2, IDT_BENCH_COMPRESS_RPU1,
    IDT_BENCH_COMPRESS_RPU2,

    IDT_BENCH_DECOMPR_SIZE1,    IDT_BENCH_DECOMPR_SIZE2,    IDT_BENCH_DECOMPR_SPEED1,
    IDT_BENCH_DECOMPR_SPEED2,   IDT_BENCH_DECOMPR_RATING1,  IDT_BENCH_DECOMPR_RATING2,
    IDT_BENCH_DECOMPR_USAGE1,   IDT_BENCH_DECOMPR_USAGE2,   IDT_BENCH_DECOMPR_RPU1,
    IDT_BENCH_DECOMPR_RPU2,

    IDT_BENCH_TOTAL_USAGE_VAL,  IDT_BENCH_TOTAL_RATING_VAL, IDT_BENCH_TOTAL_RPU_VAL};

static const unsigned k_Ids_Enc_1[] = {IDT_BENCH_COMPRESS_USAGE1, IDT_BENCH_COMPRESS_SPEED1,
                                       IDT_BENCH_COMPRESS_RPU1, IDT_BENCH_COMPRESS_RATING1,
                                       IDT_BENCH_COMPRESS_SIZE1};

static const unsigned k_Ids_Enc[] = {IDT_BENCH_COMPRESS_USAGE2, IDT_BENCH_COMPRESS_SPEED2,
                                     IDT_BENCH_COMPRESS_RPU2, IDT_BENCH_COMPRESS_RATING2,
                                     IDT_BENCH_COMPRESS_SIZE2};

static const unsigned k_Ids_Dec_1[] = {IDT_BENCH_DECOMPR_USAGE1, IDT_BENCH_DECOMPR_SPEED1,
                                       IDT_BENCH_DECOMPR_RPU1, IDT_BENCH_DECOMPR_RATING1,
                                       IDT_BENCH_DECOMPR_SIZE1};

static const unsigned k_Ids_Dec[] = {IDT_BENCH_DECOMPR_USAGE2, IDT_BENCH_DECOMPR_SPEED2,
                                     IDT_BENCH_DECOMPR_RPU2, IDT_BENCH_DECOMPR_RATING2,
                                     IDT_BENCH_DECOMPR_SIZE2};

static const unsigned k_Ids_Tot[] = {IDT_BENCH_TOTAL_USAGE_VAL, 0, IDT_BENCH_TOTAL_RPU_VAL,
                                     IDT_BENCH_TOTAL_RATING_VAL, 0};

void CBenchmarkDialog::MyKillTimer() {
    if (_timer != 0) {
        KillTimer(kTimerID);
        _timer = 0;
    }
}

void CBenchmarkDialog::OnDestroy() {
    /* actually timer was removed before.
       also the timer must be removed by Windows, when window  will be removed. */
    MyKillTimer(); // it's optional code
}

void SetErrorMessage_MemUsage(UString &s, UInt64 reqSize, UInt64 ramSize, UInt64 ramLimit,
                              const UString &usageString);

void CBenchmarkDialog::StartBenchmark() {
    StartResult = S_OK;
    NeedRestart = false;
    WasStopped_in_GUI = false;

    SetItemText_Empty(IDT_BENCH_ERROR_MESSAGE);

    MyKillTimer(); // optional code. timer was killed before

    const size_t dict = OnChangeDictionary();
    const UInt32 numThreads = GetNumberOfThreads();
    const UInt32 numPasses = (UInt32)m_NumPasses.GetItemData_of_CurSel();

    for (unsigned i = 0; i < Z7_ARRAY_SIZE(g_IDs); i++)
        SetItemText(g_IDs[i], kProcessingString);

    SetItemText_Empty(IDT_BENCH_LOG);
    SetItemText_Empty(IDT_BENCH_ELAPSED_VAL);
    SetItemText_Empty(IDT_BENCH_ERROR_MESSAGE);

    const UInt64 memUsage = GetBenchMemoryUsage(numThreads, Sync.Level, dict,
                                                false); // totalBench
    if (!IsMemoryUsageOK(memUsage)) {
        StartResult = E_OUTOFMEMORY;
        UString s2;
        LangString_OnlyFromLangFile(IDS_MEM_REQUIRED_MEM_SIZE, s2);
        if (s2.IsEmpty()) {
            s2 = LangString(IDT_BENCH_MEMORY);
            if (s2.IsEmpty())
                GetItemText(IDT_BENCH_MEMORY, s2);
            s2.RemoveChar(L':');
        }
        UString s;
        SetErrorMessage_MemUsage(s, memUsage, RamSize, RamSize_Limit, s2);
        MessageBoxError_Status(s);
        return;
    }

    EnableItem(IDB_STOP, true);

    _startTime = GetTickCount();
    _finishTime = _startTime;
    _finishTime_WasSet = false;

    {
        NWindows::NSynchronization::CCriticalSectionLock lock(Sync.CS);
        InitSyncNew();
        Sync.DictSize = dict;
        Sync.NumThreads = numThreads;
        Sync.NumPasses_Limit = numPasses;
    }

    PrintTime();

    _timer = SetTimer(kTimerID, kTimerElapse);
    const WRes threadResult = _thread.Create(CThreadBenchmark::MyThreadFunction, &_threadBenchmark);
    if (threadResult == 0)
        QtTaskPresentation::BeginWork(*this);
    if (threadResult != 0) {
        StartResult = HRESULT_FROM_WIN32(threadResult);
        MyKillTimer();
        MessageBoxError_Status(
            QtDiagnosticText(QT_TRANSLATE_NOOP("SevenZipDiagnostics", "Can't create thread")));
    }
    return;
}

void CBenchmarkDialog::RestartBenchmark() {
    if (ExitWasAsked_in_GUI)
        return;

    if (_thread.IsCreated()) {
        NeedRestart = true;
        SendExit_Status(
            QtDiagnosticText(QT_TRANSLATE_NOOP("SevenZipDiagnostics", "Stop for restart ...")));
    } else
        StartBenchmark();
}

void CBenchmarkDialog::Disable_Stop_Button() {
    // if we disable focused button, then focus will be lost
    if (GetFocus() == GetItem(IDB_STOP)) {
        // SendMsg_NextDlgCtl_Prev();
        SendMsg_NextDlgCtl_CtlId(IDB_RESTART);
    }
    EnableItem(IDB_STOP, false);
}

void CBenchmarkDialog::OnStopButton() {
    if (ExitWasAsked_in_GUI)
        return;

    Disable_Stop_Button();

    WasStopped_in_GUI = true;
    if (_thread.IsCreated()) {
        SendExit_Status(QtDiagnosticText(QT_TRANSLATE_NOOP("SevenZipDiagnostics", "Stop ...")));
    }
}

void CBenchmarkDialog::OnCancel() {
    ExitWasAsked_in_GUI = true;

    /*
    SendMsg_NextDlgCtl_Prev();
    EnableItem(IDCANCEL, false);
    */

    if (_thread.IsCreated())
        SendExit_Status(QtDiagnosticText(QT_TRANSLATE_NOOP("SevenZipDiagnostics", "Cancel ...")));
    else
        CQtBenchmarkWindow::OnCancel();
}

void CBenchmarkDialog::OnHelp() { ShowHelpWindow(kHelpTopic); }

// void GetTimeString(UInt64 timeValue, wchar_t *s);

void CBenchmarkDialog::PrintTime() {
    const UInt32 curTime = _finishTime_WasSet ? _finishTime : GetTickCount();

    const UInt32 elapsedTime = (curTime - _startTime);

    WCHAR s[64];

    WCHAR *p = ConvertUInt32ToString(elapsedTime / 1000, s);

    if (_finishTime_WasSet) {
        *p++ = '.';
        UINT_TO_STR_3(p, elapsedTime % 1000)
    }

    // p = NumberToDot3((UInt64)elapsedTime, s);

    MyStringCopy(p, L" s");

    // if (WasStopped_in_GUI) wcscat(s, L" X"); // for debug

    if (s == ElapsedSec_Prev)
        return;

    ElapsedSec_Prev = s;

    // static cnt = 0; cnt++; wcscat(s, L" ");
    // UString s2; s2.Add_UInt32(cnt); wcscat(s, s2.Ptr());

    SetItemText(IDT_BENCH_ELAPSED_VAL, s);
}

static UInt64 GetMips(UInt64 ips) { return (ips + 500000) / 1000000; }

static UInt64 GetUsagePercents(UInt64 usage) { return Benchmark_GetUsage_Percents(usage); }

static UInt32 GetRating(const CTotalBenchRes &info) {
    UInt64 numIter = info.NumIterations2;
    if (numIter == 0)
        numIter = 1000000;
    const UInt64 rating64 = GetMips(info.Rating / numIter);
    // return rating64;
    UInt32 rating32 = (UInt32)rating64;
    if (rating32 != rating64)
        rating32 = (UInt32)(Int32)-1;
    return rating32;
}

static void AddUsageString(UString &s, const CTotalBenchRes &info) {
    UInt64 numIter = info.NumIterations2;
    if (numIter == 0)
        numIter = 1000000;
    UInt64 usage = GetUsagePercents(info.Usage / numIter);

    wchar_t w[32];
    wchar_t *p = ConvertUInt64ToString(usage, w);
    p[0] = '%';
    p[1] = 0;
    unsigned len = (unsigned)(size_t)(p - w);
    while (len < 5) {
        s.Add_Space();
        len++;
    }
    s += w;
}

static void Add_Dot3String(UString &s, UInt64 val) {
    WCHAR temp[32];
    NumberToDot3(val, temp);
    s += temp;
}

static void AddRatingString(UString &s, const CTotalBenchRes &info) {
    // AddUsageString(s, info);
    // s.Add_Space();
    // s.Add_UInt32(GetRating(info));
    Add_Dot3String(s, GetRating(info));
}

static void AddRatingsLine(UString &s, const CTotalBenchRes &enc, const CTotalBenchRes &dec
#ifdef PRINT_ITER_TIME
                           ,
                           DWORD ticks
#endif
) {
    // AddUsageString(s, enc); s.Add_Space();

    AddRatingString(s, enc);
    s += "  ";
    AddRatingString(s, dec);

    CTotalBenchRes tot_BenchRes;
    tot_BenchRes.SetSum(enc, dec);

    s += "  ";
    AddRatingString(s, tot_BenchRes);

    s.Add_Space();
    AddUsageString(s, tot_BenchRes);

#ifdef PRINT_ITER_TIME
    s.Add_Space();
    {
    Add_Dot3String(s, ticks;
    s += " s";
    // s.Add_UInt32(ticks); s += " ms";
    }
#endif
}

void CBenchmarkDialog::PrintRating(UInt64 rating, UINT controlID) {
    // SetItemText_Number(controlID, GetMips(rating), kMIPS);
    WCHAR s[64];
    MyStringCopy(NumberToDot3(GetMips(rating), s), L" GIPS");
    SetItemText(controlID, s);
}

void CBenchmarkDialog::PrintUsage(UInt64 usage, UINT controlID) {
    SetItemText_Number(controlID, GetUsagePercents(usage), L"%");
}

// void SetItemText_Number

void CBenchmarkDialog::PrintBenchRes(const CTotalBenchRes2 &info, const UINT ids[]) {
    if (info.NumIterations2 == 0)
        return;
    if (ids[1] != 0)
        SetItemText_Number(ids[1], (info.Speed >> 10) / info.NumIterations2, kKBs);
    PrintRating(info.Rating / info.NumIterations2, ids[3]);
    PrintRating(info.RPU / info.NumIterations2, ids[2]);
    PrintUsage(info.Usage / info.NumIterations2, ids[0]);
    if (ids[4] != 0) {
        UInt64 val = info.UnpackSize;
        const wchar_t *kPostfix;
        if (val >= ((UInt64)1 << 40)) {
            kPostfix = kGB;
            val >>= 30;
        } else {
            kPostfix = kMB;
            val >>= 20;
        }
        SetItemText_Number(ids[4], val, kPostfix);
    }
}

// static UInt32 k_Message_Finished_cnt = 0;
// static UInt32 k_OnTimer_cnt = 0;

void CBenchmarkDialog::OnMessage(UINT message, quintptr wParam) {
    if (message != k_Message_Finished)
        return;

    {
        if (wParam == k_Msg_WPARM_Thread_Finished) {
            QtTaskPresentation::SetPhase(*this, QtOperationPhase::Reviewing);
            _finishTime = GetTickCount();
            _finishTime_WasSet = true;
            MyKillTimer();

            const WRes waitResult = _thread.Wait_Close();
            if (waitResult != 0) {
                StartResult = HRESULT_FROM_WIN32(waitResult);
                MessageBoxError_Status(QtDiagnosticText(
                    QT_TRANSLATE_NOOP("SevenZipDiagnostics", "Thread Wait Error")));
            }

            if (!WasStopped_in_GUI) {
                WasStopped_in_GUI = true;
                Disable_Stop_Button();
            }

            HRESULT res = Sync.BenchFinish_Thread_HRESULT;
            if (res != S_OK)
                // if (!ExitWasAsked_in_GUI || res != E_ABORT)
                MessageBoxError_Status(HResultToMessage(res));

            if (ExitWasAsked_in_GUI) {
                // SetItemText(IDT_BENCH_ERROR_MESSAGE, "before CModalDialog::OnCancel()");
                // Sleep (2000);
                // MessageBoxError(L"test");
                CQtBenchmarkWindow::OnCancel();
                return;
            }

            SetItemText_Empty(IDT_BENCH_ERROR_MESSAGE);

            res = Sync.BenchFinish_Task_HRESULT;
            if (res != S_OK) {
                if (!WasStopped_in_GUI || res != E_ABORT) {
                    UString m;
                    if (res == S_FALSE)
                        m = QtDiagnosticText(
                            QT_TRANSLATE_NOOP("SevenZipDiagnostics", "Decoding error"));
                    else if (res == CLASS_E_CLASSNOTAVAILABLE)
                        m = QtDiagnosticText(
                            QT_TRANSLATE_NOOP("SevenZipDiagnostics", "Can't find 7z.dll"));
                    else
                        m = HResultToMessage(res);
                    MessageBoxError_Status(m);
                }
            }

            if (NeedRestart) {
                StartBenchmark();
                return;
            }
        }
        // k_Message_Finished_cnt++;
        UpdateGui();
    }
}

void CBenchmarkDialog::OnTimer(quintptr timerID) {
    // k_OnTimer_cnt++;
    if (timerID == kTimerID)
        UpdateGui();
}

void CBenchmarkDialog::UpdateGui() {
    PrintTime();

    if (TotalMode) {
        bool wasChanged = false;
        {
            NWindows::NSynchronization::CCriticalSectionLock lock(Sync.CS);

            if (Sync.TextWasChanged) {
                wasChanged = true;
                Bench2Text += Sync.Text;
                Sync.Text.Empty();
                Sync.TextWasChanged = false;
            }
        }
        if (wasChanged)
            _consoleEdit.SetText(Bench2Text);
        return;
    }

    CSyncData sd;
    CRecordVector<CBenchPassResult> RatingVector;

    {
        NWindows::NSynchronization::CCriticalSectionLock lock(Sync.CS);
        sd = Sync.sd;

        if (sd.NeedPrint_RatingVector)
            RatingVector = Sync.RatingVector;

        if (sd.NeedPrint_Freq) {
            Sync.FreqString_GUI = Sync.FreqString_Sync;
            sd.NeedPrint_RatingVector = true;
        }

        Sync.sd.NeedPrint_RatingVector = false;
        Sync.sd.NeedPrint_Enc_1 = false;
        Sync.sd.NeedPrint_Enc = false;
        Sync.sd.NeedPrint_Dec_1 = false;
        Sync.sd.NeedPrint_Dec = false;
        Sync.sd.NeedPrint_Tot = false;
        Sync.sd.NeedPrint_Freq = false;
    }

    if (sd.NumPasses_Finished != NumPasses_Finished_Prev) {
        SetItemText_Number(IDT_BENCH_PASSES_VAL, sd.NumPasses_Finished, L" /");
        NumPasses_Finished_Prev = sd.NumPasses_Finished;
    }

    if (sd.NeedPrint_Enc_1)
        PrintBenchRes(sd.Enc_BenchRes_1, k_Ids_Enc_1);
    if (sd.NeedPrint_Enc)
        PrintBenchRes(sd.Enc_BenchRes, k_Ids_Enc);
    if (sd.NeedPrint_Dec_1)
        PrintBenchRes(sd.Dec_BenchRes_1, k_Ids_Dec_1);
    if (sd.NeedPrint_Dec)
        PrintBenchRes(sd.Dec_BenchRes, k_Ids_Dec);

    if (sd.BenchWasFinished && sd.NeedPrint_Tot) {
        CTotalBenchRes2 tot_BenchRes = sd.Enc_BenchRes;
        tot_BenchRes.Update_With_Res2(sd.Dec_BenchRes);
        PrintBenchRes(tot_BenchRes, k_Ids_Tot);
    }

    if (sd.NeedPrint_RatingVector)
    // for (unsigned k = 0; k < 1; k++)
    {
        UString s;
        s += Sync.FreqString_GUI;
        if (!RatingVector.IsEmpty()) {
            if (!s.IsEmpty())
                s.Add_LF();
            s += UString(QCoreApplication::translate("BenchmarkReport", "Compr Decompr Total   CPU")
                             .toStdWString()
                             .c_str());
#ifdef PRINT_ITER_TIME
            s += UString(
                QCoreApplication::translate("BenchmarkReport", "   Time").toStdWString().c_str());
#endif
            s.Add_LF();
        }
        // s += "GIPS    GIPS   GIPS    %   s"; s.Add_LF();
        for (unsigned i = 0; i < RatingVector.Size(); i++) {
            if (i != 0)
                s.Add_LF();
            if ((int)i == sd.RatingVector_DeletedIndex) {
                s += "...";
                s.Add_LF();
            }
            const CBenchPassResult &pair = RatingVector[i];
            /*
              s += "g:"; s.Add_UInt32((UInt32)pair.EncInfo.GlobalTime);
              s += " u:"; s.Add_UInt32((UInt32)pair.EncInfo.UserTime);
              s.Add_Space();
            */
            AddRatingsLine(s, pair.Enc, pair.Dec
#ifdef PRINT_ITER_TIME
                           ,
                           pair.Ticks
#endif
            );
            /*
            {
              UInt64 v = i + 1;
              if (sd.RatingVector_DeletedIndex >= 0 && i >= (unsigned)sd.RatingVector_DeletedIndex)
                v += sd.RatingVector_NumDeleted;
              char temp[64];
              ConvertUInt64ToString(v, temp);
              s += " : ";
              s += temp;
            }
            */
        }

        if (sd.BenchWasFinished) {
            s.Add_LF();
            s += "-------------";
            s.Add_LF();
            {
                // average time is not correct because of freq detection in first iteration
                AddRatingsLine(
                    s, sd.Enc_BenchRes, sd.Dec_BenchRes
#ifdef PRINT_ITER_TIME
                    ,
                    (DWORD)(sd.TotalTicks / (sd.NumPasses_Finished ? sd.NumPasses_Finished : 1))
#endif
                );
            }
        }
        // s.Add_LF(); s += "OnTimer: "; s.Add_UInt32(k_OnTimer_cnt);
        // s.Add_LF(); s += "finished Message: "; s.Add_UInt32(k_Message_Finished_cnt);
        // static cnt = 0; cnt++; s.Add_LF(); s += "Print: "; s.Add_UInt32(cnt);
        // s.Add_LF(); s += "NumEncProgress: "; s.Add_UInt32((UInt32)sd.NumEncProgress);
        // s.Add_LF(); s += "NumDecProgress: "; s.Add_UInt32((UInt32)sd.NumDecProgress);
        SetItemText(IDT_BENCH_LOG, s);
    }
}

void CBenchmarkDialog::OnCommand(unsigned code, unsigned itemID) {
    if (code == 1 && (itemID == IDC_BENCH_DICTIONARY || itemID == IDC_BENCH_NUM_PASSES ||
                      itemID == IDC_BENCH_NUM_THREADS)) {
        RestartBenchmark();
    }
}

void CBenchmarkDialog::OnButtonClicked(unsigned buttonID) {
    switch (buttonID) {
    case IDB_RESTART:
        RestartBenchmark();
        return;
    case IDB_STOP:
        OnStopButton();
        return;
    }
}

// ---------- Benchmark Thread ----------

struct CBenchCallback Z7_final : public IBenchCallback {
    UInt64 dictionarySize;
    CBenchProgressSync *Sync;
    CBenchmarkDialog *BenchmarkDialog;

    HRESULT SetEncodeResult(const CBenchInfo &info, bool final) Z7_override;
    HRESULT SetDecodeResult(const CBenchInfo &info, bool final) Z7_override;
};

HRESULT CBenchCallback::SetEncodeResult(const CBenchInfo &info, bool final) {
    bool needPost = false;
    {
        NSynchronization::CCriticalSectionLock lock(Sync->CS);
        if (Sync->Exit)
            return E_ABORT;
        CSyncData &sd = Sync->sd;
        // sd.NumEncProgress++;
        CTotalBenchRes2 &br = sd.Enc_BenchRes_1;
        {
            UInt64 dictSize = Sync->DictSize;
            if (final) {
                // sd.EncInfo = info;
            } else {
                /* if (!final), then CBenchInfo::NumIterations means totalNumber of threads.
                   so we can reduce the dictionary */
                if (dictSize > info.UnpackSize)
                    dictSize = info.UnpackSize;
            }
            br.Rating = info.GetRating_LzmaEnc(dictSize);
        }
        br.SetFrom_BenchInfo(info);
        sd.NeedPrint_Enc_1 = true;
        if (final) {
            sd.Enc_BenchRes.Update_With_Res2(br);
            sd.NeedPrint_Enc = true;
            needPost = true;
        }
    }

    if (needPost)
        BenchmarkDialog->PostMsg(k_Message_Finished, k_Msg_WPARM_Enc1_Finished);

    return S_OK;
}

HRESULT CBenchCallback::SetDecodeResult(const CBenchInfo &info, bool final) {
    NSynchronization::CCriticalSectionLock lock(Sync->CS);
    if (Sync->Exit)
        return E_ABORT;
    CSyncData &sd = Sync->sd;
    // sd.NumDecProgress++;
    CTotalBenchRes2 &br = sd.Dec_BenchRes_1;
    br.Rating = info.GetRating_LzmaDec();
    br.SetFrom_BenchInfo(info);
    sd.NeedPrint_Dec_1 = true;
    if (final)
        sd.Dec_BenchRes.Update_With_Res2(br);
    return S_OK;
}

struct CBenchCallback2 Z7_final : public IBenchPrintCallback {
    CBenchProgressSync *Sync;
    bool TotalMode;

    void Print(const char *s) Z7_override;
    void NewLine() Z7_override;
    HRESULT CheckBreak() Z7_override;
};

void CBenchCallback2::Print(const char *s) {
    if (TotalMode) {
        NSynchronization::CCriticalSectionLock lock(Sync->CS);
        Sync->Text += s;
        Sync->TextWasChanged = true;
    }
}

void CBenchCallback2::NewLine() { Print("\xD\n"); }

HRESULT CBenchCallback2::CheckBreak() {
    NSynchronization::CCriticalSectionLock lock(Sync->CS);
    if (Sync->Exit)
        return E_ABORT;
    return S_OK;
}

struct CFreqCallback Z7_final : public IBenchFreqCallback {
    CBenchmarkDialog *BenchmarkDialog;

    virtual HRESULT AddCpuFreq(unsigned numThreads, UInt64 freq, UInt64 usage) Z7_override;
    virtual HRESULT FreqsFinished(unsigned numThreads) Z7_override;
};

HRESULT CFreqCallback::AddCpuFreq(unsigned numThreads, UInt64 freq, UInt64 usage) {
    HRESULT res;
    {
        CBenchProgressSync &sync = BenchmarkDialog->Sync;
        NSynchronization::CCriticalSectionLock lock(sync.CS);
        UString &s = sync.FreqString_Sync;
        if (sync.NumFreqThreadsPrev != numThreads) {
            sync.NumFreqThreadsPrev = numThreads;
            if (!s.IsEmpty())
                s.Add_LF();
            s += UString(QCoreApplication::translate("BenchmarkReport", "%1T Frequency (MHz):")
                             .arg(numThreads)
                             .toStdWString()
                             .c_str());
            s.Add_LF();
        }
        s.Add_Space();
        if (numThreads != 1) {
            s.Add_UInt64(GetUsagePercents(usage));
            s.Add_Char('%');
            s.Add_Space();
        }
        s.Add_UInt64(GetMips(freq));
        // BenchmarkDialog->Sync.sd.NeedPrint_Freq = true;
        res = sync.Exit ? E_ABORT : S_OK;
    }
    // BenchmarkDialog->PostMsg(k_Message_Finished, k_Msg_WPARM_Enc1_Finished);
    return res;
}

HRESULT CFreqCallback::FreqsFinished(unsigned /* numThreads */) {
    HRESULT res;
    {
        CBenchProgressSync &sync = BenchmarkDialog->Sync;
        NSynchronization::CCriticalSectionLock lock(sync.CS);
        sync.sd.NeedPrint_Freq = true;
        BenchmarkDialog->PostMsg(k_Message_Finished, k_Msg_WPARM_Enc1_Finished);
        res = sync.Exit ? E_ABORT : S_OK;
    }
    BenchmarkDialog->PostMsg(k_Message_Finished, k_Msg_WPARM_Enc1_Finished);
    return res;
}

// define USE_DUMMY only for debug
// #define USE_DUMMY
#ifdef USE_DUMMY
static unsigned dummy = 1;
static unsigned Dummy(unsigned limit) {
    unsigned sum = 0;
    for (unsigned k = 0; k < limit; k++) {
        sum += dummy;
        if (sum == 0)
            break;
    }
    return sum;
}
#endif

HRESULT CThreadBenchmark::Process() {
/* the first benchmark pass can be slow,
   if we run benchmark while the window is being created,
   and (no freq detecion loop) && (dictionary is small) (-mtic is small) */

// Sleep(300); // for debug
#ifdef USE_DUMMY
    Dummy(1000 * 1000 * 1000); // for debug
#endif

    CBenchProgressSync &sync = BenchmarkDialog->Sync;
    HRESULT finishHRESULT = S_OK;

    try {
        for (UInt32 passIndex = 0;; passIndex++) {
            // throw 1; // to debug
            // throw CSystemException(E_INVALIDARG); // to debug

            UInt64 dictionarySize;
            UInt32 numThreads;
            {
                NSynchronization::CCriticalSectionLock lock(sync.CS);
                if (sync.Exit)
                    break;
                dictionarySize = sync.DictSize;
                numThreads = sync.NumThreads;
            }

#ifdef PRINT_ITER_TIME
            const DWORD startTick = GetTickCount();
#endif

            CBenchCallback callback;

            callback.dictionarySize = dictionarySize;
            callback.Sync = &sync;
            callback.BenchmarkDialog = BenchmarkDialog;

            CBenchCallback2 callback2;
            callback2.TotalMode = BenchmarkDialog->TotalMode;
            callback2.Sync = &sync;

            CFreqCallback freqCallback;
            freqCallback.BenchmarkDialog = BenchmarkDialog;

            HRESULT result;

            try {
                CObjectVector<CProperty> props;

                props = BenchmarkDialog->Props;

                if (BenchmarkDialog->TotalMode) {
                    props = BenchmarkDialog->Props;
                } else {
                    {
                        CProperty prop;
                        prop.Name = "mt";
                        prop.Value.Add_UInt32(numThreads);
                        props.Add(prop);
                    }
                    {
                        CProperty prop;
                        prop.Name = 'd';
                        prop.Name.Add_UInt32((UInt32)(dictionarySize >> 10));
                        prop.Name.Add_Char('k');
                        props.Add(prop);
                    }
                }

                result = BenchmarkDialog->Services->Run(
                    EXTERNAL_CODECS_LOC_VARS BenchmarkDialog->TotalMode ? &callback2 : NULL,
                    BenchmarkDialog->TotalMode ? NULL : &callback, props, 1, false,
                    (!BenchmarkDialog->TotalMode) && passIndex == 0 ? &freqCallback : NULL);

                // result = S_FALSE; // for debug;
                // throw 1;
            } catch (...) {
                result = E_FAIL;
            }

#ifdef PRINT_ITER_TIME
            const DWORD numTicks = GetTickCount() - startTick;
#endif

            bool finished = true;

            NSynchronization::CCriticalSectionLock lock(sync.CS);

            if (result != S_OK) {
                sync.BenchFinish_Task_HRESULT = result;
                break;
            }

            {
                CSyncData &sd = sync.sd;

                sd.NumPasses_Finished++;
#ifdef PRINT_ITER_TIME
                sd.TotalTicks += numTicks;
#endif

                if (BenchmarkDialog->TotalMode)
                    break;

                {
                    CTotalBenchRes tot_BenchRes = sd.Enc_BenchRes_1;
                    tot_BenchRes.Update_With_Res(sd.Dec_BenchRes_1);

                    sd.NeedPrint_RatingVector = true;
                    {
                        CBenchPassResult pair;
                        // pair.EncInfo = sd.EncInfo; // for debug
                        pair.Enc = sd.Enc_BenchRes_1;
                        pair.Dec = sd.Dec_BenchRes_1;
#ifdef PRINT_ITER_TIME
                        pair.Ticks = numTicks;
#endif
                        sync.RatingVector.Add(pair);
                        // pair.Dec_Defined = true;
                    }
                }

                sd.NeedPrint_Dec = true;
                sd.NeedPrint_Tot = true;

                if (sync.RatingVector.Size() > kRatingVector_NumBundlesMax) {
                    // sd.RatingVector_NumDeleted++;
                    sd.RatingVector_DeletedIndex = (int)(kRatingVector_NumBundlesMax / 4);
                    sync.RatingVector.Delete((unsigned)(sd.RatingVector_DeletedIndex));
                }

                if (sync.sd.NumPasses_Finished < sync.NumPasses_Limit)
                    finished = false;
                else {
                    sync.sd.BenchWasFinished = true;
                    // BenchmarkDialog->_finishTime = GetTickCount();
                    // return 0;
                }
            }

            if (BenchmarkDialog->TotalMode)
                break;

            /*
            if (newTick - prevTick < 1000)
              numSameTick++;
            if (numSameTick > 5 || finished)
            {
              prevTick = newTick;
              numSameTick = 0;
            */
            // for (unsigned i = 0; i < 1; i++)
            {
                // we suppose that PostMsg messages will be processed in order.
                if (!BenchmarkDialog->PostMsg_Finish(k_Msg_WPARM_Iter_Finished)) {
                    finished = true;
                    finishHRESULT = E_FAIL;
                    // throw 1234567;
                }
            }
            if (finished)
                break;
        }
        // return S_OK;
    } catch (CSystemException &e) {
        finishHRESULT = e.ErrorCode;
        // BenchmarkDialog->MessageBoxError(HResultToMessage(e.ErrorCode));
        // return E_FAIL;
    } catch (...) {
        finishHRESULT = E_FAIL;
        // BenchmarkDialog->MessageBoxError(HResultToMessage(E_FAIL));
        // return E_FAIL;
    }

    if (finishHRESULT != S_OK) {
        NSynchronization::CCriticalSectionLock lock(sync.CS);
        sync.BenchFinish_Thread_HRESULT = finishHRESULT;
    }
    if (!BenchmarkDialog->PostMsg_Finish(k_Msg_WPARM_Thread_Finished)) {
        // sync.BenchFinish_Thread_HRESULT = E_FAIL;
    }
    return 0;
}

static void ParseNumberString(const UString &s, NCOM::CPropVariant &prop) {
    const wchar_t *end;
    UInt64 result = ConvertStringToUInt64(s, &end);
    if (*end != 0 || s.IsEmpty())
        prop = s;
    else if (result <= (UInt32)0xFFFFFFFF)
        prop = (UInt32)result;
    else
        prop = result;
}

HRESULT Benchmark(DECL_EXTERNAL_CODECS_LOC_VARS const CObjectVector<CProperty> &props,
                  UInt32 numIterations, QWidget *hwndParent, IQtBenchmarkServices *services,
                  HRESULT *operationResult) {
    QtBenchmarkServices defaultServices;
    CBenchmarkDialog bd;
    bd.Services = services ? services : &defaultServices;

    bd.TotalMode = false;
    bd.Props = props;
    if (numIterations == 0)
        numIterations = 1;
    bd.Sync.NumPasses_Limit = numIterations;
    bd.Sync.DictSize = (UInt64)(Int64)-1;
    bd.Sync.NumThreads = (UInt32)(Int32)-1;
    bd.Sync.Level = -1;

    COneMethodInfo method;

    UInt32 numCPUs = 1;
#ifndef Z7_ST
    numCPUs = NSystem::GetNumberOfProcessors();
#endif
    UInt32 numThreads = numCPUs;

    FOR_VECTOR(i, props) {
        const CProperty &prop = props[i];
        UString name = prop.Name;
        name.MakeLower_Ascii();
        if (name.IsEqualTo_Ascii_NoCase("m") && prop.Value.IsEqualTo("*")) {
            bd.TotalMode = true;
            continue;
        }

        NCOM::CPropVariant propVariant;
        if (!prop.Value.IsEmpty())
            ParseNumberString(prop.Value, propVariant);
        if (name.IsPrefixedBy("mt")) {
#ifndef Z7_ST
            RINOK(ParseMtProp(name.Ptr(2), propVariant, numCPUs, numThreads))
            if (numThreads != numCPUs)
                bd.Sync.NumThreads = numThreads;
#endif
            continue;
        }
        /*
        if (name.IsEqualTo("time"))
        {
          // UInt32 testTime = 4;
          // RINOK(ParsePropToUInt32(L"", propVariant, testTime));
          continue;
        }
        RINOK(method.ParseMethodFromPROPVARIANT(name, propVariant));
        */
        // here we need to parse DictSize property, and ignore unknown properties
        method.ParseMethodFromPROPVARIANT(name, propVariant);
    }

    if (bd.TotalMode) {
        // bd.Bench2Text.Empty();
        bd.Bench2Text = "7-Zip " MY_VERSION_CPU;
        // bd.Bench2Text.Add_Char((char)0xD);
        bd.Bench2Text.Add_LF();
    }

    {
        UInt64 dict;
        if (method.Get_DicSize(dict))
            bd.Sync.DictSize = dict;
    }
    bd.Sync.Level = (int)method.GetLevel();

    // Dummy(1000 * 1000 * 1);

    {
        CThreadBenchmark &benchmarker = bd._threadBenchmark;
#ifdef Z7_EXTERNAL_CODECS
        benchmarker._externalCodecs = _externalCodecs;
#endif
        benchmarker.BenchmarkDialog = &bd;
    }

    bd.Create(hwndParent);
    if (operationResult)
        *operationResult = bd.OperationResult();

    return S_OK;
}

CBenchmarkDialog::~CBenchmarkDialog() {
    if (_thread.IsCreated()) {
        /* the following code will be not executed in normal code flow.
           it can be called, if there is some internal failure in dialog code. */
        Attach(NULL);
        MessageBoxError(QtDiagnosticText(
            QT_TRANSLATE_NOOP("SevenZipDiagnostics", "The flaw in benchmark thread code")));
        Sync.SendExit();
        _thread.Wait_Close();
    }
}
