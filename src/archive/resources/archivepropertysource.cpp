#include "archive/resources/archivepropertysource.h"
#include "7zip/PropID.h"
#include "Windows/PropVariant.h"
#include "archive/resources/archivenativeopen.h"
#include "archive/resources/qtarchivefileresource.h"

ArchivePropertySource::ArchivePropertySource(const QtArchiveFileResource *resource) {
    if (!resource)
        return;
    if (auto reader = resource->NativeReaderLease())
        _source = std::weak_ptr<ArchiveNativeOpen>(reader);
    else
        _source = FileMetadata{resource->Info, !resource->FilesystemReadLimitations().isEmpty()};
}

HRESULT ArchivePropertySource::AcquireReader(std::shared_ptr<ArchiveNativeOpen> &reader) const {
    reader.reset();
    if (const auto *native = std::get_if<std::weak_ptr<ArchiveNativeOpen>>(&_source)) {
        reader = native->lock();
        if (!reader)
            return E_FAIL;
    }
    return S_OK;
}

QString ArchivePropertySource::Format() const {
    const auto *file = std::get_if<FileMetadata>(&_source);
    return file ? file->Info.format : QString();
}

HRESULT ArchivePropertySource::GetProperty(PROPID id, PROPVARIANT *value) const {
    std::shared_ptr<ArchiveNativeOpen> reader;
    RINOK(AcquireReader(reader))
    if (reader)
        return reader->arc().Archive->GetArchiveProperty(id, value);
    NWindows::NCOM::CPropVariant prop;
    if (const auto *file = std::get_if<FileMetadata>(&_source)) {
        const auto &info = file->Info;
        if (id == kpidReadOnly)
            prop = file->ReadOnly;
        else if (id == kpidPhySize && info.physicalSize >= 0)
            prop = UInt64(info.physicalSize);
        else if (id == kpidOffset)
            prop.Set_Int64(info.offset);
        else if (id >= 0x10000 && id - 0x10000 < unsigned(info.properties.size())) {
            auto i = info.properties.cbegin();
            std::advance(i, id - 0x10000);
            prop = i.value().toStdWString().c_str();
        }
    }
    return prop.Detach(value);
}

HRESULT ArchivePropertySource::GetCount(UInt32 *count) const {
    std::shared_ptr<ArchiveNativeOpen> reader;
    RINOK(AcquireReader(reader))
    if (reader)
        return reader->arc().Archive->GetNumberOfArchiveProperties(count);
    const auto *file = std::get_if<FileMetadata>(&_source);
    *count = file ? UInt32(file->Info.properties.size()) : 0;
    return S_OK;
}

HRESULT ArchivePropertySource::GetInfo(UInt32 index, BSTR *name, PROPID *id, VARTYPE *type) const {
    std::shared_ptr<ArchiveNativeOpen> reader;
    RINOK(AcquireReader(reader))
    if (reader)
        return reader->arc().Archive->GetArchivePropertyInfo(index, name, id, type);
    const auto *file = std::get_if<FileMetadata>(&_source);
    if (!file || index >= unsigned(file->Info.properties.size()))
        return E_INVALIDARG;
    auto i = file->Info.properties.cbegin();
    std::advance(i, index);
    const std::wstring key = i.key().toStdWString();
    *name = ::SysAllocStringLen(key.c_str(), unsigned(key.size()));
    *id = 0x10000 + index;
    *type = VT_BSTR;
    return *name ? S_OK : E_OUTOFMEMORY;
}
