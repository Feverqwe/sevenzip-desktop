#include "Common/StringConvert.h"
#include "Windows/Thread.h"
#include "ui/progress/progresssync.h"
#include <QtTest>
#include <atomic>
#include <functional>

class ProgressWorker {
    CProgressSync &Sync;
    NWindows::CThread Thread;

    static THREAD_FUNC_DECL run(void *context) {
        auto &worker = *static_cast<ProgressWorker *>(context);
        worker.Started.Set();
        worker.Result = worker.Call();
        worker.Returned.store(true);
        return THREAD_FUNC_RET_ZERO;
    }

  public:
    NWindows::NSynchronization::CManualResetEvent Started;
    std::function<HRESULT()> Call;
    std::atomic<bool> Returned{false};
    HRESULT Result = E_FAIL;
    explicit ProgressWorker(CProgressSync &sync) : Sync(sync) {}
    ~ProgressWorker() {
        if (Thread.IsCreated()) {
            Sync.Set_Stopped(true);
            Thread.Wait_Close();
        }
    }
    WRes Start() {
        const WRes created = Started.Create();
        return created ? created : Thread.Create(run, this);
    }
    WRes Join() { return Thread.Wait_Close(); }
};

class ProgressSyncTests : public QObject {
    Q_OBJECT

    static QString text(const UString &value) { return QString::fromWCharArray(value); }

  private slots:
    void nullUpdatesPreserveCountersAndClearPaths() {
        CProgressSync sync;
        UInt64 completed = 123;
        QCOMPARE(sync.Set_NumBytesCur(&completed), S_OK);
        QCOMPARE(sync.Set_NumBytesCur(static_cast<const UInt64 *>(NULL)), S_OK);
        QCOMPARE(sync._completedBytes, completed);
        QCOMPARE(sync.Set_NumBytesCur(UInt64(456)), S_OK);
        UInt64 in = 800, out = 200;
        sync.Set_Ratio(&in, &out);
        in = 900;
        sync.Set_Ratio(&in, NULL);
        sync.Set_Ratio(NULL, NULL);
        QCOMPARE(sync._inSize, in);
        QCOMPARE(sync._outSize, out);
        const FString path = us2fs(L"папка с пробелом/архив й.7z");
        QCOMPARE(sync.ScanProgress(10, 2000, path, true), S_OK);
        QCOMPARE(text(sync._filePath), text(fs2us(path)));
        QVERIFY(sync._isDir);
        QCOMPARE(sync._completedBytes, UInt64(456));
        QCOMPARE(sync.Set_Status2(L"Сканирование", NULL), S_OK);
        QVERIFY(sync._filePath.IsEmpty() && !sync._isDir);
        sync.Set_FilePath(L"папка й", true);
        QVERIFY(sync._isDir);
        sync.Set_FilePath(NULL);
        QVERIFY(sync._filePath.IsEmpty() && !sync._isDir);
    }

    void pausedWorkerResumesOrStops_data() {
        QTest::addColumn<bool>("stop");
        QTest::newRow("resume") << false;
        QTest::newRow("cancel-paused") << true;
    }
    void pausedWorkerResumesOrStops() {
        QFETCH(bool, stop);
        CProgressSync sync;
        sync.Set_Paused(true);
        QVERIFY(sync.Get_Paused());
        ProgressWorker worker(sync);
        worker.Call = [&] { return sync.Set_NumBytesCur(UInt64(77)); };
        QCOMPARE(worker.Start(), WRes(0));
        QCOMPARE(worker.Started.Lock(), WRes(0));
        QTest::qWait(30);
        QVERIFY(!worker.Returned.load());
        if (stop)
            sync.Set_Stopped(true);
        else
            sync.Set_Paused(false);
        QTRY_VERIFY_WITH_TIMEOUT(worker.Returned.load(), 2000);
        QCOMPARE(worker.Join(), WRes(0));
        QCOMPARE(worker.Result, stop ? E_ABORT : S_OK);
        QCOMPARE(sync._completedBytes, UInt64(77));
        QCOMPARE(sync.Get_Paused(), stop);
        if (stop) {
            sync.Clear_Stop_Status();
            QVERIFY(!sync.Get_Stopped() && sync.Get_Paused());
            sync.Set_Paused(false);
        }
        QCOMPARE(sync.CheckStop(), S_OK);
    }

    void stoppedCallbacksAbortAndReopenClearsOnlyStop() {
        CProgressSync sync;
        sync.Set_Stopped(true);
        QCOMPARE(sync.CheckStop(), E_ABORT);
        QCOMPARE(sync.Set_NumFilesTotal(12), E_ABORT);
        QCOMPARE(sync.Set_NumBytesCur(UInt64(34)), E_ABORT);
        QCOMPARE(sync.Set_NumBytesCur(static_cast<const UInt64 *>(NULL)), E_ABORT);
        QCOMPARE(sync.ScanProgress(56, 78, us2fs(L"имя й.txt")), E_ABORT);
        QCOMPARE(sync.Set_Status2(L"status", L"path"), E_ABORT);
        sync.AddError_Message(L"retained error");
        sync.Clear_Stop_Status();
        QCOMPARE(sync.CheckStop(), S_OK);
        QCOMPARE(sync._totalFiles, UInt64(56));
        QCOMPARE(sync._totalBytes, UInt64(78));
        QCOMPARE(sync._completedBytes, UInt64(34));
        QCOMPARE(text(sync._status), QString("status"));
        QCOMPARE(sync.Messages.Size(), 1u);
    }

    void originalErrorAccumulationAndFinalMessagePresence() {
        CProgressSync sync;
        QVERIFY(!sync.ThereIsMessage());
        sync.FinalMessage.OkMessage.Title = L"title only";
        QVERIFY(!sync.ThereIsMessage());
        sync.FinalMessage.OkMessage.Message = L"complete";
        QVERIFY(sync.ThereIsMessage());
        sync.FinalMessage.OkMessage.Message.Empty();
        sync.FinalMessage.ErrorMessage.Message = L"failed";
        QVERIFY(sync.ThereIsMessage());
        sync.FinalMessage.ErrorMessage.Message.Empty();
        sync.AddError_Message_Name(L"problem\n\n", L"файл й с пробелом");
        sync.AddError_Message_Name(NULL, L"path only");
        sync.AddError_Code_Name(0, L"zero error path");
        sync.AddError_Code_Name(HRESULT(0xe1234567), L"unknown error path");
        QCOMPARE(sync.Messages.Size(), 4u);
        QCOMPARE(text(sync.Messages[0]), QStringLiteral("файл й с пробелом\nproblem\n"));
        QCOMPARE(text(sync.Messages[1]), QString("path only"));
        QCOMPARE(text(sync.Messages[2]), QString("zero error path\nError"));
        QCOMPARE(text(sync.Messages[3]), QString("unknown error path\nError #E1234567"));
        QVERIFY(sync.ThereIsMessage());
    }
};

QTEST_GUILESS_MAIN(ProgressSyncTests)
#include "progress_sync_tests.moc"
