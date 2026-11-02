#include "7zip/IPassword.h"
#include "Common/MyCom.h"
#include "archive/backend/archivebackend.h"
#include "archive/resources/qtarchivebackendrequest.h"
#include "archive/support/source_extract_fixture.h"
#include "ui/progress/ProgressDialog2.h"
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <chrono>
#include <future>

namespace {
using namespace SourceExtractFixture;

class RequestBackend final : public ArchiveBackend {
  public:
    int Cancellations = 0;
    void cancel() override {
        ++Cancellations;
        QTimer::singleShot(50, this, [this] { emit finished(true, {}); });
    }
};

class PasswordFailure final : public ICryptoGetTextPassword, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_1(ICryptoGetTextPassword)
  public:
    HRESULT Result = E_ABORT;
    int Calls = 0;
    Z7_COM7F_IMF(CryptoGetTextPassword(BSTR *)) override {
        ++Calls;
        return Result;
    }
};
} // namespace

class BackendRequestTests final : public QObject {
    Q_OBJECT
  private slots:
    void cancelledRequestDoesNotStartWhenGuiResumes() {
        ArchiveBackend backend;
        QSignalSpy finished(&backend, &ArchiveBackend::finished);
        CProgressSync sync;
        sync.Set_Stopped(true);
        bool started = false;
        auto request = std::async(std::launch::async, [&] {
            return RunArchiveBackendRequest(
                backend,
                [&] {
                    started = true;
                    emit backend.finished(false, "Cancelled request was dispatched");
                },
                nullptr, &sync);
        });
        // Withhold GUI dispatch until the cancelled caller can release its
        // captures. Still drain on failure so the fixture can join its worker.
        const bool returnedBeforeDispatch =
            request.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
        QTRY_VERIFY_WITH_TIMEOUT(
            request.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready, 10000);
        QCOMPARE(request.get(), HRESULT(E_ABORT));
        QCoreApplication::sendPostedEvents(&backend, QEvent::MetaCall);
        QVERIFY(returnedBeforeDispatch);
        QVERIFY(!started);
        QCOMPARE(finished.size(), 0);
        QVERIFY(!backend.busy());
    }
    void busyRequestLeavesUnrelatedOperationRunning_data() {
        QTest::addColumn<bool>("cancelled");
        QTest::newRow("rejected") << false;
        QTest::newRow("cancelled") << true;
    }
    void busyRequestLeavesUnrelatedOperationRunning() {
        QFETCH(bool, cancelled);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("другой запрос 空.bin"));
        QVERIFY(writeFile(path, "123456789"));
        ArchiveBackend backend;
        QSignalSpy finished(&backend, &ArchiveBackend::finished);
        const auto pause = connect(&backend, &ArchiveBackend::phaseStarted, &backend,
                                   [&] { QCOMPARE(backend.setPaused(true), HRESULT(S_OK)); });
        backend.hashMethods({path}, {"CRC32"});
        disconnect(pause);
        QVERIFY(backend.busy());
        CProgressSync sync;
        sync.Set_Stopped(cancelled);
        bool started = false;
        auto request = std::async(std::launch::async, [&] {
            return RunArchiveBackendRequest(
                backend,
                [&] {
                    started = true;
                    emit backend.finished(false, "Unexpected request start");
                },
                nullptr, &sync);
        });
        QTRY_VERIFY_WITH_TIMEOUT(
            request.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready, 10000);
        QCOMPARE(request.get(), HRESULT(cancelled ? E_ABORT : E_FAIL));
        QVERIFY(!started);
        QVERIFY(backend.busy());
        QCOMPARE(finished.size(), 0);
        QCOMPARE(backend.setPaused(false), HRESULT(S_OK));
        QTRY_COMPARE(finished.size(), 1);
        QVERIFY(finished.front().front().toBool());
        QCOMPARE(backend.lastResult(), HRESULT(S_OK));
    }

    void stoppedRequestJoinsCompletion_data() {
        QTest::addColumn<HRESULT>("result");
        QTest::addColumn<bool>("missing");
        QTest::addColumn<bool>("stop");
        QTest::addColumn<bool>("finishInStart");
        QTest::newRow("password-cancel") << HRESULT(E_ABORT) << false << false << false;
        QTest::newRow("password-error") << HRESULT(E_INVALIDARG) << false << false << false;
        QTest::newRow("password-non-success") << HRESULT(S_FALSE) << false << false << false;
        QTest::newRow("missing-password-interface")
            << HRESULT(E_NOINTERFACE) << true << false << false;
        QTest::newRow("running-cancel") << HRESULT(E_ABORT) << true << true << false;
        QTest::newRow("completed-with-stop") << HRESULT(E_ABORT) << true << true << true;
    }

    void stoppedRequestJoinsCompletion() {
        QFETCH(HRESULT, result);
        QFETCH(bool, missing);
        QFETCH(bool, stop);
        QFETCH(bool, finishInStart);
        RequestBackend backend;
        QSignalSpy finished(&backend, &ArchiveBackend::finished);
        CProgressSync sync;
        CMyComPtr2_Create<ICryptoGetTextPassword, PasswordFailure> password;
        password->Result = result;
        // Complete even if cancellation regresses, so the fixture can join.
        QTimer fallback;
        fallback.setSingleShot(true);
        connect(&fallback, &QTimer::timeout, &backend, [&] { emit backend.finished(true, {}); });
        auto request = std::async(std::launch::async, [&] {
            return RunArchiveBackendRequest(
                backend,
                [&] {
                    fallback.start(1000);
                    if (stop)
                        sync.Set_Stopped(true);
                    else
                        emit backend.passwordRequired();
                    if (finishInStart)
                        emit backend.finished(true, {});
                },
                missing ? nullptr : password.Interface(), &sync);
        });
        QTRY_VERIFY_WITH_TIMEOUT(
            request.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready, 5000);
        QCOMPARE(request.get(), result);
        fallback.stop();
        QCOMPARE(finished.size(), 1);
        QCOMPARE(backend.Cancellations, finishInStart ? 0 : 1);
        QCOMPARE(password->Calls, missing ? 0 : 1);
        QCoreApplication::sendPostedEvents(&backend, QEvent::MetaCall);
        QCOMPARE(backend.Cancellations, finishInStart ? 0 : 1);
    }
};
QTEST_MAIN(BackendRequestTests)
#include "backend_request_tests.moc"
