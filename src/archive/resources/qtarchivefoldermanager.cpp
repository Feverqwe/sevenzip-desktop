#include "archive/resources/qtarchivefoldermanager.h"
#include "Common/ComTry.h"
#include "archive/agent/Agent.h"
#include "archive/backend/archivebackend.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/resources/archiveinputlayer.h"
#include "archive/resources/archivenativeopen.h"
#include "archive/resources/qtarchiveagentopen.h"
#include "archive/resources/qtarchivebackendrequest.h"
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
#include <QScopeGuard>
#include <QSemaphore>
#include <QThread>
#include <atomic>

struct QtArchiveFolderManager::COpenServices final : IQtAgentOpenServices {
    QtArchiveFolderManager &Manager;
    IProgress *Progress;
    bool OwnedStreamFile;
    bool MaterializeStreamFile;
    const COpenOptions *Requested;
    UString Format;
    CMyComPtr2<IUnknown, QtArchiveFileResource> Lease;
    std::shared_ptr<const ArchiveCodecCatalog> Catalog;
    COpenServices(QtArchiveFolderManager &manager, const wchar_t *path, const wchar_t *format,
                  IProgress *progress, bool ownedStreamFile, const COpenOptions *requested,
                  bool materializeStream)
        : Manager(manager), Progress(progress), OwnedStreamFile(ownedStreamFile),
          MaterializeStreamFile(materializeStream), Requested(requested),
          Format(format ? format : L"") {
        Lease.SetFromCls(new QtArchiveFileResource(QString::fromWCharArray(path)));
    }
    HRESULT LoadCodecs(const CCodecs *&codecs) override {
        RINOK(Manager.WaitForCodecCatalog(Catalog, Progress))
        codecs = Catalog && Catalog->result() == S_OK ? &Catalog->nativeCodecs() : nullptr;
        Lease->AttachCodecCatalog(Catalog);
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

QtArchiveFolderManager::QtArchiveFolderManager(ArchiveBackend &backend) : _backend(backend) {}

QtArchiveFolderManager::~QtArchiveFolderManager() = default;

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
    auto *resource = services.Lease.ClsPtr();
    QString ownedPath;
    auto cleanup = qScopeGuard([&] {
        if (!ownedPath.isEmpty()) {
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
    auto *consent = dynamic_cast<IQtArchiveReadConsent *>(progress);
    auto *dialog = ui ? &ui->ProgressDialog : consent ? consent->ReadConsentDialog() : nullptr;
    auto *readCallback = ui ? ui : consent ? consent->ReadConsentCallback() : nullptr;
    if (nativeOpen) {
        RINOK(PrepareLimitedArchiveRead(nativeOpen->archiveLink(), nativeOpen->nativeCodecs(),
                                        dialog, readCallback, {}, limitations))
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
        cleanup.dismiss();
        return nativeResult;
    }
    const QString listingFormat =
        resource->RequiresOuterOpen() ? resource->TransportFormat() : format;
    const QString password = ui && ui->PasswordIsDefined
                                 ? QString::fromWCharArray(ui->Password.Ptr())
                                 : nativeProgress.Password;
    ArchiveInfo info;
    QList<ArchiveEntry> entries;
    RINOK(ReadArchiveBackendListing(_backend, path, listingFormat, password, progress,
                                    ui ? &ui->ProgressDialog.Sync : nullptr, info, entries))
    CMyComPtr<IFolderFolder> folder;
    try {
        resource->Info = info;
        const QString transportFormat = resource->TransportFormat();
        resource->AttachReopenService(_backend, transportFormat);
        resource->AttachExtractService(_backend, transportFormat);
        if (_backend.supportsArchiveUpdate(info.format))
            resource->AttachUpdateService(_backend, info.format);
        RINOK(CreateArchiveMetadata(entries, &folder))
    } catch (...) {
        return E_FAIL;
    }
    RINOK(ConfirmLimitedArchiveRead(dialog, readCallback, {}, info.readLimitations))
    resource->SetFilesystemReadLimitations(info.readLimitations);
    RINOK(CreateArchiveLinkTransport(link, folder, resource, sourceOptions.codecs))
    cleanup.dismiss();
    return nativeResult;
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
