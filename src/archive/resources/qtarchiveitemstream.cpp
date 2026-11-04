#include "archive/resources/qtarchiveitemstream.h"
#include "Common/ComTry.h"
#include "Common/MyCom.h"
#include "archive/opening/archivedirectoryaccess.h"
#include "archive/resources/archivelayercontext.h"
#include "archive/resources/archivenativeopen.h"
#include "archive/resources/archivereadstream.h"
#include "archive/resources/qtarchivefileresource.h"

namespace {
class CArchiveItemStream final : public IInStream,
                                 public IArchiveLayerSource,
                                 public IArchiveReadStream,
                                 public CMyUnknownImp {
    Z7_COM_QI_BEGIN2(ISequentialInStream)
    else if (_seekable && iid == IID_IInStream) {
        *outObject = static_cast<IInStream *>(this);
    }
    Z7_COM_QI_ENTRY(IArchiveLayerSource)
    Z7_COM_QI_ENTRY(IArchiveReadStream)
    Z7_COM_QI_END
    Z7_COM_ADDREF_RELEASE
    // Native interfaces release before the module/catalog/input owner.
    CMyComPtr<IUnknown> _resource;
    std::shared_ptr<ArchiveNativeOpen> _native;
    CMyComPtr<ISequentialInStream> _stream;
    CMyComPtr<IInStream> _seekable;
    std::mutex _mutex;
    UInt64 _position = 0;
    const UInt64 _containingDepth;

  public:
    CArchiveItemStream(IUnknown *resource, std::shared_ptr<ArchiveNativeOpen> native,
                       ISequentialInStream *stream, UInt64 depth)
        : _resource(resource), _native(std::move(native)), _stream(stream),
          _containingDepth(depth) {
        _stream.QueryInterface(IID_IInStream, &_seekable);
    }
    Z7_COM7F_IMF(GetContainingContainerDepth(UInt64 *depth)) override {
        if (!depth)
            return E_INVALIDARG;
        *depth = _containingDepth;
        return S_OK;
    }
    Z7_COM7F_IMF(Read(void *data, UInt32 size, UInt32 *processed)) override {
        return ReadWithRequest(data, size, processed, nullptr);
    }
    Z7_COM7F_IMF(ReadWithRequest(void *data, UInt32 size, UInt32 *processed, IProgress *request))
    override {
        COM_TRY_BEGIN
        std::lock_guard<std::mutex> cursor(_mutex);
        if (processed)
            *processed = 0;
        if (request) {
            RINOK(request->SetCompleted(nullptr))
        }
        if (!size)
            return S_OK;
        if (!data)
            return E_INVALIDARG;
        ArchiveNativeReadScope scope(_native, request);
        if (_seekable) {
            RINOK(_seekable->Seek(Int64(_position), STREAM_SEEK_SET, nullptr))
        }
        UInt32 actual = 0;
        const HRESULT result = _stream->Read(data, size, &actual);
        _position += actual;
        if (processed)
            *processed = actual;
        return result;
        COM_TRY_END
    }
    Z7_COM7F_IMF(Seek(Int64 offset, UInt32 origin, UInt64 *position)) override {
        return SeekWithRequest(offset, origin, position, nullptr);
    }
    Z7_COM7F_IMF(SeekWithRequest(Int64 offset, UInt32 origin, UInt64 *position, IProgress *request))
    override {
        COM_TRY_BEGIN
        std::lock_guard<std::mutex> cursor(_mutex);
        if (request) {
            RINOK(request->SetCompleted(nullptr))
        }
        if (!_seekable)
            return E_NOTIMPL;
        ArchiveNativeReadScope scope(_native, request);
        if (origin == STREAM_SEEK_CUR) {
            RINOK(_seekable->Seek(Int64(_position), STREAM_SEEK_SET, nullptr))
        }
        UInt64 next;
        RINOK(_seekable->Seek(offset, origin, &next))
        _position = next;
        if (position)
            *position = next;
        return S_OK;
        COM_TRY_END
    }
};
} // namespace

HRESULT CreateArchiveInputStream(IUnknown *resource, std::shared_ptr<ArchiveNativeOpen> reader,
                                 UInt64 depth, IInStream **stream) {
    COM_TRY_BEGIN
    if (!stream || !reader)
        return E_INVALIDARG;
    *stream = nullptr;
    CMyComPtr<IArchiveDirectoryAccess> directories;
    RINOK(reader->arc().Archive.QueryInterface(IID_IArchiveDirectoryAccess, (void **)&directories))
    CMyComPtr<IInStream> input;
    {
        ArchiveNativeReadScope scope(reader, nullptr);
        RINOK(directories->GetDirectoryInputStream(&input))
    }
    if (!input)
        return E_FAIL;
    CMyComPtr<IInStream> retained = new CArchiveItemStream(resource, reader, input, depth);
    *stream = retained.Detach();
    return S_OK;
    COM_TRY_END
}

HRESULT CreateArchiveItemStream(IUnknown *resource, UInt32 index, ISequentialInStream **stream,
                                IProgress *request) {
    COM_TRY_BEGIN
    if (!stream)
        return E_INVALIDARG;
    *stream = nullptr;
    auto *owner = dynamic_cast<QtArchiveFileResource *>(resource);
    if (!owner || !owner->NativeItemProperties())
        return S_OK;
    return CreateArchiveItemStream(resource, owner->NativeReaderLease(), owner->ContainerDepth(),
                                   index, stream, request);
    COM_TRY_END
}

HRESULT CreateArchiveItemStream(IUnknown *resource, std::shared_ptr<ArchiveNativeOpen> lease,
                                UInt64 depth, UInt32 index, ISequentialInStream **stream,
                                IProgress *request) {
    COM_TRY_BEGIN
    if (!stream)
        return E_INVALIDARG;
    *stream = nullptr;
    IInArchive *native = lease ? lease->arc().Archive.Interface() : nullptr;
    CMyComPtr<IInArchiveGetStream> streams;
    if (!native || native->QueryInterface(IID_IInArchiveGetStream, (void **)&streams) != S_OK ||
        !streams)
        return S_OK;
    ArchiveNativeReadScope scope(lease, request);
    if (request) {
        RINOK(request->SetCompleted(nullptr))
    }
    CMyComPtr<ISequentialInStream> source;
    const HRESULT result = streams->GetStream(index, &source);
    if (source) {
        CMyComPtr<ISequentialInStream> retained =
            new CArchiveItemStream(resource, std::move(lease), source, depth);
        *stream = retained.Detach();
    }
    return result;
    COM_TRY_END
}
