#include "filemanager/panels/qtpanelfolder.h"
#include "7zip/Archive/Common/ItemNameUtils.h"
#include "7zip/PropID.h"
#include "Windows/PropVariant.h"

namespace {
constexpr PROPID kSnapshotProperty = 0x10000;
const PROPID kArchiveProperties[] = {kpidPath, kpidSize, kpidPackSize};

} // namespace

QtPanelFolder::QtPanelFolder(const QString &prefix, const CObjectVector<CPanelItem> &items,
                             PropertyCatalog catalog)
    : prefix_(prefix), items_(items), catalog_(catalog) {
    if (catalog_ == PropertyCatalog::ArchiveTransport) {
        bool hasMTime = false, hasCrc = false;
        FOR_VECTOR(i, items_) {
            if (items_[i].MTime && items_[i].MTime->vt == VT_FILETIME)
                hasMTime = true;
            hasCrc |= items_[i].Crc.has_value();
            for (auto p = items_[i].RawProperties.cbegin(); p != items_[i].RawProperties.cend();
                 ++p)
                if (rawPropertyIds_.FindInSorted(p.key()) < 0)
                    rawPropertyIds_.AddToUniqueSorted(p.key());
        }
        for (const PROPID id : kArchiveProperties)
            archivePropertyIds_.Add(id);
        if (hasMTime)
            archivePropertyIds_.Add(kpidMTime);
        if (hasCrc)
            archivePropertyIds_.Add(kpidCRC);
    }
    // Names are already localized/formatted by the Qt property presentation.
    // Describe their union once; missing values use the source VT_EMPTY contract.
    FOR_VECTOR(i, items_) {
        const auto &properties = catalog_ == PropertyCatalog::ArchiveTransport
                                     ? items_[i].ArchiveProperties
                                     : items_[i].Properties;
        FOR_VECTOR(j, properties) {
            const UString &name = properties[j].Name;
            unsigned k = 0;
            for (; k < propertyNames_.Size(); ++k)
                if (propertyNames_[k] == name)
                    break;
            if (k == propertyNames_.Size())
                propertyNames_.Add(name);
        }
    }
}

Z7_COM7F_IMF(QtPanelFolder::LoadItems()) { return S_OK; }

Z7_COM7F_IMF(QtPanelFolder::GetNumberOfItems(UInt32 *count)) {
    *count = items_.Size();
    return S_OK;
}

Z7_COM7F_IMF(QtPanelFolder::GetProperty(UInt32 index, PROPID id, PROPVARIANT *value)) {
    if (index >= items_.Size())
        return E_INVALIDARG;
    const CPanelItem &item = items_[index];
    NWindows::NCOM::CPropVariant prop;
    const int separator = item.RelPath.lastIndexOf('/');
    switch (id) {
    case kpidPath:
        if (catalog_ == PropertyCatalog::ArchiveTransport)
            prop = item.IsFolder
                       ? NArchive::NItemName::GetOsPath_Remove_TailSlash(ToUString(item.RelPath))
                       : NArchive::NItemName::GetOsPath(ToUString(item.RelPath));
        else
            prop = ToUString(item.RelPath);
        break;
    case kpidName:
        prop = ToUString(item.RelPath.mid(separator + 1));
        break;
    case kpidPrefix:
        prop = ToUString(item.RelPath.left(separator + 1));
        break;
    case kpidIsDir:
        prop = item.IsFolder;
        break;
    case kpidSize:
        if (item.SizeDefined)
            prop = item.Size;
        break;
    case kpidPackSize:
        if (item.PackSizeDefined)
            prop = item.PackSize;
        break;
    case kpidCRC:
        if (catalog_ != PropertyCatalog::ArchiveTransport)
            return E_NOTIMPL;
        if (item.Crc)
            prop = *item.Crc;
        break;
    case kpidMTime:
        if (catalog_ == PropertyCatalog::ArchiveTransport) {
            if (item.MTime && item.MTime->vt == VT_FILETIME)
                prop = *item.MTime;
            break;
        }
        return E_NOTIMPL;
    case kpidNumSubDirs:
        if (catalog_ != PropertyCatalog::ArchiveTransport)
            prop = item.NumSubDirs;
        break;
    case kpidNumSubFiles:
        if (catalog_ != PropertyCatalog::ArchiveTransport)
            prop = item.NumSubFiles;
        break;
    default:
        if (id < kSnapshotProperty || id - kSnapshotProperty >= propertyNames_.Size())
            return catalog_ == PropertyCatalog::ArchiveTransport ? prop.Detach(value) : E_NOTIMPL;
        const auto &properties = catalog_ == PropertyCatalog::ArchiveTransport
                                     ? item.ArchiveProperties
                                     : item.Properties;
        FOR_VECTOR(i, properties)
        if (properties[i].Name == propertyNames_[unsigned(id - kSnapshotProperty)]) {
            prop = properties[i].Value;
            break;
        }
    }
    return prop.Detach(value);
}

Z7_COM7F_IMF(QtPanelFolder::BindToFolder(UInt32, IFolderFolder **folder)) {
    *folder = nullptr;
    return E_NOTIMPL;
}

Z7_COM7F_IMF(QtPanelFolder::BindToFolder(const wchar_t *, IFolderFolder **folder)) {
    *folder = nullptr;
    return E_NOTIMPL;
}

Z7_COM7F_IMF(QtPanelFolder::BindToParentFolder(IFolderFolder **folder)) {
    *folder = nullptr;
    return E_NOTIMPL;
}

Z7_COM7F_IMF(QtPanelFolder::GetNumberOfProperties(UInt32 *count)) {
    *count = archivePropertyIds_.Size() + propertyNames_.Size();
    return S_OK;
}

Z7_COM7F_IMF(QtPanelFolder::GetPropertyInfo(UInt32 index, BSTR *name, PROPID *id, VARTYPE *type)) {
    if (index < archivePropertyIds_.Size()) {
        *name = NULL;
        *id = archivePropertyIds_[index];
        *type = k7z_PROPID_To_VARTYPE[(unsigned)*id];
        return S_OK;
    }
    index -= archivePropertyIds_.Size();
    if (index >= propertyNames_.Size())
        return E_INVALIDARG;
    *name = ::SysAllocStringLen(propertyNames_[index], propertyNames_[index].Len());
    *id = kSnapshotProperty + index;
    *type = VT_BSTR;
    return *name ? S_OK : E_OUTOFMEMORY;
}

Z7_COM7F_IMF(QtPanelFolder::GetFolderProperty(PROPID id, PROPVARIANT *value)) {
    if (id != kpidPath)
        return E_NOTIMPL;
    NWindows::NCOM::CPropVariant prop(ToUString(prefix_));
    return prop.Detach(value);
}

Z7_COM7F_IMF(QtPanelFolder::Open(IInStream *, const UInt64 *, IArchiveOpenCallback *)) {
    return E_NOTIMPL;
}

Z7_COM7F_IMF(QtPanelFolder::Close()) { return S_OK; }

Z7_COM7F_IMF(QtPanelFolder::Extract(const UInt32 *, UInt32, Int32, IArchiveExtractCallback *)) {
    return E_NOTIMPL; // Actual engine operations stay behind ArchiveBackend.
}

Z7_COM7F_IMF(QtPanelFolder::GetArchiveProperty(PROPID, PROPVARIANT *value)) {
    NWindows::NCOM::CPropVariant prop;
    return prop.Detach(value);
}

Z7_COM7F_IMF(QtPanelFolder::GetNumberOfArchiveProperties(UInt32 *count)) {
    *count = 0;
    return S_OK;
}

Z7_COM7F_IMF(QtPanelFolder::GetArchivePropertyInfo(UInt32, BSTR *, PROPID *, VARTYPE *)) {
    return E_INVALIDARG;
}
