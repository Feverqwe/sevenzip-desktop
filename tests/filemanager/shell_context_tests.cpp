#include "Common/MyTypes.h"

#include "7zip/UI/Explorer/ContextMenuFlags.h"
#include "archive/operations/Update.h"
#include "dialogs/compress/qtcompresscallservices.h"
#include "filemanager/App.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/providers/FSFolder.h"
#include "gui/UpdateCallbackGUI.h"
#include "operations/CompressCall.h"
#include "platform/shell/ContextMenu.h"
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace {
constexpr HRESULT kAccessDenied = HRESULT(0x80070005);
class CLaunch : public virtual IQtContextMenuServices {
  public:
    QString operation, name, type, folder, method;
    UStringVector paths;
    bool dialog = false, elimDup = false, hashMode = false;
    UInt32 zone = UInt32(-1), errorResource = 0;
    void Open(const UString &path, const UString &arcType) override {
        operation = "open";
        paths.Clear();
        paths.Add(path);
        type = QString::fromWCharArray(arcType);
    }
    void ExtractArchives(const UStringVector &items, const UString &out, bool show, bool eliminate,
                         UInt32 writeZone) override {
        operation = "extract";
        paths = items;
        folder = QString::fromWCharArray(out);
        dialog = show;
        elimDup = eliminate;
        zone = writeZone;
    }
    void TestArchives(const UStringVector &items, bool hash) override {
        operation = "test";
        paths = items;
        hashMode = hash;
    }
    void CompressFiles(const UString &out, const UString &arcName, const UString &arcType, bool,
                       const UStringVector &items, bool, bool show, bool) override {
        operation = "compress";
        paths = items;
        folder = QString::fromWCharArray(out);
        name = QString::fromWCharArray(arcName);
        type = QString::fromWCharArray(arcType);
        dialog = show;
    }
    void CalcChecksum(const UStringVector &items, const UString &hash) override {
        operation = "hash";
        paths = items;
        method = QString::fromWCharArray(hash);
    }
    void ShowErrorMessage(const UString &) override { operation = "error"; }
    void ShowErrorMessageRes(UInt32 id) override {
        operation = "error";
        errorResource = id;
    }
};
class CWorkflow final : public CLaunch, public IQtCompressCallServices {
  public:
    CCodecs codecs;
    CExtractOptions extraction;
    CUpdateOptions update;
    HRESULT guiResult = S_OK;
    HRESULT providerResult = S_OK;
    HRESULT reported = S_OK;
    int guiCalls = 0;
    bool shown = false, multi = false, hashType = false;
    bool displayedUpdate = false, failedUpdate = false;
    CWorkflow() {
        CArcInfoEx format;
        format.Name = "7z";
        format.UpdateEnabled = true;
        format.AddExts(UString(L"7z"), UString());
        codecs.Formats.Add(format);
        format.Name = "read-only";
        format.UpdateEnabled = false;
        codecs.Formats.Add(format);
    }
    CCodecs &Codecs() override { return codecs; }
    HRESULT LoadHashProviders(CExternalCodecs &) override { return providerResult; }
    HRESULT ExtractGUI(CCodecs &, const CObjectVector<COpenType> &types, UStringVector &items,
                       UStringVector &fullPaths, const NWildcard::CCensorNode &,
                       CExtractOptions &options, bool show, bool &displayed,
                       CExtractCallbackImp &callback) override {
        ++guiCalls;
        paths = fullPaths;
        extraction = options;
        shown = show;
        multi = callback.MultiArcMode;
        hashType = !types.IsEmpty() && types[0].IsHashType;
        displayed = false;
        return items.IsEmpty() ? E_FAIL : guiResult;
    }
    HRESULT HashCalcGUI(const CExternalCodecs *, const NWildcard::CCensor &,
                        const CHashOptions &options, bool &displayed) override {
        ++guiCalls;
        method = QString::fromWCharArray(options.Methods[0]);
        displayed = false;
        return guiResult;
    }
    void ErrorMessageHRESULT(HRESULT result) override { reported = result; }
    HRESULT UpdateGUI(CCodecs &, const CObjectVector<COpenType> &, const UString &archive,
                      NWildcard::CCensor &censor, CUpdateOptions &options, bool show,
                      bool &displayed, CUpdateCallbackGUI &callback) override {
        ++guiCalls;
        name = QString::fromWCharArray(archive.Ptr());
        update = options;
        paths.Clear();
        FOR_VECTOR(i, censor.CensorPaths)
        paths.Add(censor.CensorPaths[i].Path);
        shown = show;
        displayed = displayedUpdate;
        if (failedUpdate)
            callback.FailedFiles.Add(FString("failed fixture"));
        return guiResult;
    }
};
UString us(const QString &path) { return UString(path.toStdWString().c_str()); }
bool writeFile(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write("fixture") == 7;
}
void saveFlags(const QString &path, UInt32 flags, bool cascaded = true) {
    CContextMenuInfo ci;
    ci.Load(path);
    ci.Flags = flags;
    ci.Flags_Def = true;
    ci.Cascaded.Val = cascaded;
    ci.Cascaded.Def = true;
    ci.ElimDup.Val = true;
    ci.ElimDup.Def = true;
    ci.WriteZone = 7;
    ci.Save(path);
}
} // namespace

class ShellContextTests final : public QObject {
    Q_OBJECT
  private slots:
    void fileManagerContextUsesSourcePathsAndPluginRange() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString fileName = QStringLiteral("данные 空 with space.txt");
        const QString folderName = QStringLiteral("folder 空");
        QVERIFY(writeFile(fixture.filePath(fileName)));
        QVERIFY(QDir(fixture.path()).mkdir(folderName));
        CMyComPtr<IFolderFolder> folder = new NFsFolder::CFSFolder(fixture.path());
        QCOMPARE(folder->LoadItems(), S_OK);
        CPanel panel(folder);
        panel.LoadFullPath();
        UInt32 numItems = 0;
        QCOMPARE(folder->GetNumberOfItems(&numItems), S_OK);
        QCOMPARE(numItems, UInt32(2));
        CRecordVector<UInt32> indices;
        for (UInt32 i = numItems; i > 0; --i)
            indices.Add(i - 1);
        UStringVector paths;
        paths.Add(L"stale path");
        panel.GetFilePaths(indices, paths);
        QCOMPARE(paths.Size(), unsigned(2));
        for (unsigned i = 0; i < indices.Size(); ++i)
            QCOMPARE(ToQString(paths[i]), ToQString(panel.GetItemFullPath(indices[i])));
        QStringList actualPaths;
        for (unsigned i = 0; i < paths.Size(); ++i)
            actualPaths.append(QDir::fromNativeSeparators(ToQString(paths[i])));
        QStringList expectedPaths{fixture.filePath(fileName), fixture.filePath(folderName)};
        actualPaths.sort();
        expectedPaths.sort();
        QCOMPARE(actualPaths, expectedPaths);
        const QString settings = fixture.filePath("settings.ini");
        saveFlags(settings, NContextMenuFlags::kTest | NContextMenuFlags::kCompressToZip);
        CLaunch launch;
        NQtShellMenu::CMenuHandle tree;
        std::unique_ptr<CZipContextMenu> context;
        panel.CreateSevenZipMenu(&tree, false, indices, panel.FindDir_InOperatedList(indices),
                                 context, settings, launch);
        QVERIFY(context);
        QVERIFY(!tree.Items.empty());
        for (const auto &entry : tree.Items) {
            QVERIFY(entry.ID >= unsigned(kMenuCmdID_Plugin_Start));
            QVERIFY(entry.ID < unsigned(kMenuCmdID_Plugin_Start + 400));
        }
        QVERIFY(context->FindVerb(UString(L"SevenZipTest")) < 0); // Directory selection.
        const int compress = context->FindVerb(UString(L"SevenZipCompressToZip"));
        QVERIFY(compress >= 0);
        QCOMPARE(context->InvokeCommandCommon(context->Commands()[unsigned(compress)]), S_OK);
        QCOMPARE(launch.operation, QString("compress"));
        QCOMPARE(launch.type, QString("zip"));
        QCOMPARE(launch.paths.Size(), paths.Size());
        for (unsigned i = 0; i < paths.Size(); ++i)
            QCOMPARE(ToQString(launch.paths[i]), ToQString(paths[i]));
        indices.Clear();
        for (UInt32 i = 0; i < numItems; ++i)
            if (ToQString(panel.GetItemName(i)) == fileName)
                indices.Add(i);
        QCOMPARE(indices.Size(), unsigned(1));
        saveFlags(settings, NContextMenuFlags::kTest);
        tree.Items.clear();
        panel.CreateSevenZipMenu(&tree, false, indices, -1, context, settings, launch);
        QVERIFY(context);
        QVERIFY(context->FindVerb(UString(L"SevenZipTest")) < 0);
        tree.Items.clear();
        panel.CreateSevenZipMenu(&tree, true, indices, -1, context, settings, launch);
        QVERIFY(context);
        QVERIFY(context->FindVerb(UString(L"SevenZipTest")) >= 0);
        QCOMPARE(context->InvokeCommand(unsigned(context->FindVerb(UString(L"SevenZipTest")))),
                 S_OK);
        QCOMPARE(launch.operation, QString("test"));
        indices.Clear();
        tree.Items.clear();
        panel.CreateSevenZipMenu(&tree, true, indices, -1, context, settings, launch);
        QVERIFY(context); // Upstream retains every successful query, including zero items.
        QVERIFY(tree.Items.empty());
        QCOMPARE(context->InvokeCommand(0u), E_INVALIDARG);
    }
    void sourceCompressionKeepsRawInputsNamingAndEmailPolicy() {
        CWorkflow service;
        UStringVector paths;
        paths.Add(UString(L"/fixture/данные 空 with spaces.bin"));
        paths.Add(UString(L"/fixture/literal*.bin"));
        QCOMPARE(::CompressFiles(UString(L"/output/"), UString(L"archive 空.7z"), UString(L"7z"),
                                 true, paths, true, false, false, service),
                 S_OK);
        QCOMPARE(service.guiCalls, 1);
        QCOMPARE(service.update.ArchivePath.GetFinalPath(), UString(L"/output/archive 空.7z.7z"));
        QVERIFY(service.update.EMailMode && !service.shown);
        QCOMPARE(service.paths.Size(), unsigned(2));
        QCOMPARE(service.paths[0], paths[0]);
        QCOMPARE(service.paths[1], paths[1]);
        QCOMPARE(::CompressFiles(UString(L"/output/"), UString(L"exact"), UString(L"7z"), false,
                                 paths, false, true, false, service),
                 S_OK);
        QCOMPARE(service.update.ArchivePath.GetFinalPath(), UString(L"/output/exact"));
        QVERIFY(!service.update.EMailMode && service.shown);
        QCOMPARE(::CompressFiles(UString(), UString(L"refused"), UString(L"read-only"), true, paths,
                                 false, false, false, service),
                 E_FAIL);
        QCOMPARE(service.guiCalls, 2);
        QCOMPARE(::CompressFiles(UString(), UString(L"refused"), UString(L"no handler"), true,
                                 paths, false, false, false, service),
                 E_FAIL);
        QCOMPARE(service.guiCalls, 2);
        service.guiResult = kAccessDenied;
        ::CompressFiles(UString(), UString(L"refused"), UString(L"7z"), true, paths, false, false,
                        false, service);
        QCOMPARE(service.reported, kAccessDenied);
        service.reported = S_OK;
        service.guiResult = E_ABORT;
        ::CompressFiles(UString(), UString(L"cancelled"), UString(L"7z"), true, paths, false, false,
                        false, service);
        QCOMPARE(service.reported, S_OK);
        service.guiResult = S_OK;
        service.failedUpdate = service.displayedUpdate = true;
        QCOMPARE(::CompressFiles(UString(), UString(L"failed input"), UString(L"7z"), true, paths,
                                 false, false, false, service),
                 E_FAIL);
    }
    void sourceInProcessGroupingPoliciesFailureAndCancel() {
        QTemporaryDir fixture;
        const QString a = fixture.filePath("а с пробелом.zip");
        const QString z = fixture.filePath("я 空.7z");
        QVERIFY(writeFile(a));
        QVERIFY(writeFile(z));
        UStringVector paths;
        paths.Add(us(z));
        paths.Add(us(a));
        CWorkflow service;
        QCOMPARE(::ExtractArchives(paths, us(fixture.filePath("output/*")), false, true,
                                   UInt32(NExtract::NZoneIdMode::kOffice), service),
                 S_OK);
        QCOMPARE(service.guiCalls, 1);
        QCOMPARE(service.paths.Size(), 2u);
        QCOMPARE(QDir::fromNativeSeparators(QString::fromWCharArray(service.paths[0].Ptr())), a);
        QCOMPARE(QDir::fromNativeSeparators(QString::fromWCharArray(service.paths[1].Ptr())), z);
        QVERIFY(service.multi && !service.shown);
        QVERIFY(service.extraction.ElimDup.Val && service.extraction.ElimDup.Def);
        QCOMPARE(service.extraction.OutDirMode, NExtractOutDirMode::k_ReplaceAsterisk);
        QCOMPARE(service.extraction.ZoneMode, NExtract::NZoneIdMode::kOffice);
        QCOMPARE(::TestArchives(paths, true, service), S_OK);
        QVERIFY(service.hashType && service.extraction.TestMode);
        service.guiResult = E_ABORT;
        QCOMPARE(::TestArchives(paths, false, service), E_ABORT);
        QCOMPARE(service.reported, S_OK);
        service.guiResult = kAccessDenied;
        QCOMPARE(::TestArchives(paths, false, service), kAccessDenied);
        QCOMPARE(service.reported, kAccessDenied);
        service.guiCalls = 0;
        UStringVector missing;
        missing.Add(us(fixture.filePath("missing.zip")));
        QCOMPARE(::TestArchives(missing, false, service), E_FAIL);
        QCOMPARE(service.guiCalls, 0);
    }
    void sourceInProcessHashProviders() {
        CWorkflow service;
        UStringVector paths;
        paths.Add(UString(L"/fixture/данные 空"));
        QCOMPARE(::CalcChecksum(paths, UString(L"SHA256"), service), S_OK);
        QCOMPARE(service.method, QString("SHA256"));
        service.providerResult = kAccessDenied;
        QCOMPARE(::CalcChecksum(paths, UString(L"CRC32"), service), kAccessDenied);
        QCOMPARE(service.guiCalls, 1);
        QCOMPARE(service.reported, kAccessDenied);
    }
    void flagsDirectoryGuardsAndExtendedVerbs() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString settings = fixture.filePath("settings.ini");
        saveFlags(settings, NContextMenuFlags::kExtractHere | NContextMenuFlags::kTest, false);
        CLaunch launch;
        CZipContextMenu menu(settings, &launch);
        const QString path = fixture.filePath("текст 空 с пробелом.txt");
        QVERIFY(writeFile(path));
        menu._fileNames.Add(us(path));
        NQtShellMenu::CMenuHandle tree;
        QCOMPARE(menu.QueryContextMenu(&tree, 0, 40, 400, 0), HRESULT(0));
        QVERIFY(menu.Commands().IsEmpty());
        QCOMPARE(menu.QueryContextMenu(&tree, 0, 40, 400, 0x100), HRESULT(2));
        QCOMPARE(menu.Commands()[0].CommandInternalID, CZipContextMenu::kExtractHere);
        QCOMPARE(menu.InvokeCommandCommon(menu.Commands()[0]), S_OK);
        QCOMPARE(launch.operation, QString("extract"));
        QCOMPARE(launch.paths[0], us(path));
        QVERIFY(!launch.dialog && !launch.elimDup);
        QCOMPARE(launch.zone, UInt32(7));
        const auto singleExtract = menu.Commands()[0];
        const QString second = fixture.filePath("second.zip");
        QVERIFY(writeFile(second));
        menu._fileNames.Add(us(second));
        NQtShellMenu::CMenuHandle multipleTree;
        QCOMPARE(menu.QueryContextMenu(&multipleTree, 0, 40, 400, 0x100), HRESULT(1));
        QCOMPARE(menu.Commands()[0].CommandInternalID, CZipContextMenu::kTest);
        launch.operation.clear();
        QCOMPARE(menu.InvokeCommandCommon(singleExtract), E_INVALIDARG);
        QVERIFY(launch.operation.isEmpty());
        QCOMPARE(menu.InvokeCommandCommon(menu.Commands()[0]), S_OK);
        QCOMPARE(launch.operation, QString("test"));
        QCOMPARE(launch.paths.Size(), 2u);
        menu._fileNames.DeleteBack();
        menu._attribs.FirstDirIndex = 0;
        NQtShellMenu::CMenuHandle directoryTree;
        QCOMPARE(menu.QueryContextMenu(&directoryTree, 0, 0, 400, 0x100), HRESULT(0));
        QCOMPARE(
            menu.QueryContextMenu(static_cast<NQtShellMenu::CMenuHandle *>(nullptr), 0, 400, 1, 0),
            E_INVALIDARG);
        menu._fileNames.Clear();
        QCOMPARE(
            menu.QueryContextMenu(static_cast<NQtShellMenu::CMenuHandle *>(nullptr), 0, 0, 400, 0),
            HRESULT(0));
    }
    void extractionNamingPoliciesAndVerbsKeepRawPaths() {
        QTemporaryDir fixture;
        const QString settings = fixture.filePath("settings.ini");
        saveFlags(settings, NContextMenuFlags::kExtractTo);
        CLaunch launch;
        CZipContextMenu menu(settings, &launch);
        const QString path = fixture.filePath("архив & 空.part01.rar");
        QVERIFY(writeFile(path));
        menu._fileNames.Add(us(path));
        NQtShellMenu::CMenuHandle tree;
        QCOMPARE(menu.QueryContextMenu(&tree, 0, 100, 500, 0), HRESULT(2));
        const int index = menu.FindVerb(UString(L"SevenZipExtractTo"));
        QVERIFY(index >= 0);
        const auto &command = menu.Commands()[unsigned(index)];
        QVERIFY(QString::fromWCharArray(command.UserString).contains("&&"));
        QCOMPARE(menu.InvokeCommandCommon(command), S_OK);
        QCOMPARE(launch.paths[0], us(path));
        QVERIFY(launch.elimDup && !launch.dialog);
        QCOMPARE(QDir::fromNativeSeparators(launch.folder), fixture.filePath("архив & 空") + '/');
        QCOMPARE(menu.FindVerb(UString(L"Unknown")), -1);
    }
    void nativeReducedDisplayRecomputesArchiveNameFromFullSelection() {
        QTemporaryDir fixture;
        const QString settings = fixture.filePath("settings.ini");
        saveFlags(settings, NContextMenuFlags::kCompressTo7z);
        CLaunch launch;
        CZipContextMenu menu(settings, &launch);
        menu.Init_For_Shell();
        for (int i = 0; i < 16; ++i) {
            const QString path = fixture.filePath(QString("файл с пробелом %1.txt").arg(i));
            QVERIFY(writeFile(path));
            menu._fileNames.Add(us(path));
        }
        const QString base = QFileInfo(fixture.path()).fileName();
        const QString collision = fixture.filePath(base + ".7z");
        QVERIFY(writeFile(collision));
        menu._fileNames.Add(us(collision));
        NQtShellMenu::CMenuHandle tree;
        QCOMPARE(menu.QueryContextMenu(&tree, 0, 0, 999, 0), HRESULT(2));
        const int index = menu.FindVerb(UString(L"SevenZipCompressTo7z"));
        QVERIFY(index >= 0);
        const auto &command = menu.Commands()[unsigned(index)];
        QVERIFY(QString::fromWCharArray(command.UserString).contains("_.7z"));
        QCOMPARE(QString::fromWCharArray(command.ArcName), base + ".7z");
        QCOMPARE(menu.InvokeCommandCommon(command), S_OK);
        QCOMPARE(launch.paths.Size(), unsigned(17));
        QCOMPARE(launch.name, base + "_2.7z");
        QCOMPARE(launch.type, QString("7z"));
        QVERIFY(!launch.dialog);
    }
    void checksumSubmenuExposesCalculationOnly() {
        QTemporaryDir fixture;
        const QString settings = fixture.filePath("settings.ini");
        saveFlags(settings, NContextMenuFlags::kCRC_Cascaded);
        CLaunch launch;
        CZipContextMenu menu(settings, &launch);
        const QString path = fixture.filePath("данные 空.txt");
        QVERIFY(writeFile(path));
        menu._fileNames.Add(us(path));
        NQtShellMenu::CMenuHandle tree;
        QVERIFY(menu.QueryContextMenu(&tree, 0, 0, 999, 0) > 0);
        QCOMPARE(tree.Items.size(), size_t(1));
        QVERIFY(tree.Items[0].SubMenu);
        QCOMPARE(tree.Items[0].SubMenu->Items.size(), size_t(1));
        const int calc = menu.FindVerb(UString(L"SevenZip.Checksum.Calc.SHA256"));
        QVERIFY(calc >= 0);
        QCOMPARE(menu.InvokeCommandCommon(menu.Commands()[unsigned(calc)]), S_OK);
        QCOMPARE(launch.operation, QString("hash"));
        QCOMPARE(launch.method, QString("SHA256"));
        QVERIFY(launch.name.isEmpty());
        const int generate = menu.FindVerb(UString(L"SevenZip.Checksum.Generate.SHA256"));
        QCOMPARE(generate, -1);
        const int test = menu.FindVerb(UString(L"SevenZip.Checksum.Test.Hash"));
        QCOMPARE(test, -1);
    }
};
QTEST_MAIN(ShellContextTests)
#include "shell_context_tests.moc"
