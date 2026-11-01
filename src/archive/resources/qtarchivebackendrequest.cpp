#include "archive/resources/qtarchivebackendrequest.h"
#include "7zip/IPassword.h"
#include "Common/MyCom.h"
#include "archive/backend/archivebackend.h"
#include "ui/progress/ProgressDialog2.h"
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <memory>
#include <utility>

namespace {
struct CRequest {
    enum class Phase { Pending, Running, Finished };
    QMutex Mutex;
    Phase State = Phase::Pending;
    HRESULT Result = E_FAIL;
    bool PasswordRequested = false;
};
class CConnection {
    QMetaObject::Connection _connection;

  public:
    explicit CConnection(QMetaObject::Connection connection) : _connection(connection) {}
    ~CConnection() { QObject::disconnect(_connection); }
};

HRESULT RequestPassword(ArchiveBackend &backend, const std::shared_ptr<CRequest> &request,
                        IUnknown *callback) {
    try {
        CMyComPtr<ICryptoGetTextPassword> crypto;
        if (callback)
            callback->QueryInterface(IID_ICryptoGetTextPassword, (void **)&crypto);
        if (!crypto)
            return E_NOINTERFACE;
        CMyComBSTR value;
        RINOK(crypto->CryptoGetTextPassword(&value))
        const QString text = QString::fromWCharArray(value);
        QMetaObject::invokeMethod(
            &backend,
            [&backend, request, text] {
                bool provide;
                {
                    QMutexLocker lock(&request->Mutex);
                    provide = request->State == CRequest::Phase::Running;
                }
                if (provide)
                    backend.providePassword(text);
            },
            Qt::QueuedConnection);
        return S_OK;
    } catch (...) {
        return E_FAIL;
    }
}
HRESULT RunRequest(ArchiveBackend &backend, const std::function<void()> &start,
                   IUnknown *passwordCallback, CProgressSync *sync, ArchiveInfo *info = nullptr,
                   QList<ArchiveEntry> *entries = nullptr) {
    if (QThread::currentThread() == backend.thread())
        return E_FAIL;
    const auto request = std::make_shared<CRequest>();
    CConnection infoReady(info
                              ? QObject::connect(&backend, &ArchiveBackend::infoReady, &backend,
                                                 [request, info](const ArchiveInfo &value) {
                                                     QMutexLocker lock(&request->Mutex);
                                                     if (request->State == CRequest::Phase::Running)
                                                         *info = value;
                                                 })
                              : QMetaObject::Connection{});
    CConnection entriesReady(
        entries ? QObject::connect(&backend, &ArchiveBackend::entriesReady, &backend,
                                   [request, entries](const QList<ArchiveEntry> &value) {
                                       QMutexLocker lock(&request->Mutex);
                                       if (request->State == CRequest::Phase::Running)
                                           *entries = value;
                                   })
                : QMetaObject::Connection{});
    CConnection finished(QObject::connect(&backend, &ArchiveBackend::finished, &backend,
                                          [&backend, request](bool success, const QString &) {
                                              QMutexLocker lock(&request->Mutex);
                                              if (request->State != CRequest::Phase::Running)
                                                  return;
                                              request->Result =
                                                  success ? S_OK : backend.lastResult();
                                              if (!success && request->Result == S_OK)
                                                  request->Result = E_FAIL;
                                              request->State = CRequest::Phase::Finished;
                                          }));
    CConnection password(
        QObject::connect(&backend, &ArchiveBackend::passwordRequired, &backend, [request] {
            QMutexLocker lock(&request->Mutex);
            if (request->State == CRequest::Phase::Running)
                request->PasswordRequested = true;
        }));
    QMetaObject::invokeMethod(
        &backend,
        [&backend, request, start] {
            {
                QMutexLocker lock(&request->Mutex);
                if (backend.busy()) {
                    request->State = CRequest::Phase::Finished;
                    return;
                }
                request->State = CRequest::Phase::Running;
            }
            try {
                start();
            } catch (...) {
                backend.cancelAndWait();
                QMutexLocker lock(&request->Mutex);
                request->Result = E_FAIL;
                request->State = CRequest::Phase::Finished;
            }
        },
        Qt::QueuedConnection);
    HRESULT stopResult = S_OK;
    bool cancelRequested = false;
    for (;;) {
        if (stopResult == S_OK && sync && sync->Get_Stopped())
            stopResult = E_ABORT;
        bool passwordRequested;
        {
            QMutexLocker lock(&request->Mutex);
            if (request->State == CRequest::Phase::Finished)
                return stopResult == S_OK ? request->Result : stopResult;
            passwordRequested =
                stopResult == S_OK && std::exchange(request->PasswordRequested, false);
        }
        if (passwordRequested) {
            stopResult = RequestPassword(backend, request, passwordCallback);
        }
        if (stopResult != S_OK && !cancelRequested) {
            cancelRequested = true;
            QMetaObject::invokeMethod(
                &backend,
                [&backend, request] {
                    bool cancel;
                    {
                        QMutexLocker lock(&request->Mutex);
                        cancel = request->State == CRequest::Phase::Running;
                    }
                    if (cancel)
                        backend.cancel();
                },
                Qt::QueuedConnection);
        }
        QThread::msleep(10);
    }
}
} // namespace

HRESULT RunArchiveBackendRequest(ArchiveBackend &backend, const std::function<void()> &start,
                                 IUnknown *passwordCallback, CProgressSync *sync) {
    return RunRequest(backend, start, passwordCallback, sync);
}

HRESULT ReadArchiveBackendListing(ArchiveBackend &backend, const QString &path,
                                  const QString &format, const QString &password,
                                  IUnknown *passwordCallback, CProgressSync *sync,
                                  ArchiveInfo &info, QList<ArchiveEntry> &entries) {
    return RunRequest(
        backend, [&backend, path, format, password] { backend.openFolder(path, format, password); },
        passwordCallback, sync, &info, &entries);
}
