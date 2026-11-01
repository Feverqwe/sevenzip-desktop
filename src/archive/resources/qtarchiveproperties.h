// Shared native archive property view over the folder's engine metadata owner.
#pragma once
#include "7zip/Archive/IArchive.h"
#include "7zip/UI/FileManager/IFolder.h"
#include <QString>
#include <memory>

HRESULT CreateArchivePropertyTransport(IFolderFolder *properties, IInArchive **archive,
                                       IUnknown *engineResource = nullptr);
struct CArchiveLink;
class CCodecs;
class ArchiveNativeOpen;
struct ArchivePropertyContext {
    std::shared_ptr<ArchiveNativeOpen> Reader;
    const CArchiveLink *Link;
    const CCodecs *Codecs;
    QString Format;
    HRESULT Result;
    ArchivePropertyContext(IInArchive *archive, const CArchiveLink &fallback,
                           const CCodecs *codecs);
};
HRESULT CreateArchiveLinkTransport(CArchiveLink &link, IFolderFolder *properties,
                                   IUnknown *engineResource, const CCodecs *codecs = nullptr);

// Portable transport counterpart of a codec FormatInfo.UpdateEnabled lookup.
// The metadata owner exposes IOutArchive only when that engine service exists.
bool ArchiveUpdateEnabled(IInArchive *archive);
// The view's indexes are valid only for the reader generation it was bound to.
bool ArchiveUsesNativeItemIndexes(IInArchive *archive);
bool ArchiveHasExtractService(IInArchive *archive);
