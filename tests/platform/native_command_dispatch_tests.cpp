#include "7zip/UI/FileManager/resource.h"
#include "filemanager/App.h"
#include "filemanager/qtfilemanagercommandservices.h"
#include <QtTest>

namespace {
class Commands final : public IQtFileManagerCommandServices {
  public:
    bool allowed = true, shift = false, recycle = false;
    QStringList calls;
    QString parameter;
    bool CanExecuteCommand(unsigned) override { return allowed; }
    bool IsShiftDown() const override { return shift; }
    void InvokePluginCommand(unsigned id) override {
        calls << "plugin";
        parameter = QString::number(id);
    }
    void ReleasePluginMenus() override { calls << "release-menus"; }
    void OpenItemInside(const wchar_t *type) override {
        calls << "inside";
        parameter = type ? QString::fromWCharArray(type) : QStringLiteral("default");
    }
    void EditItem(bool editor) override { calls << (editor ? "edit" : "view"); }
    void Delete(bool toRecycleBin) override {
        calls << "delete";
        recycle = toRecycleBin;
    }
    void CalculateCrc(const char *method) override {
        calls << "hash";
        parameter = QString::fromLatin1(method);
    }
#define RECORD_COMMAND(name)                                                                       \
    void name() override { calls << #name; }
    RECORD_COMMAND(OpenItem)
    RECORD_COMMAND(OpenItemOutside)
    RECORD_COMMAND(Rename)
    RECORD_COMMAND(CopyTo)
    RECORD_COMMAND(MoveTo)
    RECORD_COMMAND(Split)
    RECORD_COMMAND(Combine)
    RECORD_COMMAND(Properties)
    RECORD_COMMAND(Comment)
    RECORD_COMMAND(CreateFolder)
    RECORD_COMMAND(CreateFile)
    RECORD_COMMAND(Link)
    RECORD_COMMAND(OpenAltStreams)
#undef RECORD_COMMAND
};
} // namespace

class NativeCommandDispatchTests : public QObject {
    Q_OBJECT
  private slots:
    void changedCapabilityBlocksEveryRouteBeforeSideEffects() {
        Commands commands;
        commands.allowed = false;
        for (unsigned id : {unsigned(IDM_OPEN), unsigned(IDM_DELETE), unsigned(IDM_SHA256),
                            unsigned(kMenuCmdID_Plugin_Start)})
            QVERIFY(!ExecuteFileCommand(id, commands));
        QVERIFY(commands.calls.isEmpty());
    }
    void sourceInternalTypesAndHashNamesStayTyped() {
        Commands commands;
        for (auto command : {std::pair<unsigned, QString>{IDM_OPEN_INSIDE, "default"},
                             {IDM_OPEN_INSIDE_ONE, "*"},
                             {IDM_OPEN_INSIDE_PARSER, "#"}}) {
            commands.calls.clear();
            QVERIFY(ExecuteFileCommand(command.first, commands));
            QCOMPARE(commands.calls, QStringList({"inside"}));
            QCOMPARE(commands.parameter, command.second);
        }
        for (auto command : {std::pair<unsigned, QString>{IDM_HASH_ALL, "*"},
                             {IDM_CRC32, "CRC32"},
                             {IDM_CRC64, "CRC64"},
                             {IDM_XXH64, "XXH64"},
                             {IDM_MD5, "MD5"},
                             {IDM_SHA1, "SHA1"},
                             {IDM_SHA256, "SHA256"},
                             {IDM_SHA384, "SHA384"},
                             {IDM_SHA512, "SHA512"},
                             {IDM_SHA3_256, "SHA3-256"},
                             {IDM_BLAKE2SP, "BLAKE2sp"}}) {
            commands.calls.clear();
            QVERIFY(ExecuteFileCommand(command.first, commands));
            QCOMPARE(commands.calls, QStringList({"hash"}));
            QCOMPARE(commands.parameter, command.second);
        }
    }
    void sourceDeleteShiftFlagAndPluginReleaseOrder() {
        Commands commands;
        QVERIFY(ExecuteFileCommand(IDM_DELETE, commands));
        QVERIFY(commands.recycle);
        commands.shift = true;
        QVERIFY(ExecuteFileCommand(IDM_DELETE, commands));
        QVERIFY(!commands.recycle);
        commands.calls.clear();
        QVERIFY(ExecuteFileCommand(kMenuCmdID_Plugin_Start + 7, commands));
        QCOMPARE(commands.calls, QStringList({"plugin", "release-menus"}));
        QCOMPARE(commands.parameter, QString::number(kMenuCmdID_Plugin_Start + 7));
        commands.calls.clear();
        for (unsigned id :
             {999u, unsigned(IDM_DIFF), unsigned(IDM_VER_EDIT), unsigned(IDM_VER_COMMIT),
              unsigned(IDM_VER_REVERT), unsigned(IDM_VER_DIFF)})
            QVERIFY(!ExecuteFileCommand(id, commands));
        QVERIFY(commands.calls.isEmpty());
    }
};

QTEST_GUILESS_MAIN(NativeCommandDispatchTests)
#include "native_command_dispatch_tests.moc"
