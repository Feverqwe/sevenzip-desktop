#include "Common/MyCom.h"
#include "Common/StringConvert.h"
#include "Windows/COM.h"
#include "Windows/MemoryGlobal.h"
#include "platform/shell/ContextMenu.h"
#include "platform/shell/Shell.h"
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <cstring>
#include <type_traits>

namespace {
class SelectionData final : public IDataObject {
  public:
    QByteArray drop;
    QByteArray idList;
    QByteArray attributes;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **) override { return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC *format, STGMEDIUM *medium) override {
        const QByteArray *bytes = nullptr;
        if (format->cfFormat == CF_HDROP)
            bytes = &drop;
        else if (format->cfFormat == RegisterClipboardFormatW(L"Shell IDList Array"))
            bytes = &idList;
        else if (format->cfFormat == RegisterClipboardFormatW(L"File Attributes Array"))
            bytes = &attributes;
        if (!bytes || bytes->isEmpty())
            return DV_E_FORMATETC;
        *medium = {};
        medium->tymed = TYMED_HGLOBAL;
        medium->hGlobal = GlobalAlloc(GMEM_MOVEABLE, SIZE_T(bytes->size()));
        if (!medium->hGlobal)
            return E_OUTOFMEMORY;
        void *data = GlobalLock(medium->hGlobal);
        if (!data) {
            GlobalFree(medium->hGlobal);
            return E_OUTOFMEMORY;
        }
        std::memcpy(data, bytes->constData(), size_t(bytes->size()));
        GlobalUnlock(medium->hGlobal);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC *, STGMEDIUM *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC *, FORMATETC *) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC *, STGMEDIUM *, BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD, IEnumFORMATETC **) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC *, DWORD, IAdviseSink *, DWORD *) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA **) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }
};

class RecordingLaunch final : public IQtContextMenuServices {
  public:
    UStringVector tested;
    unsigned calls = 0;
    void Open(const UString &, const UString &) override {}
    void ExtractArchives(const UStringVector &, const UString &, bool, bool, UInt32) override {}
    void TestArchives(const UStringVector &paths, bool) override {
        tested = paths;
        ++calls;
    }
    void CompressFiles(const UString &, const UString &, const UString &, bool,
                       const UStringVector &, bool, bool, bool) override {}
    void CalcChecksum(const UStringVector &, const UString &) override {}
    void ShowErrorMessage(const UString &) override {}
    void ShowErrorMessageRes(UInt32) override {}
};

QByteArray Drop(const QByteArray &names, bool wide) {
    DROPFILES header{};
    header.pFiles = sizeof(header);
    header.fWide = wide;
    QByteArray data(reinterpret_cast<const char *>(&header), sizeof(header));
    data += names;
    return data;
}

QByteArray Words(std::initializer_list<DWORD> words) {
    return QByteArray(reinterpret_cast<const char *>(words.begin()), int(words.size() * 4));
}
} // namespace

class NativeShellDataTests final : public QObject {
    Q_OBJECT
    void compareNativeTree(HMENU native, unsigned position,
                           const NQtShellMenu::CMenuHandle &expected, bool icons) {
        for (const auto &entry : expected.Items) {
            wchar_t text[2048]{};
            MENUITEMINFOW item{};
            item.cbSize = sizeof(item);
            item.fMask = MIIM_FTYPE | MIIM_ID | MIIM_STRING | MIIM_SUBMENU | MIIM_CHECKMARKS;
            item.dwTypeData = text;
            item.cch = 2048;
            QVERIFY(GetMenuItemInfoW(native, position++, TRUE, &item));
            QCOMPARE(item.wID, entry.ID);
            QCOMPARE(bool(item.fType & MFT_SEPARATOR), entry.Type == NQtShellMenu::kSeparator);
            if (entry.Type != NQtShellMenu::kSeparator)
                QCOMPARE(QString::fromWCharArray(text), QString::fromWCharArray(entry.Text));
            QCOMPARE(bool(item.hSubMenu), bool(entry.SubMenu));
            QCOMPARE(bool(item.hbmpUnchecked), icons && entry.Bitmap != 0);
            if (item.hbmpUnchecked) {
                BITMAP bitmap{};
                QCOMPARE(GetObjectW(item.hbmpUnchecked, sizeof(bitmap), &bitmap),
                         int(sizeof(bitmap)));
                QVERIFY(bitmap.bmWidth > 0);
                QVERIFY(bitmap.bmHeight > 0);
            }
            if (entry.SubMenu) {
                QCOMPARE(GetMenuItemCount(item.hSubMenu), int(entry.SubMenu->Items.size()));
                compareNativeTree(item.hSubMenu, 0, *entry.SubMenu, icons);
            }
        }
    }
  private slots:
    void nativeMenuRetainsSourceTreeAndForeignItems_data() {
        QTest::addColumn<bool>("cascaded");
        QTest::addColumn<bool>("icons");
        QTest::addColumn<unsigned>("selectionSize");
        for (bool cascaded : {false, true})
            for (bool icons : {false, true})
                for (unsigned selectionSize : {1u, 20u})
                    QTest::newRow(qPrintable(QString("cascade-%1-icons-%2-selection-%3")
                                                 .arg(cascaded)
                                                 .arg(icons)
                                                 .arg(selectionSize)))
                        << cascaded << icons << selectionSize;
    }

    void nativeMenuRetainsSourceTreeAndForeignItems() {
        QFETCH(bool, cascaded);
        QFETCH(bool, icons);
        QFETCH(unsigned, selectionSize);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString path = fixture.filePath(QStringLiteral("файл 密碼 with space.zip"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("fixture"), qint64(7));
        file.close();
        const QString settings = fixture.filePath("settings.ini");
        CContextMenuInfo info;
        info.Cascaded.Val = cascaded;
        info.Cascaded.Def = true;
        info.MenuIcons.Val = icons;
        info.MenuIcons.Def = true;
        info.Flags = UInt32(-1);
        info.Flags_Def = true;
        info.Save(settings);
        RecordingLaunch launch;
        CZipContextMenu source(settings, &launch);
        CZipContextMenu consumer(settings, &launch);
        source.Init_For_Shell();
        consumer.Init_For_Shell();
        const std::wstring selected = QDir::toNativeSeparators(path).toStdWString();
        for (unsigned i = 0; i < selectionSize; ++i) {
            source._fileNames.Add(UString(selected.c_str()));
            consumer._fileNames.Add(UString(selected.c_str()));
        }
        NQtShellMenu::CMenuHandle tree;
        const HRESULT generated = source.QueryContextMenu(&tree, 0, 100, 999, CMF_NORMAL);
        QVERIFY(SUCCEEDED(generated));
        QVERIFY(!tree.Items.empty());
        const auto destroy = [](HMENU menu) { DestroyMenu(menu); };
        std::unique_ptr<std::remove_pointer_t<HMENU>, decltype(destroy)> native(CreatePopupMenu(),
                                                                                destroy);
        QVERIFY(native);
        QVERIFY(AppendMenuW(native.get(), MF_STRING, 7000, L"Foreign before"));
        QVERIFY(AppendMenuW(native.get(), MF_STRING, 7001, L"Foreign after"));
        QCOMPARE(consumer.QueryContextMenu(native.get(), 1, 100, 999, CMF_NORMAL), generated);
        QCOMPARE(GetMenuItemCount(native.get()), int(tree.Items.size()) + 2);
        QCOMPARE(GetMenuItemID(native.get(), 0), UINT(7000));
        QCOMPARE(GetMenuItemID(native.get(), int(tree.Items.size()) + 1), UINT(7001));
        compareNativeTree(native.get(), 1, tree, icons);
        QCOMPARE(consumer.Commands().Size(), source.Commands().Size());
        const int test = consumer.FindVerb(UString(L"SevenZipTest"));
        QVERIFY(test >= 0);
        CMINVOKECOMMANDINFO invoke{};
        invoke.cbSize = sizeof(invoke);
        invoke.lpVerb = MAKEINTRESOURCEA(test);
        QCOMPARE(consumer.InvokeCommand(&invoke), S_OK);
        QCOMPARE(launch.calls, unsigned(1));
        QCOMPARE(launch.tested.Size(), selectionSize);
        // Reject an invalid insertion position before touching the host menu.
        QCOMPARE(consumer.QueryContextMenu(native.get(), 999, 100, 999, CMF_NORMAL), E_INVALIDARG);
        QCOMPARE(GetMenuItemCount(native.get()), int(tree.Items.size()) + 2);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("fixture"));
    }

    void unicodeAndAnsiDropKeepSelectionOrder() {
        SelectionData data;
        const wchar_t names[] = L"C:\\файл 密碼 with space.txt\0C:\\second.txt\0";
        data.drop = Drop(QByteArray(reinterpret_cast<const char *>(names), sizeof(names)), true);
        UStringVector paths;
        QCOMPARE(NWindows::NShell::DataObject_GetData_HDROP_or_IDLIST_Names(&data, paths), S_OK);
        QCOMPARE(paths.Size(), unsigned(2));
        QCOMPARE(QString::fromWCharArray(paths[0]), QStringLiteral("C:\\файл 密碼 with space.txt"));
        QCOMPARE(QString::fromWCharArray(paths[1]), QStringLiteral("C:\\second.txt"));
        const char ansi[] = "C:\\space name.txt\0C:\\second.txt\0";
        data.drop = Drop(QByteArray(ansi, sizeof(ansi)), false);
        QCOMPARE(NWindows::NShell::DataObject_GetData_HDROP_or_IDLIST_Names(&data, paths), S_OK);
        QCOMPARE(paths.Size(), unsigned(2));
        QCOMPARE(QString::fromWCharArray(paths[0]), QStringLiteral("C:\\space name.txt"));
    }

    void malformedDropAndIdListAreRejected_data() {
        QTest::addColumn<QByteArray>("drop");
        QTest::newRow("short-header") << QByteArray(3, 'x');
        auto beforeHeader = Drop(QByteArray(4, '\0'), true);
        DWORD offset = 1;
        std::memcpy(beforeHeader.data(), &offset, sizeof(offset));
        QTest::newRow("offset-before-header") << beforeHeader;
        auto outside = Drop(QByteArray(4, '\0'), true);
        offset = 0xffffffff;
        std::memcpy(outside.data(), &offset, sizeof(offset));
        QTest::newRow("offset-outside-block") << outside;
        QTest::newRow("odd-utf16-size") << Drop(QByteArray(3, 'x'), true);
        QTest::newRow("unterminated-utf16") << Drop(QByteArray(4, 'x'), true);
        QTest::newRow("unterminated-ansi") << Drop(QByteArray("file"), false);
    }

    void malformedDropAndIdListAreRejected() {
        QFETCH(QByteArray, drop);
        SelectionData data;
        data.drop = drop;
        // Both offsets are inside the CIDA header, so fallback cannot call
        // the shell with malformed PIDLs.
        data.idList = Words({1, 0, 0, 0});
        UStringVector paths;
        QCOMPARE(NWindows::NShell::DataObject_GetData_HDROP_or_IDLIST_Names(&data, paths),
                 E_INVALIDARG);
        QCOMPARE(paths.Size(), unsigned(0));
    }

    void fileAttributesUseOriginalFirstDirectoryPolicy() {
        SelectionData data;
        data.attributes =
            Words({3, FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_NORMAL, 0, FILE_ATTRIBUTE_NORMAL,
                   FILE_ATTRIBUTE_DIRECTORY, FILE_ATTRIBUTE_DIRECTORY});
        NWindows::NShell::CFileAttribs attributes;
        QCOMPARE(NWindows::NShell::DataObject_GetData_FILE_ATTRS(&data, attributes), S_OK);
        QCOMPARE(attributes.FirstDirIndex, 1);
        data.attributes = Words({0, 0, 0, 0});
        QCOMPARE(NWindows::NShell::DataObject_GetData_FILE_ATTRS(&data, attributes), E_INVALIDARG);
        QCOMPARE(attributes.FirstDirIndex, -1);
        data.attributes = Words({4, FILE_ATTRIBUTE_DIRECTORY, 0, FILE_ATTRIBUTE_DIRECTORY});
        QCOMPARE(NWindows::NShell::DataObject_GetData_FILE_ATTRS(&data, attributes), E_INVALIDARG);
        QCOMPARE(attributes.FirstDirIndex, -1);
    }

    void realShellDataObjectReturnsDisposableUnicodeSelection() {
        NWindows::NCOM::CComInitializer initialized;
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString path = fixture.filePath(QStringLiteral("файл 密碼 space.zip"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("fixture"), qint64(7));
        file.close();
        NWindows::NShell::CItemIDList pidl;
        const std::wstring native = QDir::toNativeSeparators(path).toStdWString();
        QCOMPARE(SHParseDisplayName(native.c_str(), nullptr, &pidl, 0, nullptr), S_OK);
        CMyComPtr<IShellFolder> parent;
        PCUITEMID_CHILD child = nullptr;
        QCOMPARE(SHBindToParent(pidl, IID_IShellFolder, reinterpret_cast<void **>(&parent), &child),
                 S_OK);
        CMyComPtr<IDataObject> data;
        QCOMPARE(parent->GetUIObjectOf(nullptr, 1, &child, IID_IDataObject, nullptr,
                                       reinterpret_cast<void **>(&data)),
                 S_OK);
        UStringVector paths;
        QCOMPARE(NWindows::NShell::DataObject_GetData_HDROP_or_IDLIST_Names(data, paths), S_OK);
        QCOMPARE(paths.Size(), unsigned(1));
        QCOMPARE(QDir::cleanPath(QDir::fromNativeSeparators(QString::fromWCharArray(paths[0]))),
                 path);
        // Exercise the real shell's IDLIST fallback as well: the ordinary data
        // object above can satisfy CF_HDROP without resolving any STRRET names.
        SelectionData idListOnly;
        {
            FORMATETC format = {CLIPFORMAT(RegisterClipboardFormatW(L"Shell IDList Array")),
                                nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
            NWindows::NCOM::CStgMedium medium;
            QCOMPARE(data->GetData(&format, &medium), S_OK);
            QCOMPARE(medium.tymed, DWORD(TYMED_HGLOBAL));
            NWindows::NMemory::CGlobalLock lock(medium.hGlobal);
            QVERIFY(lock.GetPointer());
            idListOnly.idList = QByteArray(static_cast<const char *>(lock.GetPointer()),
                                           qsizetype(GlobalSize(medium.hGlobal)));
        }
        QCOMPARE(NWindows::NShell::DataObject_GetData_HDROP_or_IDLIST_Names(&idListOnly, paths),
                 S_OK);
        QCOMPARE(paths.Size(), unsigned(1));
        QCOMPARE(QDir::cleanPath(QDir::fromNativeSeparators(QString::fromWCharArray(paths[0]))),
                 path);
        RecordingLaunch launch;
        CZipContextMenu commands(fixture.filePath("settings.ini"), &launch);
        QCOMPARE(commands.Initialize(nullptr, data, nullptr), S_OK);
        QCOMPARE(commands._fileNames.Size(), unsigned(1));
        NQtShellMenu::CMenuHandle menu;
        QVERIFY(SUCCEEDED(commands.QueryContextMenu(&menu, 0, 100, 999, 0)));
        const int offset = commands.FindVerb(UString(L"SevenZipTest"));
        QVERIFY(offset >= 0);
        wchar_t verb[128] = {};
        QCOMPARE(commands.GetCommandString(UINT_PTR(offset), GCS_VERBW, nullptr,
                                           reinterpret_cast<LPSTR>(verb), 128),
                 S_OK);
        QCOMPARE(QString::fromWCharArray(verb), QStringLiteral("SevenZipTest"));
        char shortVerb[4] = {};
        QCOMPARE(commands.GetCommandString(UINT_PTR(offset), GCS_VERBA, nullptr, shortVerb, 4),
                 S_OK);
        QCOMPARE(QByteArray(shortVerb), QByteArray("Sev"));
        QCOMPARE(commands.GetCommandString(9999, GCS_VALIDATEW, nullptr, nullptr, 0), S_FALSE);
        QCOMPARE(commands.GetCommandString(9999, GCS_VERBW, nullptr, nullptr, 0), E_INVALIDARG);
        CMINVOKECOMMANDINFOEX info{};
        info.cbSize = sizeof(info);
        info.fMask = CMIC_MASK_UNICODE;
        info.lpVerb = MAKEINTRESOURCEA(offset);
        // Explorer sends a null lpVerbW for numeric commands, even with
        // CMIC_MASK_UNICODE. The original decoder must use lpVerb's low word.
        QCOMPARE(commands.InvokeCommand(reinterpret_cast<LPCMINVOKECOMMANDINFO>(&info)), S_OK);
        QCOMPARE(launch.calls, unsigned(1));
        info.lpVerbW = verb;
        info.lpVerb = "invalid ansi verb";
        QCOMPARE(commands.InvokeCommand(reinterpret_cast<LPCMINVOKECOMMANDINFO>(&info)), S_OK);
        QCOMPARE(launch.calls, unsigned(2));
        info.fMask = 0;
        info.lpVerb = "SevenZipTest";
        QCOMPARE(commands.InvokeCommand(reinterpret_cast<LPCMINVOKECOMMANDINFO>(&info)), S_OK);
        QCOMPARE(launch.calls, unsigned(3));
        QCOMPARE(QString::fromWCharArray(launch.tested[0]), QString::fromWCharArray(paths[0]));
        info.lpVerb = "SevenZipUnknown";
        QCOMPARE(commands.InvokeCommand(reinterpret_cast<LPCMINVOKECOMMANDINFO>(&info)),
                 E_INVALIDARG);
        QCOMPARE(launch.calls, unsigned(3));
        QCOMPARE(commands.Initialize(nullptr, nullptr, nullptr), E_INVALIDARG);
        QCOMPARE(commands._fileNames.Size(), unsigned(0));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("fixture"));
    }
};

QTEST_GUILESS_MAIN(NativeShellDataTests)
#include "native_shell_data_tests.moc"
