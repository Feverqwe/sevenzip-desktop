#include "Common/MyTypes.h"

#include "7zip/UI/Explorer/ContextMenuFlags.h"
#include "Common/StringConvert.h"
#include "platform/shell/ContextMenu.h"
#include "settings/MenuPage.h"
#include <QCheckBox>
#include <QFile>
#include <QListWidget>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>

namespace {
class ExtractionReceiver final : public IQtContextMenuServices {
  public:
    bool called = false, eliminate = true;
    UInt32 zone = 0;
    UStringVector received;
    void Open(const UString &, const UString &) override {}
    void ExtractArchives(const UStringVector &paths, const UString &, bool, bool elimDup,
                         UInt32 writeZone) override {
        called = true;
        eliminate = elimDup;
        zone = writeZone;
        received = paths;
    }
    void TestArchives(const UStringVector &, bool) override {}
    void CompressFiles(const UString &, const UString &, const UString &, bool,
                       const UStringVector &, bool, bool, bool) override {}
    void CalcChecksum(const UStringVector &, const UString &) override {}
    void ShowErrorMessage(const UString &) override {}
    void ShowErrorMessageRes(UInt32) override {}
};
void ChooseCommands(CMenuPage &page, UInt32 flags) {
    auto *items = page.findChild<QListWidget *>("menuItems");
    for (int i = 0; i < items->count(); ++i) {
        auto *item = items->item(i);
        item->setCheckState(item->data(Qt::UserRole).toUInt() & flags ? Qt::Checked
                                                                      : Qt::Unchecked);
    }
}
} // namespace

class MenuPolicyTests final : public QObject {
    Q_OBJECT
  private slots:
    void appliedPageChangesSourceQueryAndInvocation_data() {
        QTest::addColumn<bool>("cascaded");
        QTest::addColumn<bool>("icons");
        QTest::newRow("flat-no-icons") << false << false;
        QTest::newRow("flat-icons") << false << true;
        QTest::newRow("cascade-no-icons") << true << false;
        QTest::newRow("cascade-icons") << true << true;
    }
    void appliedPageChangesSourceQueryAndInvocation() {
        QFETCH(bool, cascaded);
        QFETCH(bool, icons);
        QTemporaryDir fixture;
        const QString settings = fixture.filePath("options 空.ini");
        CMenuPage page(settings);
        QVERIFY(page.findChild<QListWidget *>("menuItems")->isEnabled());
        ChooseCommands(page, NContextMenuFlags::kExtractTo);
        for (const auto &entry : {qMakePair("cascadedMenu", cascaded),
                                  qMakePair("menuIcons", icons), qMakePair("menuElimDup", false)}) {
            auto *check = page.findChild<QCheckBox *>(entry.first);
            if (check->isChecked() != entry.second)
                check->click();
        }
        QVERIFY(page.IsModified());
        QCOMPARE(page.OnApply(), 0L);
        QVERIFY(!page.IsModified());
        QFile file(fixture.filePath("архив 空 with spaces.zip"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("fixture"), qint64(7));
        file.close();
        ExtractionReceiver receiver;
        CZipContextMenu context(settings, &receiver);
        context._fileNames.Add(UString(file.fileName().toStdWString().c_str()));
        NQtShellMenu::CMenuHandle menu;
        QVERIFY(context.QueryContextMenu(&menu, 0, 40, 999, 0) > 0);
        QVERIFY(!menu.Items.empty());
        QCOMPARE(bool(menu.Items.front().SubMenu), cascaded);
        const auto &commands = cascaded ? menu.Items.front().SubMenu->Items : menu.Items;
        auto command = std::find_if(commands.begin(), commands.end(), [](const auto &entry) {
            return entry.Type != NQtShellMenu::kSeparator;
        });
        QVERIFY(command != commands.end());
        QCOMPARE(command->Bitmap != 0, icons);
        const int extract = context.FindVerb(UString(L"SevenZipExtractTo"));
        QVERIFY(extract >= 0);
        QCOMPARE(context.FindVerb(UString(L"SevenZipTest")), -1);
        QCOMPARE(context.FindVerb(UString(L"SevenZipCompress")), -1);
        QCOMPARE(context.InvokeCommand(unsigned(extract)), S_OK);
        QVERIFY(receiver.called && !receiver.eliminate);
        QCOMPARE(receiver.zone, UInt32(-1));
        QCOMPARE(receiver.received.Size(), unsigned(1));
        QCOMPARE(receiver.received[0], context._fileNames[0]);
        {
            CMenuPage cancel(settings);
            ChooseCommands(cancel, NContextMenuFlags::kTest);
            QVERIFY(cancel.IsModified());
        }
        CContextMenuInfo persisted;
        persisted.Load(settings);
        QCOMPARE(persisted.Flags, UInt32(NContextMenuFlags::kExtractTo));
        QCOMPARE(persisted.Cascaded.Val, cascaded);
        QCOMPARE(persisted.MenuIcons.Val, icons);
    }
};
QTEST_MAIN(MenuPolicyTests)
#include "menu_policy_tests.moc"
