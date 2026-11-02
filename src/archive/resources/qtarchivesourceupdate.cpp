#include "archive/backend/archivebackend.h"
#include "archive/callbacks/UpdateCallback.h"
#include "archive/resources/archivenativeopen.h"
#include "archive/resources/qtarchivefileresource.h"
#include "ui/progress/ProgressDialog2.h"
#include "ui/progress/archiveprogresstransport.h"
#include <QPointer>

namespace {
class CResourceProgress final : public IQtProgressTransport {
    CMyComPtr<IUnknown> _resource;
    QtArchiveFileResource *_resourceSpec;
    ArchiveProgressTransport _transport;

  public:
    CResourceProgress(QtArchiveFileResource *resource, CProgressDialog &dialog,
                      ArchiveBackend &backend, bool nativeWorker)
        : _resource(resource), _resourceSpec(resource), _transport(dialog, backend, nativeWorker) {
        resource->SetUpdateProgress(&dialog.Sync);
    }
    ~CResourceProgress() override { _resourceSpec->SetUpdateProgress(nullptr); }
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
    _setProperties = [this, backend = QPointer<ArchiveBackend>(&backend)](
                         const wchar_t *const *names, const PROPVARIANT *values, UInt32 count) {
        return backend ? backend->setNativeArchiveProperties(_nativeOpen, names, values, count)
                       : E_FAIL;
    };
    _update = [this, backend = QPointer<ArchiveBackend>(&backend)](
                  ISequentialOutStream *output, IInArchive *, UInt32 count,
                  IArchiveUpdateCallback *callback) -> HRESULT {
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
    };
}
