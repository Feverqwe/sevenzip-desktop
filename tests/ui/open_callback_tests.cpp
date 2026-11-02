#include "Windows/PropVariant.h"
#include "archive/operations/ZipRegistry.h"
#include "dialogs/password/PasswordDialog.h"
#include "filemanager/OpenCallback.h"
#include <QApplication>
#include <QCheckBox>
#include <QFile>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

namespace {
class CStopBeforeDispatch final : public QObject {
    CProgressDialog &progress_;

  public:
    bool Armed = false;
    bool Stopped = false;
    explicit CStopBeforeDispatch(CProgressDialog &progress) : progress_(progress) {
        progress.installEventFilter(this);
    }
    bool eventFilter(QObject *, QEvent *event) override {
        if (Armed && event->type() == QEvent::MetaCall) {
            Armed = false;
            Stopped = true;
            progress_.Sync.Set_Stopped(true);
        }
        return false;
    }
};

struct CPasswordRequest {
    COpenArchiveCallback Callback;
    HRESULT Result = E_FAIL;
    BSTR Password = nullptr;
    CStopBeforeDispatch *StopBeforeDispatch = nullptr;
    ~CPasswordRequest() { SysFreeString(Password); }
    static THREAD_FUNC_DECL Run(void *param) {
        auto &request = *static_cast<CPasswordRequest *>(param);
        CProgressCloser closer(request.Callback.ProgressDialog);
        if (request.StopBeforeDispatch) {
            request.Callback.ProgressDialog.WaitCreating();
            // Arm before the callback's queued GUI delivery. Cancellation then
            // happens in that event's filter, after worker-side prechecks.
            QMetaObject::invokeMethod(
                &request.Callback.ProgressDialog,
                [&request] { request.StopBeforeDispatch->Armed = true; }, Qt::QueuedConnection);
        }
        request.Result = request.Callback.Open_CryptoGetTextPassword(&request.Password);
        return 0;
    }
};
} // namespace

class OpenCallbackTests : public QObject {
    Q_OBJECT
  private slots:
    void stoppedCallbackAbortsOpening() {
        COpenArchiveCallback callback;
        UInt64 files = 9, bytes = 4096;
        callback.ProgressDialog.Sync.Set_Stopped(true);
        QCOMPARE(callback.Open_SetTotal(&files, &bytes), HRESULT(E_ABORT));
        QCOMPARE(callback.Open_CheckBreak(), HRESULT(E_ABORT));
        QCOMPARE(callback.Open_Finished(), HRESULT(E_ABORT));
    }

    void stoppedPasswordRequestDoesNotPrompt_data() {
        QTest::addColumn<bool>("queued");
        QTest::newRow("already-stopped") << false;
        QTest::newRow("stopped-at-gui-dispatch") << true;
    }
    void stoppedPasswordRequestDoesNotPrompt() {
        QFETCH(bool, queued);
        QTemporaryDir settings;
        QVERIFY(settings.isValid());
        CPasswordRequest request;
        request.Callback.SettingsPath = settings.filePath("options.ini");
        CStopBeforeDispatch stop(request.Callback.ProgressDialog);
        bool prompted = false;
        QTimer responder;
        connect(&responder, &QTimer::timeout, this, [&] {
            auto *dialog = dynamic_cast<CPasswordDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            prompted = true;
            dialog->findChild<QCheckBox *>("passwordShow")->setChecked(true);
            dialog->findChild<QLineEdit *>("passwordEdit")->setText("discarded fixture");
            dialog->accept();
        });
        responder.start(5); // Also bounds the unfixed run if a late prompt opens.
        if (queued) {
            request.StopBeforeDispatch = &stop;
            NWindows::CThread worker;
            QCOMPARE(worker.Create(CPasswordRequest::Run, &request), WRes(0));
            request.Callback.StartProgressDialog(UString(L"Cancelled opening"), worker);
            QVERIFY(stop.Stopped);
        } else {
            request.Callback.ProgressDialog.Sync.Set_Stopped(true);
            request.Result = request.Callback.Open_CryptoGetTextPassword(&request.Password);
        }
        responder.stop();
        QCOMPARE(request.Result, HRESULT(E_ABORT));
        QVERIFY(!prompted);
        QVERIFY(!request.Password);
        QVERIFY(!request.Callback.PasswordIsDefined);
        QVERIFY(!NExtract::Read_ShowPassword(request.Callback.SettingsPath));
    }

    void volumeSafetyAndRetainedStream() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString name = QStringLiteral("том й space.002");
        QFile file(temp.filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("volume fixture"), qint64(14));
        file.close();
        auto *implementation = new COpenCallbackImp;
        CMyComPtr<IArchiveOpenCallback> owner = implementation;
        CMyComPtr<IArchiveOpenVolumeCallback> volumes;
        QCOMPARE(owner.QueryInterface(IID_IArchiveOpenVolumeCallback, &volumes), HRESULT(S_OK));
        const std::wstring prefix = (temp.path() + '/').toStdWString();
        const std::wstring nativeName = name.toStdWString();
        QCOMPARE(implementation->Init2(us2fs(prefix.c_str()), us2fs(nativeName.c_str())),
                 HRESULT(S_OK));
        CMyComPtr<IInStream> stream;
        for (const QString &unsafe : {QStringLiteral("../outside"), temp.filePath(name),
                                      QStringLiteral("dir/../../outside"), QStringLiteral(".")}) {
            const std::wstring path = unsafe.toStdWString();
            QCOMPARE(volumes->GetStream(path.c_str(), &stream), HRESULT(S_FALSE));
            QVERIFY(!stream);
        }
        QCOMPARE(volumes->GetStream(nativeName.c_str(), &stream), HRESULT(S_OK));
        QVERIFY(stream);
        QCOMPARE(implementation->FileNames.Size(), unsigned(1));
        QVERIFY(implementation->FileNames_WasUsed[0]);
        char data[15]{};
        UInt32 read = 0;
        QCOMPARE(stream->Read(data, 6, &read), HRESULT(S_OK));
        QCOMPARE(read, UInt32(6));
        QCOMPARE(QByteArray(data, int(read)), QByteArray("volume"));
        // Native LRU close/reopen must preserve the current logical stream position.
        implementation->Volumes.Streams[0].LocalPos = 6;
        implementation->Volumes.CloseFile(0);
        volumes.Release();
        owner.Release();
        QCOMPARE(stream->Read(data, 8, &read), HRESULT(S_OK));
        QCOMPARE(read, UInt32(8));
        QCOMPARE(QByteArray(data, int(read)), QByteArray(" fixture"));
        stream.Release(); // last reference also releases volume owner safely
    }

    void passwordReuse() {
        COpenArchiveCallback callback;
        callback.PasswordIsDefined = true;
        callback.Password = L"fixture";
        auto *implementation = new COpenCallbackImp;
        CMyComPtr<IArchiveOpenCallback> owner = implementation;
        implementation->Callback = &callback;
        CMyComPtr<ICryptoGetTextPassword> passwordCallback;
        QCOMPARE(owner.QueryInterface(IID_ICryptoGetTextPassword, &passwordCallback),
                 HRESULT(S_OK));
        BSTR password = nullptr;
        QCOMPARE(passwordCallback->CryptoGetTextPassword(&password), HRESULT(S_OK));
        QCOMPARE(QString::fromWCharArray(password), QStringLiteral("fixture"));
        SysFreeString(password);
        QVERIFY(callback.PasswordWasAsked);
        QVERIFY(implementation->PasswordWasAsked);
    }

    void passwordPresentationAndPersistence_data() {
        QTest::addColumn<bool>("accept");
        QTest::addColumn<QString>("value");
        QTest::newRow("accepted-empty") << true << QString();
        QTest::newRow("accepted-raw-unicode") << true << QStringLiteral("  fixture 密碼 🗜  ");
        QTest::newRow("cancelled") << false << QStringLiteral("discarded fixture");
    }
    void passwordPresentationAndPersistence() {
        QFETCH(bool, accept);
        QFETCH(QString, value);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        CPasswordRequest request;
        request.Callback.SettingsPath = temp.filePath("options.ini");
        QVERIFY(!NExtract::Read_ShowPassword(request.Callback.SettingsPath));
        bool promptSeen = false;
        QTimer responder;
        connect(&responder, &QTimer::timeout, this, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog || dialog->objectName() != "passwordDialog")
                return;
            promptSeen = true;
            auto *show = dialog->findChild<QCheckBox *>("passwordShow");
            auto *edit = dialog->findChild<QLineEdit *>("passwordEdit");
            QVERIFY(show);
            QVERIFY(edit);
            QVERIFY(!show->isChecked());
            show->setChecked(true);
            edit->setText(value);
            responder.stop();
            if (accept)
                dialog->accept();
            else
                dialog->reject();
        });
        responder.start(5);
        NWindows::CThread worker;
        QCOMPARE(worker.Create(CPasswordRequest::Run, &request), WRes(0));
        request.Callback.StartProgressDialog(UString(L"Open fixture"), worker);
        responder.stop();
        QVERIFY(promptSeen);
        QCOMPARE(request.Result, HRESULT(accept ? S_OK : E_ABORT));
        QCOMPARE(request.Callback.PasswordIsDefined, accept);
        QCOMPARE(NExtract::Read_ShowPassword(request.Callback.SettingsPath), accept);
        if (accept) {
            QVERIFY(request.Password);
            QCOMPARE(QString::fromWCharArray(request.Password), value);
            BSTR cached = nullptr;
            QCOMPARE(request.Callback.Open_CryptoGetTextPassword(&cached), HRESULT(S_OK));
            const QString cachedValue = QString::fromWCharArray(cached);
            SysFreeString(cached);
            QCOMPARE(cachedValue, value);
            QVERIFY(!QApplication::activeModalWidget());
        }
        QVERIFY(request.Callback.PasswordWasAsked);
    }
};

QTEST_MAIN(OpenCallbackTests)
#include "open_callback_tests.moc"
