#include "archive/resources/qtarchiveproperties.h"
#include "Common/MyCom.h"
#include "Windows/PropVariant.h"
#include "archive/opening/OpenArchive.h"
#include "archive/resources/archivenativeopen.h"
#include "archive/resources/archivereadstream.h"
#include "archive/resources/qtarchivefileresource.h"
#include "archive/resources/qtarchiveitemstream.h"

bool ArchiveUpdateEnabled(IInArchive *archive) {
    if (!archive)
        return false;
    CMyComPtr<IOutArchive> update;
    archive->QueryInterface(IID_IOutArchive, reinterpret_cast<void **>(&update));
    return bool(update);
}

namespace {
class CFolderArchiveProperties final : public IInArchive,
                                       public IInArchiveGetStream,
                                       public IArchiveGetStreamWithRequest,
                                       public IOutArchive,
                                       public ISetProperties,
                                       public IArchiveGetRawProps,
                                       public CMyUnknownImp {
    Z7_COM_QI_BEGIN2(IInArchive)
    else if (NativeArchive() && _resourceSpec->HasNativeItemStreamService() &&
             iid == IID_IInArchiveGetStream) {
        *outObject = static_cast<IInArchiveGetStream *>(this);
    }
    else if (NativeArchive() && _resourceSpec->HasNativeItemStreamService() &&
             iid == IID_IArchiveGetStreamWithRequest) {
        *outObject = static_cast<IArchiveGetStreamWithRequest *>(this);
    }
    else if (RawProperties() && iid == IID_IArchiveGetRawProps) {
        *outObject = static_cast<IArchiveGetRawProps *>(this);
    }
    else if (CanUpdate() && iid == IID_IOutArchive) {
        *outObject = static_cast<IOutArchive *>(this);
    }
    else if (NativeArchive() && _resourceSpec->HasNativePropertyService() &&
             iid == IID_ISetProperties) {
        *outObject = static_cast<ISetProperties *>(this);
    }
    Z7_COM_QI_END
    Z7_COM_ADDREF_RELEASE
    // Native interfaces must release before their module/catalog lease.
    CMyComPtr<IUnknown> _resource;
    CMyComPtr<IFolderFolder> _properties;
    CMyComPtr<IArchiveGetRawProps> _rawProps;
    QtArchiveFileResource *_resourceSpec;
    std::weak_ptr<ArchiveNativeOpen> _native;
    std::shared_ptr<ArchiveNativeOpen> NativeReader() const {
        auto reader = _native.lock();
        return _resourceSpec && reader == _resourceSpec->NativeReaderLease() ? reader : nullptr;
    }
    IInArchive *NativeArchive() const {
        const auto reader = NativeReader();
        return reader ? reader->arc().Archive.Interface() : nullptr;
    }
    IArchiveGetRawProps *RawProperties() const {
        return _properties || NativeArchive() ? _rawProps.Interface() : nullptr;
    }
    bool CanUpdate() const {
        return _resourceSpec && _resourceSpec->HasUpdateService() &&
               (!_resourceSpec->NativeItemProperties() || NativeArchive());
    }
    void BindRawProperties() {
        _native.reset();
        _rawProps.Release();
        if (_properties)
            _properties.QueryInterface(IID_IArchiveGetRawProps, &_rawProps);
        if (_resourceSpec) {
            _native = _resourceSpec->MatchNativeItemOrder(_properties);
            if (auto *native = NativeArchive()) {
                CMyComPtr<IArchiveGetRawProps> raw;
                native->QueryInterface(IID_IArchiveGetRawProps, (void **)&raw);
                if (raw)
                    _rawProps = raw;
            }
        }
    }

  public:
    bool UsesNativeItemIndexes() const { return NativeArchive() != nullptr; }
    bool HasExtractService() const {
        return _resourceSpec && _resourceSpec->HasExtractService() &&
               ((!_resourceSpec->HasNativeRarReader() && !_resourceSpec->HasNativeHashReader()) ||
                UsesNativeItemIndexes());
    }
    CFolderArchiveProperties(IFolderFolder *properties, IUnknown *resource)
        : _resource(resource), _properties(properties),
          _resourceSpec(dynamic_cast<QtArchiveFileResource *>(resource)) {
        BindRawProperties();
    }
    Z7_IFACE_COM7_IMP(IInArchive)
    Z7_IFACE_COM7_IMP(IInArchiveGetStream)
    Z7_COM7F_IMP(GetStreamWithRequest(UInt32 index, ISequentialInStream **stream,
                                      IProgress *request))
    Z7_IFACE_COM7_IMP(IOutArchive)
    Z7_IFACE_COM7_IMP(ISetProperties)
    Z7_IFACE_COM7_IMP(IArchiveGetRawProps)
};

Z7_COM7F_IMF(CFolderArchiveProperties::GetStream(UInt32 index, ISequentialInStream **stream)) {
    return GetStreamWithRequest(index, stream, nullptr);
}

Z7_COM7F_IMF(CFolderArchiveProperties::GetStreamWithRequest(UInt32 index,
                                                            ISequentialInStream **stream,
                                                            IProgress *request)) {
    if (!stream)
        return E_INVALIDARG;
    *stream = nullptr;
    auto reader = NativeReader();
    return reader ? CreateArchiveItemStream(_resource, std::move(reader),
                                            _resourceSpec->ContainerDepth(), index, stream, request)
                  : E_NOINTERFACE;
}

Z7_COM7F_IMF(CFolderArchiveProperties::SetProperties(const wchar_t *const *names,
                                                     const PROPVARIANT *values, UInt32 count)) {
    return NativeArchive() ? _resourceSpec->SetNativeProperties(names, values, count)
                           : E_NOINTERFACE;
}

Z7_COM7F_IMF(CFolderArchiveProperties::GetParent(UInt32 index, UInt32 *parent, UInt32 *type)) {
    auto *raw = RawProperties();
    return raw ? raw->GetParent(index, parent, type) : E_NOTIMPL;
}
Z7_COM7F_IMF(CFolderArchiveProperties::GetNumRawProps(UInt32 *count)) {
    auto *raw = RawProperties();
    return raw ? raw->GetNumRawProps(count) : E_NOTIMPL;
}
Z7_COM7F_IMF(CFolderArchiveProperties::GetRawPropInfo(UInt32 index, BSTR *name, PROPID *id)) {
    auto *raw = RawProperties();
    return raw ? raw->GetRawPropInfo(index, name, id) : E_NOTIMPL;
}
Z7_COM7F_IMF(CFolderArchiveProperties::GetRawProp(UInt32 index, PROPID id, const void **data,
                                                  UInt32 *size, UInt32 *type)) {
    auto *raw = RawProperties();
    return raw ? raw->GetRawProp(index, id, data, size, type) : E_NOTIMPL;
}

Z7_COM7F_IMF(CFolderArchiveProperties::GetFileTimeType(UInt32 *type)) {
    if (!type)
        return E_INVALIDARG;
    if (auto *native = NativeArchive()) {
        CMyComPtr<IOutArchive> writer;
        if (native->QueryInterface(IID_IOutArchive, (void **)&writer) == S_OK)
            return writer->GetFileTimeType(type);
    }
    *type = NFileTimeType::kWindows;
    return S_OK;
}
Z7_COM7F_IMF(CFolderArchiveProperties::UpdateItems(ISequentialOutStream *output, UInt32 count,
                                                   IArchiveUpdateCallback *callback)) {
    return CanUpdate() ? _resourceSpec->Update(output, this, count, callback) : E_NOINTERFACE;
}

Z7_COM7F_IMF(CFolderArchiveProperties::Open(IInStream *, const UInt64 *,
                                            IArchiveOpenCallback *callback)) {
    if (!_resourceSpec)
        return E_NOTIMPL;
    CMyComPtr<IFolderFolder> metadata;
    RINOK(_resourceSpec->ReOpen(&metadata, callback))
    _properties = metadata;
    BindRawProperties();
    return S_OK;
}
Z7_COM7F_IMF(CFolderArchiveProperties::Close()) {
    if (!_resourceSpec || (_resourceSpec->NativeReaderLease() && !NativeReader()))
        return S_OK;
    _native.reset();
    return _resourceSpec->CloseNativeOpen();
}
Z7_COM7F_IMF(CFolderArchiveProperties::GetNumberOfItems(UInt32 *count)) {
    if (auto *native = NativeArchive())
        return native->GetNumberOfItems(count);
    if (!_properties)
        return E_FAIL;
    return _properties->GetNumberOfItems(count);
}
Z7_COM7F_IMF(CFolderArchiveProperties::GetProperty(UInt32 index, PROPID id, PROPVARIANT *value)) {
    if (auto *native = NativeArchive()) {
        // IInArchive permits an omitted IsDir property (gzip is a file).
        // The folder/process-plan boundary needs its defined bool. Reuse
        // the original archive property reader's default/type rules.
        if (id == kpidIsDir) {
            bool isDir;
            RINOK(Archive_IsItem_Dir(native, index, isDir))
            NWindows::NCOM::CPropVariant prop(isDir);
            return prop.Detach(value);
        }
        return native->GetProperty(index, id, value);
    }
    return _properties ? _properties->GetProperty(index, id, value) : E_FAIL;
}
Z7_COM7F_IMF(CFolderArchiveProperties::Extract(const UInt32 *indices, UInt32 count, Int32 testMode,
                                               IArchiveExtractCallback *callback)) {
    return _resourceSpec ? _resourceSpec->Extract(this, UsesNativeItemIndexes(), indices, count,
                                                  testMode, callback)
                         : E_NOTIMPL;
}
Z7_COM7F_IMF(CFolderArchiveProperties::GetArchiveProperty(PROPID id, PROPVARIANT *value)) {
    if (_resourceSpec)
        return _resourceSpec->GetArchiveProperty(id, value);
    NWindows::NCOM::CPropVariant prop;
    return prop.Detach(value);
}
Z7_COM7F_IMF(CFolderArchiveProperties::GetNumberOfProperties(UInt32 *count)) {
    if (!_properties && _resourceSpec)
        if (auto *native = NativeArchive())
            return native->GetNumberOfProperties(count);
    if (!_properties)
        return E_FAIL;
    return _properties->GetNumberOfProperties(count);
}
Z7_COM7F_IMF(CFolderArchiveProperties::GetPropertyInfo(UInt32 index, BSTR *name, PROPID *id,
                                                       VARTYPE *type)) {
    if (!_properties && _resourceSpec)
        if (auto *native = NativeArchive())
            return native->GetPropertyInfo(index, name, id, type);
    if (!_properties)
        return E_FAIL;
    return _properties->GetPropertyInfo(index, name, id, type);
}
Z7_COM7F_IMF(CFolderArchiveProperties::GetNumberOfArchiveProperties(UInt32 *count)) {
    if (_resourceSpec)
        return _resourceSpec->GetNumberOfArchiveProperties(count);
    *count = 0;
    return S_OK;
}
Z7_COM7F_IMF(CFolderArchiveProperties::GetArchivePropertyInfo(UInt32 index, BSTR *name, PROPID *id,
                                                              VARTYPE *type)) {
    return _resourceSpec ? _resourceSpec->GetArchivePropertyInfo(index, name, id, type)
                         : E_INVALIDARG;
}
} // namespace

bool ArchiveUsesNativeItemIndexes(IInArchive *archive) {
    const auto *view = dynamic_cast<CFolderArchiveProperties *>(archive);
    return view && view->UsesNativeItemIndexes();
}

bool ArchiveHasExtractService(IInArchive *archive) {
    const auto *view = dynamic_cast<CFolderArchiveProperties *>(archive);
    return view && view->HasExtractService();
}

HRESULT CreateArchivePropertyTransport(IFolderFolder *properties, IInArchive **archive,
                                       IUnknown *engineResource) {
    const auto *resource = dynamic_cast<QtArchiveFileResource *>(engineResource);
    if ((!properties && (!resource || !resource->MatchNativeItemOrder(nullptr))) || !archive)
        return E_INVALIDARG;
    *archive = nullptr;
    if (!engineResource) {
        const HRESULT result = properties->QueryInterface(IID_IInArchive, (void **)archive);
        if (result != E_NOINTERFACE)
            return result;
    }
    CMyComPtr<IInArchive> adapter = new CFolderArchiveProperties(properties, engineResource);
    *archive = adapter.Detach();
    return S_OK;
}

HRESULT CreateArchiveLinkTransport(CArchiveLink &link, IFolderFolder *properties,
                                   IUnknown *engineResource, const CCodecs *codecs) {
    const auto *resource = dynamic_cast<QtArchiveFileResource *>(engineResource);
    if (!properties && (!resource || !resource->MatchNativeItemOrder(nullptr)))
        return E_INVALIDARG;
    if (!link.Arcs.IsEmpty())
        return E_FAIL;
    CArc &arc = link.Arcs.AddNew();
    const HRESULT result = CreateArchivePropertyTransport(properties, &arc.Archive, engineResource);
    if (result != S_OK) {
        link.Release();
        return result;
    }
    arc.DefaultName = L"[Content]";
    arc.FormatIndex = -1;
    arc.ArcStreamOffset = 0;
    arc.Offset = 0;
    arc.FileSize = 0;
    arc.AvailPhySize = 0;
    arc.PhySize = 0;
    arc.PhySize_Defined = false;
    arc.Archive.QueryInterface(IID_IArchiveGetRawProps, &arc.GetRawProps);
    if (auto *resource = dynamic_cast<QtArchiveFileResource *>(engineResource)) {
        RINOK(resource->ConfigureArc(arc, codecs))
        resource->CopyNativeVolumeMetadata(link);
    }
    link.IsOpen = true;
    return S_OK;
}
