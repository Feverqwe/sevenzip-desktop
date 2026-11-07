#include "7zip/PropID.h"
#include "Windows/PropVariant.h"
#include "Windows/PropVariantConv.h"
#include "archive/agent/Agent.h"
#include "filemanager/panels/qtpanelfolder.h"
#include "filemanager/providers/qtlocationfolder.h"

#include <QDir>
#include <QTest>

using NWindows::NCOM::CPropVariant;

class FailingCatalog final : public IFolderFolder, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_1(IFolderFolder)
  public:
    bool FailCount = false;
    bool WriteFailedCount = false;
    HRESULT CountFailure = E_FAIL;
    bool FailDescriptor = false;
    Z7_IFACE_COM7_IMP(IFolderFolder)
};

Z7_COM7F_IMF(FailingCatalog::LoadItems()) { return S_OK; }
Z7_COM7F_IMF(FailingCatalog::GetNumberOfItems(UInt32 *count)) {
    *count = 0;
    return S_OK;
}
Z7_COM7F_IMF(FailingCatalog::GetNumberOfProperties(UInt32 *count)) {
    if (FailCount) {
        if (WriteFailedCount)
            *count = 1;
        return CountFailure;
    }
    *count = 1;
    return S_OK;
}
Z7_COM7F_IMF(FailingCatalog::GetPropertyInfo(UInt32 index, BSTR *name, PROPID *id, VARTYPE *type)) {
    if (FailDescriptor)
        return E_FAIL;
    if (index != 0)
        return E_INVALIDARG;
    *name = nullptr;
    *id = kpidPath;
    *type = VT_BSTR;
    return S_OK;
}
Z7_COM7F_IMF(FailingCatalog::GetProperty(UInt32, PROPID, PROPVARIANT *)) { return E_NOTIMPL; }
Z7_COM7F_IMF(FailingCatalog::GetFolderProperty(PROPID, PROPVARIANT *)) { return E_NOTIMPL; }
Z7_COM7F_IMF(FailingCatalog::BindToFolder(UInt32, IFolderFolder **folder)) {
    *folder = nullptr;
    return E_NOTIMPL;
}
Z7_COM7F_IMF(FailingCatalog::BindToFolder(const wchar_t *, IFolderFolder **folder)) {
    *folder = nullptr;
    return E_NOTIMPL;
}
Z7_COM7F_IMF(FailingCatalog::BindToParentFolder(IFolderFolder **folder)) {
    *folder = nullptr;
    return E_NOTIMPL;
}

class ArchivePropertyCatalogTests : public QObject {
    Q_OBJECT

    static void expectProperty(IFolderFolder *folder, PROPID expectedId, VARTYPE expectedType) {
        UInt32 count = 0;
        QCOMPARE(folder->GetNumberOfProperties(&count), S_OK);
        for (UInt32 i = 0; i < count; ++i) {
            CMyComBSTR name;
            PROPID id;
            VARTYPE type;
            QCOMPARE(folder->GetPropertyInfo(i, &name, &id, &type), S_OK);
            if (id == expectedId) {
                QCOMPARE(type, expectedType);
                QVERIFY(!name);
                return;
            }
        }
        QFAIL(qPrintable(QString("Missing property %1").arg(expectedId)));
    }

  private slots:
    void optionalMetadataKeepsNamedValuesReachable_data() {
        QTest::addColumn<bool>("timestamp");
        QTest::addColumn<bool>("crc");
        QTest::newRow("neither") << false << false;
        QTest::newRow("time") << true << false;
        QTest::newRow("crc") << false << true;
        QTest::newRow("both") << true << true;
    }
    void optionalMetadataKeepsNamedValuesReachable() {
        QFETCH(bool, timestamp);
        QFETCH(bool, crc);
        ArchiveEntry first{"first.txt", 1, 1};
        first.properties.insert("Note", QStringLiteral("первая запись"));
        if (timestamp) {
            first.modifiedTime.emplace();
            first.modifiedTime->SetAsTimeFrom_Ft64_Prec(116444736001234567ULL,
                                                        k_PropVar_TimePrec_100ns);
        }
        if (crc)
            first.crc = 0x12345678;
        ArchiveEntry second{"second.txt", 2, 2};
        second.properties.insert("Note", QStringLiteral("вторая запись"));
        CMyComPtr<IFolderFolder> folder;
        QCOMPARE(CreateArchiveFolder({first, second}, UString(L"7z"), &folder), S_OK);
        QCOMPARE(folder->LoadItems(), S_OK);
        UInt32 count = 0;
        QCOMPARE(folder->GetNumberOfProperties(&count), S_OK);
        bool foundNote = false;
        for (UInt32 i = 0; i < count; ++i) {
            CMyComBSTR name;
            PROPID id;
            VARTYPE type;
            QCOMPARE(folder->GetPropertyInfo(i, &name, &id, &type), S_OK);
            if (!name || QString::fromWCharArray(name) != "Note")
                continue;
            foundNote = true;
            CPropVariant value;
            QCOMPARE(folder->GetProperty(0, id, &value), S_OK);
            QCOMPARE(value.vt, VARTYPE(VT_BSTR));
            QCOMPARE(QString::fromWCharArray(value.bstrVal), first.properties.value("Note"));
            QCOMPARE(folder->GetProperty(1, id, &value), S_OK);
            QCOMPARE(value.vt, VARTYPE(VT_BSTR));
            QCOMPARE(QString::fromWCharArray(value.bstrVal), second.properties.value("Note"));
        }
        QVERIFY(foundNote);
    }
    void suppliedTimestampUsesNativeTimeAndMissingValuesRemainSparse() {
        ArchiveEntry known{"known.txt", 1, 1, "deliberately unrelated presentation text"};
        known.modifiedTime.emplace();
        const FILETIME original{UInt32(116444736001234567ULL), UInt32(116444736001234567ULL >> 32)};
        known.modifiedTime->SetAsTimeFrom_FT_Prec_Ns100(original, k_PropVar_TimePrec_1ns, 89);
        CMyComPtr<IFolderFolder> folder;
        QCOMPARE(CreateArchiveFolder(
                     {known, ArchiveEntry{"unknown.txt", 2, 2},
                      ArchiveEntry{"invalid.txt", 3, 3, "invalid timestamp"},
                      ArchiveEntry{"display-only.txt", 4, 4, "2026-09-28 12:34:56.1234567"}},
                     UString(L"7z"), &folder),
                 S_OK);
        expectProperty(folder, kpidMTime, VT_FILETIME);
        CPropVariant prop;
        QCOMPARE(folder->GetProperty(0, kpidMTime, &prop), S_OK);
        QCOMPARE(prop.vt, VARTYPE(VT_FILETIME));
        QCOMPARE(prop.filetime.dwLowDateTime, original.dwLowDateTime);
        QCOMPARE(prop.filetime.dwHighDateTime, original.dwHighDateTime);
        QCOMPARE(prop.wReserved1, WORD(k_PropVar_TimePrec_1ns));
        QCOMPARE(prop.wReserved2, WORD(89));
        QCOMPARE(folder->GetProperty(1, kpidMTime, &prop), S_OK);
        QCOMPARE(prop.vt, VARTYPE(VT_EMPTY));
        QCOMPARE(folder->GetProperty(2, kpidMTime, &prop), S_OK);
        QCOMPARE(prop.vt, VARTYPE(VT_EMPTY));
        QCOMPARE(folder->GetProperty(3, kpidMTime, &prop), S_OK);
        QCOMPARE(prop.vt, VARTYPE(VT_EMPTY));
    }

    void sourceFolderExpandsCatalogAndRemapsPath_data() {
        QTest::addColumn<bool>("flat");
        QTest::newRow("normal") << false;
        QTest::newRow("flat") << true;
    }

    void sourceFolderExpandsCatalogAndRemapsPath() {
        QFETCH(bool, flat);
        CMyComPtr<IFolderFolder> folder;
        QCOMPARE(CreateArchiveFolder({ArchiveEntry{QStringLiteral("папка й/a.txt"), 7, 3},
                                      ArchiveEntry{QStringLiteral("папка й/sub/b.txt"), 0, 0}},
                                     UString(L"7z"), &folder),
                 S_OK);
        CMyComPtr<IFolderSetFlatMode> mode;
        QCOMPARE(folder.QueryInterface(IID_IFolderSetFlatMode, &mode), S_OK);
        QCOMPARE(mode->SetFlatMode(flat), S_OK);
        QCOMPARE(folder->LoadItems(), S_OK);
        UInt32 count;
        QCOMPARE(folder->GetNumberOfProperties(&count), S_OK);
        const PROPID ids[] = {kpidName,       kpidSize,        kpidPackSize,
                              kpidNumSubDirs, kpidNumSubFiles, kpidPrefix};
        const VARTYPE types[] = {VT_BSTR, VT_UI8, VT_UI8, VT_UI8, VT_UI8, VT_BSTR};
        for (unsigned i = 0; i < (flat ? 6u : 5u); ++i)
            expectProperty(folder, ids[i], types[i]);
        for (UInt32 i = 0; i < count; ++i) {
            CMyComBSTR name;
            PROPID id;
            VARTYPE type;
            QCOMPARE(folder->GetPropertyInfo(i, &name, &id, &type), S_OK);
            QVERIFY(id != kpidPath);
            if (!flat)
                QVERIFY(id != kpidPrefix);
        }
        CMyComBSTR name;
        PROPID id;
        VARTYPE type;
        QCOMPARE(folder->GetPropertyInfo(count, &name, &id, &type), E_INVALIDARG);
        QCOMPARE(folder->GetPropertyInfo(0xFFFFFFFF, &name, &id, &type), E_INVALIDARG);
        CPropVariant prop;
        QCOMPARE(folder->GetProperty(0, kpidName, &prop), S_OK);
        QCOMPARE(QString::fromWCharArray(prop.bstrVal), QStringLiteral("папка й"));
        QCOMPARE(folder->GetProperty(0, kpidNumSubDirs, &prop), S_OK);
        QCOMPARE(prop.uhVal.QuadPart, UInt64(1));
        QCOMPARE(folder->GetProperty(0, kpidNumSubFiles, &prop), S_OK);
        QCOMPARE(prop.uhVal.QuadPart, UInt64(2));
        QCOMPARE(folder->GetProperty(0, kpidSize, &prop), S_OK);
        QCOMPARE(prop.vt, VARTYPE(flat ? VT_EMPTY : VT_UI8));
        if (!flat)
            QCOMPARE(prop.uhVal.QuadPart, UInt64(7));
    }

    void sourceFolderInsertsNameWhenTransportHasNoPath() {
        CObjectVector<CPanelItem> items;
        CPanelItem item;
        item.RelPath = "file.txt";
        item.Properties.Add({UString(L"Custom note"), UString(L"value")});
        items.Add(item);
        CMyComPtr<IFolderFolder> metadata = new QtPanelFolder("", items);
        auto *agentSpec = new CAgent;
        CMyComPtr<IFolderArcProps> agent = agentSpec;
        QCOMPARE(agentSpec->Init(metadata, UString(L"7z")), S_OK);
        CMyComPtr<IFolderFolder> folder;
        QCOMPARE(agentSpec->BindToRootFolder(&folder), S_OK);
        QVERIFY(!agentSpec->ThereIsPathProp);
        UInt32 count;
        QCOMPARE(folder->GetNumberOfProperties(&count), S_OK);
        const UInt32 originalCount = count;
        CMyComBSTR name;
        PROPID id;
        VARTYPE type;
        QCOMPARE(folder->GetPropertyInfo(0, &name, &id, &type), S_OK);
        QCOMPARE(id, PROPID(kpidName));
        QCOMPARE(type, VARTYPE(VT_BSTR));
        QVERIFY(!name);
        QCOMPARE(folder->GetPropertyInfo(1, &name, &id, &type), S_OK);
        QCOMPARE(QString::fromWCharArray(name), QString("Custom note"));
        CPropVariant prop;
        QCOMPARE(folder->GetProperty(0, id, &prop), S_OK);
        QCOMPARE(QString::fromWCharArray(prop.bstrVal), QString("value"));
        agent.Release();
        metadata.Release();
        QCOMPARE(folder->GetNumberOfProperties(&count), S_OK);
        QCOMPARE(count, originalCount);
    }

    void descriptorFailureDoesNotPublishPartialOwnerAndCanRetry() {
        auto *metadataSpec = new FailingCatalog;
        CMyComPtr<IFolderFolder> metadata = metadataSpec;
        metadataSpec->FailDescriptor = true;
        auto *agentSpec = new CAgent;
        CMyComPtr<IFolderArcProps> agent = agentSpec;
        QCOMPARE(agentSpec->Init(metadata, UString(L"7z")), S_OK);
        CMyComPtr<IFolderFolder> folder;
        QCOMPARE(agentSpec->BindToRootFolder(&folder), E_FAIL);
        QVERIFY(!folder);
        metadataSpec->FailCount = false;
        metadataSpec->FailDescriptor = false;
        QCOMPARE(agentSpec->BindToRootFolder(&folder), S_OK);
        QVERIFY(agentSpec->ThereIsPathProp);
        UInt32 count;
        QCOMPARE(folder->GetNumberOfProperties(&count), S_OK);
        expectProperty(folder, kpidName, VT_BSTR);
        CMyComBSTR name;
        PROPID id;
        VARTYPE type;
        metadataSpec->FailCount = true;
        QCOMPARE(folder->GetNumberOfProperties(&count), E_FAIL);
        // Source GetPropertyInfo consumes the count without a HRESULT gate;
        // an unwritten count is safely initialized to zero at this boundary.
        QCOMPARE(folder->GetPropertyInfo(0, &name, &id, &type), S_OK);
        QCOMPARE(id, PROPID(kpidNumSubDirs));
        metadataSpec->FailCount = false;
        metadataSpec->FailDescriptor = true;
        QCOMPARE(folder->GetPropertyInfo(0, &name, &id, &type), E_FAIL);
    }

    void bindingAndDescriptorUseWrittenCount_data() {
        QTest::addColumn<int>("countResult");
        QTest::addColumn<bool>("writeCount");
        QTest::newRow("failed-written") << int(E_FAIL) << true;
        QTest::newRow("false-written") << int(S_FALSE) << true;
        QTest::newRow("failed-unwritten") << int(E_FAIL) << false;
        QTest::newRow("false-unwritten") << int(S_FALSE) << false;
    }
    void bindingAndDescriptorUseWrittenCount() {
        QFETCH(int, countResult);
        QFETCH(bool, writeCount);
        auto *metadataSpec = new FailingCatalog;
        CMyComPtr<IFolderFolder> metadata = metadataSpec;
        metadataSpec->FailCount = true;
        metadataSpec->WriteFailedCount = writeCount;
        metadataSpec->CountFailure = HRESULT(countResult);
        auto *agentSpec = new CAgent;
        CMyComPtr<IInFolderArchive> agent = agentSpec;
        QCOMPARE(agentSpec->Init(metadata, UString(L"7z")), S_OK);
        CMyComPtr<IFolderFolder> folder;
        QCOMPARE(agent->BindToRootFolder(&folder), S_OK);
        QVERIFY(folder);
        QCOMPARE(agentSpec->ThereIsPathProp, writeCount);
        QCOMPARE(folder->LoadItems(), S_OK);
        UInt32 count = 99;
        QCOMPARE(folder->GetNumberOfItems(&count), S_OK);
        QCOMPARE(count, UInt32(0));
        CMyComBSTR name;
        PROPID id;
        VARTYPE type;
        QCOMPARE(folder->GetPropertyInfo(0, &name, &id, &type), S_OK);
        QCOMPARE(id, PROPID(kpidName));
        QCOMPARE(type, VARTYPE(VT_BSTR));
        QCOMPARE(folder->GetPropertyInfo(1, &name, &id, &type), S_OK);
        QCOMPARE(id, PROPID(kpidNumSubDirs));
        // GetNumberOfProperties intentionally retains its original RINOK.
        QCOMPARE(folder->GetNumberOfProperties(&count), HRESULT(countResult));
        metadataSpec->FailCount = false;
        // A fresh owner re-enumerates restored metadata after a transient
        // failure; an already loaded proxy keeps the original caching policy.
        auto *repairedSpec = new CAgent;
        CMyComPtr<IInFolderArchive> repaired = repairedSpec;
        QCOMPARE(repairedSpec->Init(metadata, UString(L"7z")), S_OK);
        CMyComPtr<IFolderFolder> repairedFolder;
        QCOMPARE(repaired->BindToRootFolder(&repairedFolder), S_OK);
        QVERIFY(repairedSpec->ThereIsPathProp);
        QCOMPARE(repairedFolder->GetNumberOfProperties(&count), S_OK);
        expectProperty(repairedFolder, kpidName, VT_BSTR);
    }

    void transportCatalogOwnsNativeTypesAndFullUnicodePaths() {
        CObjectVector<CPanelItem> items;
        CPanelItem item;
        item.RelPath = QStringLiteral("папка й/файл 😀 с пробелом.txt");
        item.SizeDefined = true;
        item.Size = 0xFFFFFFFFFFFFFFFFULL;
        item.PackSize = 0;
        // UI custom fields cannot redefine the archive engine metadata catalog.
        item.Properties.Add({UString(L"formatted UI name"), UString(L"formatted UI value")});
        items.Add(item);
        CMyComPtr<IFolderFolder> folder =
            new QtPanelFolder("", items, QtPanelFolder::PropertyCatalog::ArchiveTransport);
        items.Clear();
        UInt32 count = 0;
        QCOMPARE(folder->GetNumberOfProperties(&count), S_OK);
        const PROPID expectedIds[] = {kpidPath, kpidSize, kpidPackSize};
        const VARTYPE expectedTypes[] = {VT_BSTR, VT_UI8, VT_UI8};
        for (unsigned i = 0; i < 3; ++i)
            expectProperty(folder, expectedIds[i], expectedTypes[i]);
        for (UInt32 i = 0; i < count; ++i) {
            CMyComBSTR name;
            PROPID id;
            VARTYPE type;
            QCOMPARE(folder->GetPropertyInfo(i, &name, &id, &type), S_OK);
            QVERIFY(QString::fromWCharArray(name) != QString("formatted UI name"));
        }
        CPropVariant prop;
        QCOMPARE(folder->GetProperty(0, kpidPath, &prop), S_OK);
        QCOMPARE(QDir::fromNativeSeparators(QString::fromWCharArray(prop.bstrVal)), item.RelPath);
        QCOMPARE(folder->GetProperty(0, kpidSize, &prop), S_OK);
        QCOMPARE(prop.vt, VARTYPE(VT_UI8));
        QCOMPARE(prop.uhVal.QuadPart, UInt64(0xFFFFFFFFFFFFFFFFULL));
        QCOMPARE(folder->GetProperty(0, kpidPackSize, &prop), S_OK);
        QCOMPARE(prop.vt, VARTYPE(VT_UI8));
        QCOMPARE(prop.uhVal.QuadPart, UInt64(0));
        CMyComBSTR name;
        PROPID id;
        VARTYPE type;
        QCOMPARE(folder->GetPropertyInfo(count, &name, &id, &type), E_INVALIDARG);
        QCOMPARE(folder->GetProperty(1, kpidPath, &prop), E_INVALIDARG);
        QCOMPARE(folder->GetProperty(0, kpidMTime, &prop), S_OK);
        QCOMPARE(prop.vt, VARTYPE(VT_EMPTY));
    }

    void knownCatalogDoesNotFabricateAbsentValues() {
        CObjectVector<CPanelItem> items;
        CPanelItem unknown;
        unknown.RelPath = "unknown";
        unknown.PackSizeDefined = false;
        items.Add(unknown);
        CMyComPtr<IFolderFolder> folder =
            new QtPanelFolder("", items, QtPanelFolder::PropertyCatalog::ArchiveTransport);
        CPropVariant prop(UInt64(123));
        QCOMPARE(folder->GetProperty(0, kpidSize, &prop), S_OK);
        QCOMPARE(prop.vt, VARTYPE(VT_EMPTY));
        prop = UInt64(123);
        QCOMPARE(folder->GetProperty(0, kpidPackSize, &prop), S_OK);
        QCOMPARE(prop.vt, VARTYPE(VT_EMPTY));
        CObjectVector<CPanelItem> empty;
        CMyComPtr<IFolderFolder> noItems =
            new QtPanelFolder("", empty, QtPanelFolder::PropertyCatalog::ArchiveTransport);
        UInt32 count;
        QCOMPARE(noItems->GetNumberOfProperties(&count), S_OK);
        expectProperty(noItems, kpidPath, VT_BSTR);
        expectProperty(noItems, kpidSize, VT_UI8);
        expectProperty(noItems, kpidPackSize, VT_UI8);
        QCOMPARE(noItems->GetProperty(0, kpidSize, &prop), E_INVALIDARG);
    }

    void archiveFactoryPublishesTypedProviderWithoutChangingLegacyCatalog() {
        CMyComPtr<IFolderFolder> folder;
        QCOMPARE(CreateArchiveFolder({ArchiveEntry{QStringLiteral("папка/файл.txt"), -1, 0}},
                                     UString(L"7z"), &folder),
                 S_OK);
        UInt32 count;
        QCOMPARE(folder->GetNumberOfProperties(&count), S_OK);
        CMyComPtr<IFolderFolder> child;
        QCOMPARE(folder->BindToFolder(UInt32(0), &child), S_OK);
        CPropVariant prop;
        QCOMPARE(child->GetProperty(0, kpidSize, &prop), S_OK);
        QCOMPARE(prop.vt, VARTYPE(VT_EMPTY));
        QCOMPARE(child->GetProperty(0, kpidPackSize, &prop), S_OK);
        QCOMPARE(prop.vt, VARTYPE(VT_UI8));
        QCOMPARE(prop.uhVal.QuadPart, UInt64(0));
        QCOMPARE(child->GetProperty(0, kpidNumSubDirs, &prop), S_OK);
        QCOMPARE(prop.vt, VARTYPE(VT_EMPTY));
        QCOMPARE(child->GetProperty(0, kpidNumSubFiles, &prop), S_OK);
        QCOMPARE(prop.vt, VARTYPE(VT_EMPTY));
        CObjectVector<CPanelItem> custom;
        CPanelItem ui;
        ui.Properties.Add({UString(L"Custom й"), UString(L"value")});
        custom.Add(ui);
        CMyComPtr<IFolderFolder> legacy = new QtPanelFolder("", custom);
        QCOMPARE(legacy->GetNumberOfProperties(&count), S_OK);
        QCOMPARE(count, 1u);
        CMyComBSTR name;
        PROPID id;
        VARTYPE type;
        QCOMPARE(legacy->GetPropertyInfo(0, &name, &id, &type), S_OK);
        QCOMPARE(QString::fromWCharArray(name), QStringLiteral("Custom й"));
        QCOMPARE(type, VARTYPE(VT_BSTR));
    }
};

QTEST_GUILESS_MAIN(ArchivePropertyCatalogTests)
#include "archive_property_catalog_tests.moc"
