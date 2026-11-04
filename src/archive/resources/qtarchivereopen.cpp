// Reopen the file-only engine metadata without creating another archive owner.
#include "archive/resources/qtarchivereopen.h"
#include "7zip/Archive/IArchive.h"
#include "7zip/IPassword.h"
#include "7zip/UI/FileManager/resourceGui.h"
#include "Common/ComTry.h"
#include "Common/StringConvert.h"
#include "archive/agent/Agent.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/resources/archiveinputlayer.h"
#include "archive/resources/archivenativeopen.h"
#include "archive/resources/qtarchivebackendrequest.h"
#include "archive/resources/qtarchivefileresource.h"
#include "archive/resources/qtarchivenativeopenprogress.h"
#include "archive/resources/qtfilesystemreadprompt.h"
#include "filemanager/OpenCallback.h"
#include "filemanager/providers/qtlocationfolder.h"
#include "localization/LangUtils.h"
#include <QCoreApplication>
#include <QPointer>
#include <QScopeGuard>
#include <QThread>
#include <memory>

// Native FileFolderPluginOpen worker/modal lifecycle, with ReOpen as its action.
HRESULT ReOpenArchiveFolder(CAgentFolder &folder, const QString &settingsPath, QWidget *parent,
                            IFolderFolder **resultFolder, bool usePassword,
                            const UString &password) {
    if (!resultFolder)
        return E_INVALIDARG;
    *resultFolder = nullptr;
    struct CThreadArchiveReOpen {
        CAgentFolder *Folder;
        COpenArchiveCallback UI;
        CMyComPtr<IArchiveOpenCallback> Callback;
        CMyComPtr<IFolderFolder> ResultFolder;
        HRESULT Result = E_FAIL;
        static THREAD_FUNC_DECL Run(void *param) {
            auto &request = *static_cast<CThreadArchiveReOpen *>(param);
            CProgressCloser closer(request.UI.ProgressDialog);
            try {
                request.UI.ProgressDialog.WaitCreating();
                request.Result = request.Folder->ReOpen(request.Callback, &request.ResultFolder);
            } catch (...) {
                request.Result = E_FAIL;
            }
            return 0;
        }
    } request;
    request.Folder = &folder;
    request.UI.SettingsPath = settingsPath;
    request.UI.ParentWindow = parent;
    request.UI.PasswordIsDefined = usePassword;
    request.UI.Password = password;
    request.UI.ProgressDialog.MainWindow = parent;
    request.UI.ProgressDialog.MainTitle = L"7-Zip";
    request.UI.ProgressDialog.WaitMode = true;
    auto *callback = new COpenCallbackImp;
    request.Callback = callback;
    callback->Callback = &request.UI;
    auto transport = folder.CreateProgressTransport(request.UI.ProgressDialog);
    request.UI.ProgressDialog.AttachTransport(transport.get());
    auto *resource = dynamic_cast<QtArchiveFileResource *>(folder.GetEngineResource());
    if (resource)
        resource->SetUpdateProgress(&request.UI.ProgressDialog.Sync);
    const auto clearProgress = qScopeGuard([resource] {
        if (resource)
            resource->SetUpdateProgress(nullptr);
    });
    NWindows::CThread thread;
    const WRes started = thread.Create(CThreadArchiveReOpen::Run, &request);
    if (started != 0)
        return HRESULT_FROM_WIN32(started);
    request.UI.StartProgressDialog(LangString(IDS_OPENNING), thread);
    if (request.Result == S_OK)
        *resultFolder = request.ResultFolder.Detach();
    return request.Result;
}

void QtArchiveFileResource::AttachReopenService(ArchiveBackend &backend, const QString &format) {
    _reopen = [this, backend = QPointer<ArchiveBackend>(&backend),
               format](IFolderFolder **metadata, IArchiveOpenCallback *callback) -> HRESULT {
        return ReOpenArchive(backend, format, metadata, callback);
    };
}

HRESULT QtArchiveFileResource::ReOpenArchive(const QPointer<ArchiveBackend> &backend,
                                             const QString &format, IFolderFolder **metadata,
                                             IArchiveOpenCallback *callback) {
    COM_TRY_BEGIN
    if (!metadata)
        return E_INVALIDARG;
    *metadata = nullptr;
    if (QThread::currentThread() == QCoreApplication::instance()->thread())
        return E_FAIL;
    const QString path = EnginePath;
    auto *openCallback = dynamic_cast<COpenCallbackImp *>(callback);
    // Original COpenCallbackImp does not publish IProgress. Its archive
    // callback owns ReOpenCallback progress and crypto delegation instead.
    CNativeOpenProgress nativeProgress(nullptr, openCallback ? openCallback->Callback : nullptr,
                                       callback);
    std::shared_ptr<ArchiveNativeOpen> nativeOpen;
    COpenOptions options;
    CObjectVector<COpenType> types;
    CIntVector excluded;
    CMyComPtr<IInStream> inputCursor;
    if (_streamInput) {
        if (!_codecs || !_codecs->parseOpenTypes(format, types))
            return E_INVALIDARG;
        options.props = nullptr;
        RINOK(_streamInput->OpenCursor(&inputCursor))
        options.stream = inputCursor;
        options.types = &types;
        options.excludedFormats = &excluded;
    }
    RINOK(ArchiveBackend::openNativeArchive(_codecs, path, format, &nativeProgress, nativeOpen,
                                            _streamInput ? &options : nullptr))
    QString limitations;
    auto *ui =
        openCallback ? dynamic_cast<COpenArchiveCallback *>(openCallback->Callback) : nullptr;
    // CArchiveLink::ReOpen wraps the caller in a fresh COpenCallbackImp.
    // ReOpenCallback points to the caller for the joined operation's lifetime.
    if (!ui && openCallback) {
        const auto *outer = dynamic_cast<COpenCallbackImp *>(openCallback->ReOpenCallback);
        if (outer)
            ui = dynamic_cast<COpenArchiveCallback *>(outer->Callback);
    }
    if (nativeOpen) {
        RINOK(PrepareLimitedArchiveRead(nativeOpen->archiveLink(), nativeOpen->nativeCodecs(),
                                        ui ? &ui->ProgressDialog : nullptr, ui,
                                        FilesystemReadLimitations(), limitations))
    }
    if (nativeOpen && (UsesNativeMetadata() || !limitations.isEmpty())) {
        SetFilesystemReadLimitations(limitations);
        AttachNativeOpen(std::move(nativeOpen));
        UseNativeMetadata();
        return S_OK;
    }
    if (!backend)
        return E_FAIL;
    QString password;
    CMyComPtr<ICryptoGetTextPassword2> crypto;
    if (callback)
        callback->QueryInterface(IID_ICryptoGetTextPassword2, (void **)&crypto);
    if (!crypto) {
        if (auto *open = dynamic_cast<COpenCallbackImp *>(callback); open && open->ReOpenCallback)
            open->ReOpenCallback->QueryInterface(IID_ICryptoGetTextPassword2, (void **)&crypto);
    }
    if (crypto) {
        Int32 defined = 0;
        CMyComBSTR value;
        RINOK(crypto->CryptoGetTextPassword2(&defined, &value))
        if (defined)
            password = QString::fromWCharArray(value);
    } else if (auto *open = dynamic_cast<COpenCallbackImp *>(callback)) {
        if (auto *ui = dynamic_cast<COpenArchiveCallback *>(open->Callback);
            ui && ui->PasswordIsDefined)
            password = QString::fromWCharArray(ui->Password);
    }
    ArchiveInfo info;
    QList<ArchiveEntry> entries;
    RINOK(
        ReadArchiveBackendListing(*backend, path, format, password, callback, _sync, info, entries))
    RINOK(ConfirmLimitedArchiveRead(ui ? &ui->ProgressDialog : nullptr, ui,
                                    FilesystemReadLimitations(), info.readLimitations))
    RINOK(CreateArchiveMetadata(entries, metadata))
    Info = info;
    SetFilesystemReadLimitations(info.readLimitations.isEmpty() ? limitations
                                                                : info.readLimitations);
    AttachNativeOpen(std::move(nativeOpen));
    return S_OK;
    COM_TRY_END
}
