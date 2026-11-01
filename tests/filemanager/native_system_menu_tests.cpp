#include "7zip/UI/FileManager/resource.h"
#include "Common/MyCom.h"
#include "Windows/COM.h"
#include "Windows/Menu.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/providers/FSFolder.h"
#include "localization/LangUtils.h"
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <shlobj.h>

namespace {
class RecordingContextMenu final : public IContextMenu {
  public:
    UINT offset = 0;
    UINT mask = 0;
    QString directory;
    QString unicodeDirectory;
    bool unicodeVerbIsNull = false;
    HRESULT result = S_OK;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **) override { return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE QueryContextMenu(HMENU, UINT, UINT, UINT, UINT) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetCommandString(UINT_PTR, UINT, UINT *, LPSTR, UINT) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE InvokeCommand(LPCMINVOKECOMMANDINFO info) override {
        if (info->cbSize != sizeof(CMINVOKECOMMANDINFOEX))
            return E_INVALIDARG;
        const auto *extended = reinterpret_cast<const CMINVOKECOMMANDINFOEX *>(info);
        offset = LOWORD(info->lpVerb);
        mask = info->fMask;
        directory = QString::fromLocal8Bit(info->lpDirectory);
        unicodeDirectory = QString::fromWCharArray(extended->lpDirectoryW);
        unicodeVerbIsNull = extended->lpVerbW == nullptr;
        return result;
    }
};
} // namespace

class NativeSystemMenuTests final : public QObject {
    Q_OBJECT
  private slots:
    void numericInvocationPreservesSourceUnicodeDirectoryAndFailure() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString directory = fixture.filePath(QStringLiteral("папка 密碼 with space"));
        QVERIFY(QDir().mkpath(directory));
        CMyComPtr<IFolderFolder> folder = new NFsFolder::CFSFolder(directory);
        QCOMPARE(folder->LoadItems(), S_OK);
        CPanel panel(folder);
        panel.LoadFullPath();
        RecordingContextMenu context;
        QCOMPARE(panel.InvokeShellContextMenu(17, &context), S_OK);
        QCOMPARE(context.offset, UINT(17));
        QCOMPARE(context.mask, UINT(CMIC_MASK_UNICODE));
        QVERIFY(context.unicodeVerbIsNull);
        QCOMPARE(QDir::cleanPath(QDir::fromNativeSeparators(context.unicodeDirectory)), directory);
        QVERIFY(!context.directory.isEmpty());
        context.result = E_ABORT;
        QCOMPARE(panel.InvokeShellContextMenu(3, &context), E_ABORT);
        QCOMPARE(context.offset, UINT(3));
        QCOMPARE(panel.InvokeShellContextMenu(0, nullptr), E_INVALIDARG);
    }
    void selectedUnicodeFileAndFolderCreateOriginalSystemGroup() {
        NWindows::NCOM::CComInitializer initialized;
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QFile file(fixture.filePath(QStringLiteral("файл 密碼 with space.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("fixture"), qint64(7));
        file.close();
        QVERIFY(QDir(fixture.path()).mkdir(QStringLiteral("папка 空")));
        CMyComPtr<IFolderFolder> folder = new NFsFolder::CFSFolder(fixture.path());
        QCOMPARE(folder->LoadItems(), S_OK);
        CPanel panel(folder);
        panel.LoadFullPath();
        UInt32 count = 0;
        QCOMPARE(folder->GetNumberOfItems(&count), S_OK);
        QCOMPARE(count, UInt32(2));
        CRecordVector<UInt32> selected;
        selected.Add(1);
        selected.Add(0);
        CMyComPtr<IContextMenu> context;
        QCOMPARE(panel.CreateShellContextMenu(selected, context), S_OK);
        QVERIFY(context);
        NWindows::CMenu menu;
        QVERIFY(menu.CreatePopup());
        NWindows::CMenuDestroyer destroy(menu);
        panel.CreateSystemMenu(menu, false, selected, context);
        QVERIFY(context);
        QCOMPARE(menu.GetItemCount(), 1);
        NWindows::CMenuItem group;
        group.fMask = MIIM_STRING | MIIM_SUBMENU | MIIM_ID;
        QVERIFY(menu.GetItem(0, true, group));
        QCOMPARE(ToQString(group.StringValue), ToQString(LangString(IDS_SYSTEM)));
        QVERIFY(group.hSubMenu);
        QVERIFY(::GetMenuItemCount(group.hSubMenu) > 0);
    }

    void emptySelectionAndMissingFileReleasePreviousContext() {
        NWindows::NCOM::CComInitializer initialized;
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString path = fixture.filePath(QStringLiteral("missing файл.txt"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        CMyComPtr<IFolderFolder> folder = new NFsFolder::CFSFolder(fixture.path());
        QCOMPARE(folder->LoadItems(), S_OK);
        CPanel panel(folder);
        panel.LoadFullPath();
        CRecordVector<UInt32> selected;
        selected.Add(0);
        CMyComPtr<IContextMenu> context;
        QCOMPARE(panel.CreateShellContextMenu(selected, context), S_OK);
        QVERIFY(context);
        CRecordVector<UInt32> empty;
        QCOMPARE(panel.CreateShellContextMenu(empty, context), E_FAIL);
        QVERIFY(!context);
        QCOMPARE(panel.CreateShellContextMenu(selected, context), S_OK);
        QVERIFY(QFile::remove(path));
        QVERIFY(FAILED(panel.CreateShellContextMenu(selected, context)));
        QVERIFY(!context);
        NWindows::CMenu menu;
        QVERIFY(menu.CreatePopup());
        NWindows::CMenuDestroyer destroy(menu);
        panel.CreateSystemMenu(menu, true, selected, context);
        QCOMPARE(menu.GetItemCount(), 0);
        QVERIFY(!context);
    }

    void rejectedSystemGroupInsertionReleasesNativeMenus() {
        NWindows::NCOM::CComInitializer initialized;
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QFile file(fixture.filePath(QStringLiteral("selected файл.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        CMyComPtr<IFolderFolder> folder = new NFsFolder::CFSFolder(fixture.path());
        QCOMPARE(folder->LoadItems(), S_OK);
        CPanel panel(folder);
        panel.LoadFullPath();
        CRecordVector<UInt32> selected;
        selected.Add(0);
        const auto create = [&](bool validParent) {
            NWindows::CMenu menu;
            NWindows::CMenuDestroyer destroy(menu);
            if (validParent && !menu.CreatePopup())
                return false;
            CMyComPtr<IContextMenu> context;
            panel.CreateSystemMenu(menu, false, selected, context);
            return bool(context) && (!validParent || menu.GetItemCount() == 1);
        };
        // Warm native shell providers before measuring this process's USER
        // resources. A null destination rejects insertion of the generated
        // group; its complete submenu tree must still be released.
        for (unsigned i = 0; i < 3; ++i)
            QVERIFY(create(true));
        const DWORD before = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
        // Providers can asynchronously release pre-existing cached resources.
        // More rejections than the baseline still expose one leaked menu per
        // call, even if every resource counted before the loop is reclaimed.
        for (DWORD i = 0; i <= before; ++i)
            QVERIFY(create(false));
        QTRY_VERIFY(GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS) <= before);
        QVERIFY(create(true));
    }
};

QTEST_GUILESS_MAIN(NativeSystemMenuTests)
#include "native_system_menu_tests.moc"
