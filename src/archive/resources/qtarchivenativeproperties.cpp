#include "7zip/UI/FileManager/IFolder.h"
#include "Windows/PropVariant.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/resources/archivenativeopen.h"
#include "archive/resources/qtarchivefileresource.h"
#include <QSet>

void QtArchiveFileResource::UseNativeMetadata() {
    _nativeMetadata = true;
    if (_nativeOpen) {
        if (const auto *format = _nativeOpen->formatInfo())
            Info.format = QString::fromWCharArray(format->Name.Ptr());
        Info.physicalSize = qint64(_nativeOpen->arc().FileSize);
        Info.offset = qint64(_nativeOpen->arc().GetGlobalOffset());
    }
}

std::shared_ptr<ArchiveNativeOpen>
QtArchiveFileResource::MatchNativeItemOrder(IFolderFolder *metadata) const {
    if (!_nativeOpen)
        return {};
    if ((HasDirectInput() || UsesNativeMetadata()) && !metadata) {
        // Direct metadata comes from this exact retained handler, including
        // duplicate names; there is no independent metadata index mapping.
        return _nativeOpen;
    }
    if (!metadata)
        return {};
    IInArchive *native = _nativeOpen->arc().Archive;
    UInt32 nativeCount = 0, count = 0;
    if (native->GetNumberOfItems(&nativeCount) != S_OK ||
        metadata->GetNumberOfItems(&count) != S_OK || nativeCount != count)
        return {};
    // Independent metadata must prove its index order before using this handler.
    QSet<QString> paths;
    for (UInt32 i = 0; i < count; ++i) {
        UString original;
        NWindows::NCOM::CPropVariant presented;
        if (_nativeOpen->arc().GetItem_Path(i, original) != S_OK ||
            metadata->GetProperty(i, kpidPath, &presented) != S_OK || presented.vt != VT_BSTR ||
            QString::fromWCharArray(original.Ptr()) != QString::fromWCharArray(presented.bstrVal))
            return {};
        const QString path = QString::fromWCharArray(original.Ptr());
        // Repeated names cannot establish which physical native index the
        // independent metadata row describes. Refuse an ambiguous mapping.
        if (paths.contains(path))
            return {};
        paths.insert(path);
    }
    return _nativeOpen;
}

IInArchive *QtArchiveFileResource::NativeItemProperties() const {
    return _nativeOpen ? _nativeOpen->arc().Archive.Interface() : nullptr;
}

bool QtArchiveFileResource::HasNativeUpdateService() const {
    CMyComPtr<IOutArchive> writer;
    auto *native = NativeItemProperties();
    return native && native->QueryInterface(IID_IOutArchive, (void **)&writer) == S_OK && writer;
}

bool QtArchiveFileResource::HasNativePropertyService() const {
    CMyComPtr<ISetProperties> properties;
    auto *native = NativeItemProperties();
    return HasUpdateService() && _setProperties && native &&
           native->QueryInterface(IID_ISetProperties, (void **)&properties) == S_OK && properties;
}

HRESULT QtArchiveFileResource::SetNativeProperties(const wchar_t *const *names,
                                                   const PROPVARIANT *values, UInt32 count) {
    if (!HasNativePropertyService())
        return E_NOINTERFACE;
    RINOK(_setProperties(names, values, count))
    // Explicit source properties take precedence over the optional Qt CInfo
    // projection. The source DoOperation's empty call resets that choice.
    _nativePropertiesExplicit = count != 0;
    return S_OK;
}

bool QtArchiveFileResource::HasCommentService() const {
    return NativeItemProperties() && _nativeOpen->hasCommentWriter() && HasUpdateService();
}

bool QtArchiveFileResource::HasNativeExtractService() const {
    return NativeItemProperties() != nullptr;
}

bool QtArchiveFileResource::HasNativeItemStreamService() const {
    CMyComPtr<IInArchiveGetStream> streams;
    auto *native = NativeItemProperties();
    return native && native->QueryInterface(IID_IInArchiveGetStream, (void **)&streams) == S_OK &&
           streams;
}

bool QtArchiveFileResource::HasNativeRarReader() const {
    return _nativeOpen && _nativeOpen->hasNativeRarExtractor();
}

bool QtArchiveFileResource::HasNativeHashReader() const {
    return _nativeOpen && _nativeOpen->hasNativeHashExtractor();
}

bool QtArchiveFileResource::HasExtractService() const { return bool(_extract); }
