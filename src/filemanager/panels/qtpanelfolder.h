// Qt metadata snapshot adapter for the unchanged upstream folder contract.
#pragma once
#include "filemanager/panels/Panel.h"

class QtPanelFolder final : public IFolderFolder,
                            public IInArchive,
                            public IArchiveGetRawProps,
                            public CMyUnknownImp {
    Z7_COM_QI_BEGIN2(IFolderFolder)
    else if (catalog_ == PropertyCatalog::ArchiveTransport && iid == IID_IInArchive) {
        *outObject = static_cast<IInArchive *>(this);
    }
    else if (!rawPropertyIds_.IsEmpty() && iid == IID_IArchiveGetRawProps) {
        *outObject = static_cast<IArchiveGetRawProps *>(this);
    }
    Z7_COM_QI_END
    Z7_COM_ADDREF_RELEASE
  public:
    enum class PropertyCatalog { CustomPresentation, ArchiveTransport };
    QtPanelFolder(const QString &prefix, const CObjectVector<CPanelItem> &items,
                  PropertyCatalog catalog = PropertyCatalog::CustomPresentation);
    Z7_IFACE_COM7_IMP(IFolderFolder)
    Z7_IFACE_COM7_IMP(IArchiveGetRawProps)
    // Shared read-only native metadata contract, not an in-process codec.
    Z7_COM7F_IMP(Open(IInStream *, const UInt64 *, IArchiveOpenCallback *))
    Z7_COM7F_IMP(Close())
    Z7_COM7F_IMP(Extract(const UInt32 *, UInt32, Int32, IArchiveExtractCallback *))
    Z7_COM7F_IMP(GetArchiveProperty(PROPID, PROPVARIANT *))
    Z7_COM7F_IMP(GetNumberOfArchiveProperties(UInt32 *))
    Z7_COM7F_IMP(GetArchivePropertyInfo(UInt32, BSTR *, PROPID *, VARTYPE *))

  private:
    QString prefix_;
    CObjectVector<CPanelItem> items_;
    UStringVector propertyNames_;
    PropertyCatalog catalog_;
    CRecordVector<PROPID> archivePropertyIds_;
    CRecordVector<PROPID> rawPropertyIds_;
};
