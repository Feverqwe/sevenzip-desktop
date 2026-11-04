// Native snapshot adapter over original CArc/property readers and Console/List projection.
#include "archive/backend/archivebackend_nativeentries.h"
#include "7zip/UI/Common/PropIDUtils.h"
#include "Common/ComTry.h"
#include "Windows/FileIO.h"
#include "Windows/PropVariantConv.h"
#include "archive/resources/archivenativeopen.h"
#include "console/List.h"
#include <limits>

namespace {
QString Text(const UString &value) { return QString::fromWCharArray(value.Ptr(), value.Len()); }
qint64 SignedSize(UInt64 value) {
    return value <= UInt64((std::numeric_limits<qint64>::max)()) ? qint64(value) : -1;
}
struct CField {
    PROPID Id;
    UString Name;
};
HRESULT Fields(IInArchive *archive, QList<CField> &fields, bool header) {
    UInt32 count;
    RINOK(header ? archive->GetNumberOfArchiveProperties(&count)
                 : archive->GetNumberOfProperties(&count))
    for (UInt32 i = 0; i < count; ++i) {
        CMyComBSTR name;
        PROPID id;
        VARTYPE type;
        RINOK(header ? archive->GetArchivePropertyInfo(i, &name, &id, &type)
                     : archive->GetPropertyInfo(i, &name, &id, &type))
        fields.append({id, GetListingPropertyName(id, name)});
    }
    return S_OK;
}
HRESULT RawFields(const CArc &arc, UInt32 index, ArchiveEntry &entry, IProgress *progress) {
    if (!arc.GetRawProps)
        return S_OK;
    UInt32 count;
    RINOK(arc.GetRawProps->GetNumRawProps(&count))
    for (UInt32 i = 0; i < count; ++i) {
        RINOK(progress->SetCompleted(nullptr))
        CMyComBSTR name;
        PROPID id;
        RINOK(arc.GetRawProps->GetRawPropInfo(i, &name, &id))
        const void *data = nullptr;
        UInt32 size, type;
        RINOK(arc.GetRawProps->GetRawProp(index, id, &data, &size, &type))
        if (!size)
            continue;
        if (!data || type != NPropDataType::kRaw)
            return E_FAIL;
        const QByteArray bytes(static_cast<const char *>(data), qsizetype(size));
        entry.rawProperties.insert(id, bytes);
        // Preserve Console/List's decoded NT metadata and bounded hex display;
        // the full native bytes remain in the owned snapshot above.
        QString value;
        if (id == kpidNtSecure) {
            AString text;
            ConvertNtSecureToString(static_cast<const Byte *>(data), size, text);
            value = QString::fromLatin1(text.Ptr(), text.Len());
        } else if (id == kpidNtReparse) {
            UString text;
            if (ConvertNtReparseToString(static_cast<const Byte *>(data), size, text)) {
                value = Text(text);
                NWindows::NFile::CReparseAttr reparse;
                entry.link |= reparse.Parse(static_cast<const Byte *>(data), size);
            }
        }
        if (value.isEmpty())
            value = size <= 64 ? QString::fromLatin1(bytes.toHex())
                               : QStringLiteral("data:%1").arg(size);
        entry.properties.insert(Text(GetListingPropertyName(id, name)), value);
    }
    return S_OK;
}
HRESULT Item(const CArc &arc, UInt32 index, const QList<CField> &fields, ArchiveEntry &entry,
             IProgress *progress) {
    CReadArcItem item;
    RINOK(arc.GetItem(index, item))
    entry.path = Text(item.Path);
    entry.directory = item.IsDir;
    entry.size = entry.packedSize = -1;
    for (const auto &field : fields) {
        RINOK(progress->SetCompleted(nullptr))
        NWindows::NCOM::CPropVariant prop;
        RINOK(arc.Archive->GetProperty(index, field.Id, &prop))
        if (prop.vt == VT_EMPTY && field.Id != kpidAttrib)
            continue;
        const QString value = Text(GetListingItemPropertyValue(field.Id, prop, item.IsDir));
        switch (field.Id) {
        case kpidPath:
            break; // CArc supplies default/tree/alternate-stream path semantics.
        case kpidSize:
        case kpidPackSize: {
            UInt64 size;
            if (!ConvertPropVariantToUInt64(prop, size))
                return E_FAIL;
            (field.Id == kpidSize ? entry.sizeValue : entry.packedSizeValue) = size;
            (field.Id == kpidSize ? entry.size : entry.packedSize) = SignedSize(size);
            break;
        }
        case kpidMTime:
            if (prop.vt != VT_FILETIME)
                return E_FAIL;
            entry.modified = value;
            break;
        default:
            entry.properties.insert(Text(field.Name), value);
            if (field.Id == kpidCRC && prop.vt == VT_UI4)
                entry.crc = prop.ulVal;
            if (field.Id == kpidSymLink || field.Id == kpidHardLink)
                entry.link |= !value.isEmpty();
            if (field.Id == kpidPosixAttrib)
                entry.link |= value.startsWith('l');
            if (field.Id == kpidAttrib)
                entry.link |= value.contains('l') || value.contains("Link", Qt::CaseInsensitive);
        }
    }
    return RawFields(arc, index, entry, progress);
}
} // namespace

HRESULT ReadNativeArchiveListing(const ArchiveNativeOpen &reader, NativeArchiveListing &listing,
                                 IProgress *progress) {
    COM_TRY_BEGIN
    if (!progress || reader.archiveLink().Arcs.IsEmpty())
        return E_INVALIDARG;
    const auto &link = reader.archiveLink();
    const auto &arc = reader.arc();
    auto &info = listing.Info;
    info.containerLevels = link.Arcs.Size();
    info.format = QString::fromWCharArray(reader.nativeCodecs().GetFormatNamePtr(arc.FormatIndex));
    info.offset = qint64(arc.GetGlobalOffset());
    info.physicalSize = SignedSize(arc.PhySize_Defined ? arc.PhySize : arc.FileSize);
    // Console/List visits outer properties/subfile properties and then the
    // opened child. Preserve the same last-value-wins snapshot projection.
    for (unsigned a = 0; a < link.Arcs.Size(); ++a) {
        const auto &layer = link.Arcs[a];
        QList<CField> fields;
        RINOK(Fields(layer.Archive, fields, true))
        for (const auto &field : fields) {
            RINOK(progress->SetCompleted(nullptr))
            NWindows::NCOM::CPropVariant prop;
            RINOK(layer.Archive->GetArchiveProperty(field.Id, &prop))
            if (prop.vt != VT_EMPTY) {
                if (field.Id == kpidEncrypted && prop.vt == VT_BOOL)
                    info.encrypted |= prop.boolVal != VARIANT_FALSE;
                info.properties.insert(Text(field.Name),
                                       Text(GetListingPropertyValue(field.Id, prop)));
                if (field.Id == kpidNumVolumes || field.Id == kpidTotalPhySize) {
                    UInt64 value;
                    if (!ConvertPropVariantToUInt64(prop, value))
                        return E_FAIL;
                    if (field.Id == kpidNumVolumes) {
                        if (value > unsigned((std::numeric_limits<int>::max)()))
                            return E_FAIL;
                        info.volumeCount = int(value);
                    } else
                        info.totalPhysicalSize = SignedSize(value);
                }
            }
        }
        if (a + 1 < link.Arcs.Size()) {
            fields.clear();
            RINOK(Fields(layer.Archive, fields, false))
            for (const auto &field : fields) {
                NWindows::NCOM::CPropVariant prop;
                RINOK(layer.Archive->GetProperty(link.Arcs[a + 1].SubfileIndex, field.Id, &prop))
                if (prop.vt != VT_EMPTY && field.Id != kpidPath)
                    info.properties.insert(Text(field.Name),
                                           Text(GetListingPropertyValue(field.Id, prop)));
            }
        }
    }
    UInt32 count;
    RINOK(arc.Archive->GetNumberOfItems(&count))
    if (count > unsigned((std::numeric_limits<int>::max)()))
        return E_OUTOFMEMORY;
    RINOK(progress->SetTotal(count))
    QList<CField> fields;
    RINOK(Fields(arc.Archive, fields, false))
    for (UInt32 i = 0; i < count; ++i) {
        const UInt64 completed = i;
        RINOK(progress->SetCompleted(&completed))
        if (arc.Ask_Aux) {
            bool auxiliary;
            RINOK(Archive_IsItem_Aux(arc.Archive, i, auxiliary))
            if (auxiliary)
                continue;
        }
        ArchiveEntry entry;
        RINOK(Item(arc, i, fields, entry, progress))
        if (entry.directory)
            ++info.folders;
        else {
            ++info.files;
            if (info.unpackedSize >= 0) {
                if (entry.size < 0 ||
                    entry.size > (std::numeric_limits<qint64>::max)() - info.unpackedSize)
                    info.unpackedSize = -1;
                else
                    info.unpackedSize += entry.size;
            }
        }
        info.encrypted |= entry.properties.value("Encrypted") == "+";
        listing.Entries.append(std::move(entry));
    }
    info.encrypted |= info.properties.value("Method").contains("7zAES");
    CMyComPtr<IOutArchive> writer;
    arc.Archive.QueryInterface(IID_IOutArchive, &writer);
    // The official update publisher requires a single writable container.
    // A separately materialized child has its own single-container reader.
    listing.CanUpdate = bool(writer) && !arc.IsReadOnly && !reader.hasNestedStream();
    const UInt64 completed = count;
    return progress->SetCompleted(&completed);
    COM_TRY_END
}
