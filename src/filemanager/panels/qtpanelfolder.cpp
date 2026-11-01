#include "filemanager/panels/qtpanelfolder.h"
#include "7zip/Archive/Common/ItemNameUtils.h"
#include "7zip/PropID.h"
#include "Windows/PropVariant.h"
#include <QDateTime>

namespace {
constexpr PROPID kSnapshotProperty = 0x10000;
const PROPID kArchiveProperties[] = {kpidPath, kpidSize, kpidPackSize};

void SetArchiveTime(const QString &text, NWindows::NCOM::CPropVariant &prop) {
    // List.cpp prints local time. This file-only engine transport has to reverse
    // that presentation; source codecs normally return FILETIME directly.
    if (text.size() < 19)
        return;
    const QDateTime time = QDateTime::fromString(text.left(19), "yyyy-MM-dd HH:mm:ss");
    if (!time.isValid())
        return;
    const qint64 seconds = time.toSecsSinceEpoch();
    constexpr qint64 epochOffset = 11644473600LL;
    if (seconds < -epochOffset)
        return;
    QString fraction;
    if (text.size() > 19) {
        if (text[19] != '.' || text.size() > 27 || text.size() == 20)
            return;
        fraction = text.mid(20);
        for (const QChar c : fraction)
            if (c < '0' || c > '9')
                return;
    }
    const unsigned digits = unsigned(fraction.size());
    if (digits > 7)
        return;
    const UInt64 ticks =
        UInt64(seconds + epochOffset) * 10000000 + fraction.leftJustified(7, '0').toUInt();
    prop.SetAsTimeFrom_Ft64_Prec(ticks, k_PropVar_TimePrec_Base + digits);
}
} // namespace

QtPanelFolder::QtPanelFolder(const QString &prefix, const CObjectVector<CPanelItem> &items,
                             PropertyCatalog catalog)
    : prefix_(prefix), items_(items), catalog_(catalog) {
    if (catalog_ == PropertyCatalog::ArchiveTransport) {
        bool hasMTimeText = false, hasCrc = false;
        FOR_VECTOR(i, items_) {
            if (!items_[i].MTimeText.isEmpty())
                hasMTimeText = true;
            hasCrc |= items_[i].Crc.has_value();
            for (auto p = items_[i].RawProperties.cbegin(); p != items_[i].RawProperties.cend();
                 ++p)
                if (rawPropertyIds_.FindInSorted(p.key()) < 0)
                    rawPropertyIds_.AddToUniqueSorted(p.key());
        }
        for (const PROPID id : kArchiveProperties)
            archivePropertyIds_.Add(id);
        if (hasMTimeText)
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
            SetArchiveTime(item.MTimeText, prop);
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
