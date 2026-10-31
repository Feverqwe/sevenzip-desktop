#include "archive/resources/qtarchivefoldermanager.h"
#include "Common/ComTry.h"
#include "archive/agent/Agent.h"
#include "archive/backend/archivebackend.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/resources/archiveinputlayer.h"
#include "archive/resources/archivenativeopen.h"
#include "archive/resources/qtarchiveagentopen.h"
#include "archive/resources/qtarchivefileresource.h"
#include "archive/resources/qtarchivenativeopenprogress.h"
#include "archive/resources/qtarchiveproperties.h"
#include "archive/resources/qtfilesystemreadprompt.h"
#include "devices/deviceaccess.h"
#include "filemanager/OpenCallback.h"
#include "filemanager/providers/qtlocationfolder.h"
#include "ui/progress/archiveprogresstransport.h"
#include <QFile>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QScopeGuard>
#include <QSemaphore>
#include <QThread>
#include <atomic>

struct QtArchiveFolderManager::CRequest {
    QMutex Mutex;
    bool Completed = false;
    HRESULT Result = E_FAIL;
    QList<ArchiveEntry> Entries;
    ArchiveInfo Info;
    CMyComPtr<IFolderFolder> Folder;
    CMyComPtr<IUnknown> Resource;
    std::atomic<bool> PasswordRequested{false};
};

struct QtArchiveFolderManager::COpenServices final : IQtAgentOpenServices {
    QtArchiveFolderManager &Manager;
    IProgress *Progress;
    bool OwnedStreamFile;
    bool MaterializeStreamFile;
    const COpenOptions *Requested;
    UString Format;
    CMyComPtr<IUnknown> Lease;
    std::shared_ptr<const ArchiveCodecCatalog> Catalog;
    COpenServices(QtArchiveFolderManager &manager, const wchar_t *path, const wchar_t *format,
                  IProgress *progress, bool ownedStreamFile, const COpenOptions *requested,
                  bool materializeStream)
        : Manager(manager), Progress(progress), OwnedStreamFile(ownedStreamFile),
          MaterializeStreamFile(materializeStream), Requested(requested),
          Format(format ? format : L""),
          Lease(new QtArchiveFileResource(QString::fromWCharArray(path))) {}
    HRESULT LoadCodecs(const CCodecs *&codecs) override {
        RINOK(Manager.WaitForCodecCatalog(Catalog, Progress))
        codecs = Catalog && Catalog->result() == S_OK ? &Catalog->nativeCodecs() : nullptr;
        static_cast<QtArchiveFileResource *>(Lease.Interface())->AttachCodecCatalog(Catalog);
        return S_OK;
    }
    IUnknown *Resource() const override { return Lease; }
    HRESULT Open(CArchiveLink &link, const COpenOptions &options, IOpenCallbackUI *) override {
        return Manager.OpenLinkImpl(link, options, *this);
    }
};

HRESULT
QtArchiveFolderManager::WaitForCodecCatalog(std::shared_ptr<const ArchiveCodecCatalog> &catalog,
                                            IProgress *progress) {
    struct Request {
        QSemaphore Ready;
        std::atomic<bool> Cancelled{false};
        QMetaObject::Connection Connection;
        std::shared_ptr<const ArchiveCodecCatalog> Catalog;
    };
    const auto request = std::make_shared<Request>();
    QMetaObject::invokeMethod(
        this,
        [this, request] {
            if (request->Cancelled.load())
                return;
            const auto complete = [this, request] {
                request->Catalog = _backend.codecCatalog();
                QObject::disconnect(request->Connection);
                request->Ready.release();
            };
            if (!_backend.codecCatalog() && !_backend.busy())
                _backend.discoverCodecCatalog();
            if (_backend.discoveringCodecs())
                request->Connection =
                    connect(&_backend, &ArchiveBackend::codecCatalogReady, this, complete);
            else
                complete();
        },
        Qt::QueuedConnection);
    auto *callback = dynamic_cast<COpenCallbackImp *>(progress);
    CNativeOpenProgress nativeProgress(progress, callback ? callback->Callback : nullptr);
    while (!request->Ready.tryAcquire(1, 10)) {
        const HRESULT result = nativeProgress.Open_CheckBreak();
        if (result != S_OK) {
            request->Cancelled.store(true);
            QMetaObject::invokeMethod(
                this, [request] { QObject::disconnect(request->Connection); },
                Qt::QueuedConnection);
            return result;
        }
    }
    catalog = std::move(request->Catalog);
    return nativeProgress.Open_CheckBreak();
}

QtArchiveFolderManager::QtArchiveFolderManager(ArchiveBackend &backend) : _backend(backend) {
    connect(&backend, &ArchiveBackend::infoReady, this, [this](const ArchiveInfo &info) {
        if (_request)
            _request->Info = info;
    });
    connect(&backend, &ArchiveBackend::entriesReady, this,
            [this](const QList<ArchiveEntry> &entries) {
                if (_request)
                    _request->Entries = entries;
            });
    connect(&backend, &ArchiveBackend::passwordRequired, this, [this] {
        if (_request)
            _request->PasswordRequested.store(true);
    });
    connect(&backend, &ArchiveBackend::finished, this, [this](bool success, const QString &) {
        if (!_request)
            return;
        const auto request = std::move(_request);
        HRESULT result = success ? E_FAIL : _backend.lastResult();
        if (!success && result == S_OK)
            result = E_FAIL;
        try {
            if (success) {
                auto *resource =
                    dynamic_cast<QtArchiveFileResource *>(request->Resource.Interface());
                if (resource) {
                    resource->Info = request->Info;
                    const QString transportFormat = resource->TransportFormat();
                    resource->AttachReopenService(_backend, transportFormat);
                    // A split archive opens through the outer Split handler before its
                    // contained format; forcing -t7z on .001 bypasses that handler.
                    resource->AttachExtractService(_backend, transportFormat);
                    if (_backend.supportsArchiveUpdate(request->Info.format))
                        resource->AttachUpdateService(_backend, request->Info.format);
                }
                result = CreateArchiveMetadata(request->Entries, &request->Folder);
            }
        } catch (...) {
            result = E_FAIL;
        }
        QMutexLocker lock(&request->Mutex);
        request->Result = result;
        request->Completed = true;
    });
}

QtArchiveFolderManager::~QtArchiveFolderManager() {
    // Source Create joins the worker before its manager reference is released.
    Q_ASSERT(!_request);
    disconnect(&_backend, nullptr, this, nullptr);
}

void QtArchiveFolderManager::Start(const std::shared_ptr<CRequest> &request, const QString &path,
                                   const QString &format, const QString &password) {
    if (_request || _backend.busy()) {
        QMutexLocker lock(&request->Mutex);
        request->Result = E_FAIL;
        request->Completed = true;
        return;
    }
    _request = request;
    try {
        _backend.openFolder(path, format, password);
    } catch (...) {
        // A start implementation may throw after launching its owned process.
        _backend.cancelAndWait();
        _request.reset();
        QMutexLocker lock(&request->Mutex);
        request->Result = E_FAIL;
        request->Completed = true;
    }
}

Z7_COM7F_IMF(QtArchiveFolderManager::OpenFolderFile(IInStream *inStream, const wchar_t *filePath,
                                                    const wchar_t *arcFormat,
                                                    IFolderFolder **resultFolder,
                                                    IProgress *progress)) {
    return OpenFolderFileImpl(inStream, filePath, arcFormat, resultFolder, progress, false);
}

HRESULT QtArchiveFolderManager::OpenOwnedStreamFile(IInStream *stream, const wchar_t *path,
                                                    const wchar_t *format, IFolderFolder **folder,
                                                    IProgress *progress, bool materialize,
                                                    bool *materialized) {
    if (!materialized)
        return E_INVALIDARG;
    *materialized = false;
    const HRESULT result =
        OpenFolderFileImpl(stream, path, format, folder, progress, true, nullptr, materialize);
    if (result == S_OK && folder && *folder) {
        auto *agent = dynamic_cast<CAgentFolder *>(*folder);
        auto *resource =
            agent ? dynamic_cast<QtArchiveFileResource *>(agent->GetEngineResource()) : nullptr;
        *materialized = resource && !resource->HasStreamInput();
    }
    return result;
}

HRESULT QtArchiveFolderManager::OpenArchive(const COpenOptions &options, const wchar_t *format,
                                            IFolderFolder **folder, IProgress *progress) {
    return OpenFolderFileImpl(options.stream, options.filePath, format, folder, progress, false,
                              &options);
}

HRESULT QtArchiveFolderManager::OpenFolderFileImpl(IInStream *inStream, const wchar_t *filePath,
                                                   const wchar_t *arcFormat,
                                                   IFolderFolder **resultFolder,
                                                   IProgress *progress, bool ownedStreamFile,
                                                   const COpenOptions *options,
                                                   bool materializeStream) {
    COM_TRY_BEGIN
    if (!resultFolder)
        return E_INVALIDARG;
    *resultFolder = nullptr;
    // Native discovery and the typed backend metadata service use the GUI
    // owner. Only a source worker can wait without blocking that event loop.
    if (QThread::currentThread() == thread())
        return E_FAIL;
    if (!filePath || !*filePath)
        return E_INVALIDARG;
    COpenServices services(*this, filePath, arcFormat, progress, ownedStreamFile, options,
                           materializeStream);
    CArchiveFolderManager manager(services);
    return manager.OpenFolderFile(inStream, filePath, services.Format, resultFolder, progress);
    COM_TRY_END
}

HRESULT QtArchiveFolderManager::OpenLinkImpl(CArchiveLink &link, const COpenOptions &sourceOptions,
                                             COpenServices &services) {
    COM_TRY_BEGIN
    IInStream *inStream = sourceOptions.stream;
    const wchar_t *filePath = sourceOptions.filePath;
    IProgress *progress = services.Progress;
    const bool ownedStreamFile = services.OwnedStreamFile;
    auto *resource = static_cast<QtArchiveFileResource *>(services.Resource());
    QString ownedPath;
    bool keepOwnedPath = false;
    const auto cleanup = qScopeGuard([&] {
        if (!keepOwnedPath && !ownedPath.isEmpty()) {
            resource->CloseNativeOpen();
            QFile::remove(ownedPath);
        }
    });
    const QString format = QString::fromWCharArray(services.Format);
    auto *callback = dynamic_cast<COpenCallbackImp *>(progress);
    auto *ui = callback ? dynamic_cast<COpenArchiveCallback *>(callback->Callback) : nullptr;
    if (ui)
        ui->ProgressDialog.WaitCreating();
    if (dynamic_cast<DirectDeviceInput *>(inStream)) {
        CNativeOpenProgress nativeProgress(progress, callback ? callback->Callback : nullptr);
        QString error;
        const HRESULT result =
            resource->OpenDeviceInput(_backend, inStream, &nativeProgress, &error);
        // The source opening worker is joined before CFfpOpen reads this
        // per-opening diagnostic. No widget access or shared backend state.
        if (ui)
            ui->DeviceOpenError = error;
        RINOK(result)
        return CreateArchiveLinkTransport(link, nullptr, resource, sourceOptions.codecs);
    }
    // CopyTo's in-memory result keeps its established owned-file publication
    // and editing lifetime. Real handler streams stay direct.
    const bool streamInput = inStream && services.Catalog && services.Catalog->result() == S_OK &&
                             !services.MaterializeStreamFile;
    if (inStream && !streamInput) {
        if (ownedStreamFile) {
            RINOK(resource->SpoolToOwnedFile(inStream, QString::fromWCharArray(filePath), progress))
            ownedPath = QString::fromWCharArray(filePath);
        } else
            RINOK(resource->Spool(inStream, progress))
    }
    const QString path = resource->EnginePath;
    CNativeOpenProgress nativeProgress(progress, callback ? callback->Callback : nullptr);
    std::shared_ptr<ArchiveNativeOpen> nativeOpen;
    std::shared_ptr<ArchiveInputLayer> inputLayer;
    CMyComPtr<IInStream> inputCursor;
    if (streamInput) {
        const auto readRequest = CreateArchiveOpenReadRequest(&nativeProgress);
        auto layer = ArchiveInputLayer::Create(inStream, readRequest);
        if (!layer)
            return layer.error();
        inputLayer = std::move(*layer);
        RINOK(inputLayer->OpenCursor(&inputCursor))
    }
    COpenOptions nativeOptions = services.Requested ? *services.Requested : sourceOptions;
    nativeOptions.stream = inputCursor;
    nativeOptions.QtOpenService = nullptr;
    const HRESULT nativeResult = _backend.openNativeArchive(
        services.Catalog, path, format, &nativeProgress, nativeOpen, &nativeOptions);
    if (nativeOpen) {
        link.NonOpen_ArcPath = nativeOpen->archiveLink().NonOpen_ArcPath;
        link.NonOpen_ErrorInfo = nativeOpen->archiveLink().NonOpen_ErrorInfo;
    }
    if (nativeResult != S_OK) {
        if (nativeResult != S_FALSE || !nativeOpen || nativeOpen->archiveLink().Arcs.IsEmpty())
            return nativeResult;
    }
    QString limitations;
    bool nativeMetadata = false;
    if (nativeOpen) {
        auto *consent = dynamic_cast<IQtArchiveReadConsent *>(progress);
        auto *dialog = ui ? &ui->ProgressDialog : consent ? consent->ReadConsentDialog() : nullptr;
        auto *readCallback = ui ? ui : consent ? consent->ReadConsentCallback() : nullptr;
        RINOK(PrepareLimitedArchiveRead(nativeOpen->archiveLink(), nativeOpen->nativeCodecs(),
                                        dialog, readCallback, {}, limitations, nativeMetadata))
    }
    resource->SetFilesystemReadLimitations(limitations);
    resource->AttachNativeOpen(std::move(nativeOpen));
    if (streamInput)
        resource->RetainStreamInput(std::move(inputLayer));
    if (resource->NativeArchiveLink()) {
        resource->UseNativeMetadata();
        resource->AttachReopenService(_backend, streamInput ? format : resource->TransportFormat());
        resource->AttachExtractService(_backend, resource->TransportFormat());
        if (resource->HasNativeUpdateService())
            resource->AttachUpdateService(_backend, resource->TransportFormat());
        RINOK(CreateArchiveLinkTransport(link, nullptr, resource, sourceOptions.codecs))
        keepOwnedPath = true;
        return nativeResult;
    }
    const QString listingFormat =
        resource->RequiresOuterOpen() ? resource->TransportFormat() : format;
    const QString password = ui && ui->PasswordIsDefined
                                 ? QString::fromWCharArray(ui->Password.Ptr())
                                 : nativeProgress.Password;
    const auto request = std::make_shared<CRequest>();
    request->Resource = resource;
    QMetaObject::invokeMethod(
        this,
        [this, request, path, listingFormat, password] {
            Start(request, path, listingFormat, password);
        },
        Qt::QueuedConnection);
    HRESULT stopResult = S_OK;
    bool cancelRequested = false;
    for (;;) {
        {
            QMutexLocker lock(&request->Mutex);
            if (request->Completed) {
                if (stopResult != S_OK)
                    return stopResult;
                if (request->Result == S_OK) {
                    auto *consent = dynamic_cast<IQtArchiveReadConsent *>(progress);
                    auto *dialog = ui        ? &ui->ProgressDialog
                                   : consent ? consent->ReadConsentDialog()
                                             : nullptr;
                    auto *readCallback = ui        ? ui
                                         : consent ? consent->ReadConsentCallback()
                                                   : nullptr;
                    RINOK(ConfirmLimitedArchiveRead(dialog, readCallback, {},
                                                    request->Info.readLimitations))
                    resource->SetFilesystemReadLimitations(request->Info.readLimitations);
                    RINOK(CreateArchiveLinkTransport(link, request->Folder, resource,
                                                     sourceOptions.codecs))
                    keepOwnedPath = true;
                    return nativeResult;
                }
                return request->Result;
            }
        }
        if (stopResult == S_OK && request->PasswordRequested.exchange(false)) {
            try {
                CMyComPtr<ICryptoGetTextPassword> crypto;
                if (progress)
                    progress->QueryInterface(IID_ICryptoGetTextPassword, (void **)&crypto);
                if (!crypto)
                    stopResult = E_NOINTERFACE;
                else {
                    CMyComBSTR password;
                    stopResult = crypto->CryptoGetTextPassword(&password);
                    if (stopResult == S_OK) {
                        const QString value = QString::fromWCharArray(password);
                        QMetaObject::invokeMethod(
                            this,
                            [this, request, value] {
                                if (_request == request)
                                    _backend.providePassword(value);
                            },
                            Qt::QueuedConnection);
                    }
                }
            } catch (...) {
                stopResult = E_FAIL;
            }
        }
        // An external child can finish while paused. Do not enter the source
        // cooperative pause wait before observing its completion latch.
        if (stopResult == S_OK && ui && ui->ProgressDialog.Sync.Get_Stopped())
            stopResult = E_ABORT;
        if (stopResult != S_OK && !cancelRequested) {
            cancelRequested = true;
            QMetaObject::invokeMethod(
                this,
                [this, request] {
                    if (_request == request)
                        _backend.cancel();
                },
                Qt::QueuedConnection);
        }
        QThread::msleep(10);
    }
    COM_TRY_END
}

CArchiveFolderManager *QtArchiveFolderManager::AssociationManager() {
    const auto catalog = _backend.codecCatalog();
    if (!catalog || catalog->result() != S_OK)
        return nullptr;
    if (_associationCatalog != catalog) {
        _associationManager.reset();
        _associationCatalog = catalog;
        _associationManager = std::make_unique<CArchiveFolderManager>(catalog->nativeCodecs());
    }
    return _associationManager.get();
}

Z7_COM7F_IMF(QtArchiveFolderManager::GetExtensions(BSTR *extensions)) {
    if (!extensions)
        return E_INVALIDARG;
    *extensions = nullptr;
    auto *manager = AssociationManager();
    return manager ? manager->GetExtensions(extensions) : E_FAIL;
}

Z7_COM7F_IMF(QtArchiveFolderManager::GetIconPath(const wchar_t *ext, BSTR *iconPath,
                                                 Int32 *iconIndex)) {
    if (!ext || !iconPath || !iconIndex)
        return E_INVALIDARG;
    *iconPath = nullptr;
    *iconIndex = 0;
    auto *manager = AssociationManager();
    return manager ? manager->GetIconPath(ext, iconPath, iconIndex) : E_FAIL;
}

std::unique_ptr<IQtProgressTransport>
QtArchiveFolderManager::CreateProgressTransport(CProgressDialog &dialog) {
    return std::make_unique<ArchiveProgressTransport>(dialog, _backend, true);
}
