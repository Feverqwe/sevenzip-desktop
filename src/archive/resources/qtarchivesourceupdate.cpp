#include "archive/backend/archivebackend.h"
#include "archive/callbacks/UpdateCallback.h"
#include "archive/resources/archivenativeopen.h"
#include "archive/resources/qtarchivefileresource.h"
#include "ui/progress/ProgressDialog2.h"
#include "ui/progress/archiveprogresstransport.h"
#include <QPointer>

namespace {
class CResourceProgress final : public IQtProgressTransport {
    CMyComPtr2<IUnknown, QtArchiveFileResource> _resource;
    ArchiveProgressTransport _transport;

  public:
    CResourceProgress(QtArchiveFileResource *resource, CProgressDialog &dialog,
                      ArchiveBackend &backend, bool nativeWorker)
        : _transport(dialog, backend, nativeWorker) {
        _resource.SetFromCls(resource);
        _resource->SetUpdateProgress(&dialog.Sync);
    }
    ~CResourceProgress() override { _resource->SetUpdateProgress(nullptr); }
    HRESULT SetPaused(bool paused) override { return _transport.SetPaused(paused); }
    void Cancel() override { _transport.Cancel(); }
};
} // namespace

void QtArchiveFileResource::AttachProgressService(ArchiveBackend &backend) {
    _progress = [this, backend = QPointer<ArchiveBackend>(&backend)](
                    CProgressDialog &dialog,
                    bool nativeWorker) -> std::unique_ptr<IQtProgressTransport> {
        return backend ? std::make_unique<CResourceProgress>(this, dialog, *backend, nativeWorker)
                       : nullptr;
    };
}

void QtArchiveFileResource::AttachUpdateService(ArchiveBackend &backend, const QString &format) {
    AttachReopenService(backend, format);
    AttachProgressService(backend);
    _updateBackend = &backend;
}

void QtArchiveFileResource::RebindServices(ArchiveBackend &backend) {
    // Called only for a quiescent live tab. Keep the input and native reader,
    // and retarget the mutable services before the old window can disappear.
    const QString format = TransportFormat();
    if (_reopen && !_directInput)
        AttachReopenService(backend, format);
    if (_extract)
        AttachExtractService(backend, format);
    if (_progress)
        AttachProgressService(backend);
    if (_updateBackend)
        _updateBackend = &backend;
}

HRESULT QtArchiveFileResource::UpdateNative(ISequentialOutStream *output, UInt32 count,
                                            IArchiveUpdateCallback *callback) {
    const auto backend = *_updateBackend;
    if (!backend)
        return E_FAIL;
    _outputMTime.reset();
    auto *native = NativeItemProperties();
    if (!native)
        return E_NOTIMPL;
    CMyComPtr<IOutArchive> writer;
    // AgentOut uses the bound archive's original IOutArchive interface.
    // A missing writer cannot be replaced by a console reconstruction.
    RINOK(native->QueryInterface(IID_IOutArchive, (void **)&writer))
    if (const auto *source = dynamic_cast<CArchiveUpdateCallback *>(callback);
        source && source->CommentIndex >= 0 && source->Comment) {
        if (!HasCommentService())
            return E_NOTIMPL;
        return backend->updateNativeComment(_nativeOpen, output, count, callback);
    }
    if (_compressionOptions && !_nativePropertiesExplicit)
        return backend->updateNativeArchive(_nativeOpen, output, count, callback,
                                            *_compressionOptions);
    return backend->updateNativeArchive(_nativeOpen, output, count, callback);
}
