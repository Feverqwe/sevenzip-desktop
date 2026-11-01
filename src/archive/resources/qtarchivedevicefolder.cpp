#include "Common/ComTry.h"
#include "archive/callbacks/ArchiveOpenCallback.h"
#include "archive/resources/archivenativeopen.h"
#include "archive/resources/qtarchivefileresource.h"
#include "archive/resources/qtarchivenativeopenprogress.h"
#include "devices/deviceaccess.h"

bool QtArchiveFileResource::AllowsIncompleteDeviceRead() const {
    const auto *direct = dynamic_cast<DirectDeviceInput *>(_directInput.Interface());
    return direct && direct->allowsIncompleteDeviceRead();
}

HRESULT QtArchiveFileResource::RefreshDeviceInput(IOpenCallbackUI *callback, QString *error) {
    const auto *device = dynamic_cast<DirectDeviceInput *>(_directInput.Interface());
    if (!device)
        return E_INVALIDARG;
    CMyComPtr<IInStream> cursor;
    RINOK(CreateDevicePartitionInput(_directInput, 0, device->deviceInputSize(), &cursor))
    std::shared_ptr<ArchiveNativeOpen> opened;
    RINOK(
        ArchiveBackend::openDeviceFilesystem(_codecs, cursor, EnginePath, callback, opened, error))
    AttachNativeOpen(std::move(opened));
    _nativeReadOnly = true;
    Info.format = _nativeOpen->resolvedOpenType();
    Info.physicalSize = qint64(_nativeOpen->arc().FileSize);
    Info.offset = 0;
    return S_OK;
}

HRESULT QtArchiveFileResource::OpenDeviceInput(ArchiveBackend &backend, IInStream *input,
                                               IOpenCallbackUI *callback, QString *error) {
    COM_TRY_BEGIN
    if (!dynamic_cast<DirectDeviceInput *>(input) || _directInput)
        return E_INVALIDARG;
    _directInput = input;
    const HRESULT result = RefreshDeviceInput(callback, error);
    if (result != S_OK) {
        _directInput.Release();
        return result;
    }
    AttachExtractService(backend, TransportFormat());
    _reopen = [this](IFolderFolder **metadata, IArchiveOpenCallback *callback) -> HRESULT {
        COM_TRY_BEGIN
        if (!metadata)
            return E_INVALIDARG;
        *metadata = nullptr;
        auto *open = dynamic_cast<COpenCallbackImp *>(callback);
        CMyComPtr<IProgress> progress;
        if (callback)
            callback->QueryInterface(IID_IProgress, (void **)&progress);
        CNativeOpenProgress nativeProgress(progress, open ? open->Callback : nullptr);
        RINOK(CloseNativeOpen())
        return RefreshDeviceInput(&nativeProgress);
        COM_TRY_END
    };
    return S_OK;
    COM_TRY_END
}
