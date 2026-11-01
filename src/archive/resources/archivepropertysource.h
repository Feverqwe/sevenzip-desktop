#pragma once

#include "7zip/Archive/IArchive.h"
#include "archive/backend/archivebackend.h"
#include <memory>
#include <variant>

class ArchiveNativeOpen;
class QtArchiveFileResource;

// A view's physical metadata source; retaining it does not retain open input handles.
class ArchivePropertySource {
    struct FileMetadata {
        ArchiveInfo Info;
        bool ReadOnly;
    };
    std::variant<std::monostate, FileMetadata, std::weak_ptr<ArchiveNativeOpen>> _source;

  public:
    explicit ArchivePropertySource(const QtArchiveFileResource *resource = nullptr);
    HRESULT AcquireReader(std::shared_ptr<ArchiveNativeOpen> &reader) const;
    QString Format() const;
    HRESULT GetProperty(PROPID id, PROPVARIANT *value) const;
    HRESULT GetCount(UInt32 *count) const;
    HRESULT GetInfo(UInt32 index, BSTR *name, PROPID *id, VARTYPE *type) const;
};
