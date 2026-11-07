#include "7zip/UI/FileManager/resource.h"
#include "Common/StringConvert.h"
#include "filemanager/MyLoadMenu.h"
#include "filemanager/qtfilemenuservices.h"
#include "fileops/native/Dialog.h"
#include <QtTest>

namespace {
class Menu final : public IQtFileMenuServices {
  public:
    QList<QtFileMenuItem> source;
    QList<QtFileMenuItem> items;
    QList<bool> enabled;
    bool large = true;
    bool IsDialogSizeOK(unsigned, unsigned) const override { return large; }
    bool GetItem(unsigned index, QtFileMenuItem &item) const override {
        if (index >= unsigned(source.size()))
            return false;
        item = source[index];
        return true;
    }
    void CopyPopMenu_IfRequired(QtFileMenuItem &) override {}
    bool InsertItem(unsigned position, const QtFileMenuItem &item) override {
        items.insert(position, item);
        enabled.insert(position, true);
        return true;
    }
    void EnableItem(unsigned position, bool value) override { enabled[position] = value; }
    void RemoveAllItemsFrom(unsigned position) override {
        items.resize(position);
        enabled.resize(position);
    }
    QList<unsigned> ids() const {
        QList<unsigned> result;
        for (const auto &item : items)
            result << item.wID;
        return result;
    }
    QList<unsigned> disabled() const {
        QList<unsigned> result;
        for (qsizetype i = 0; i < items.size(); ++i)
            if (!enabled[i])
                result << items[i].wID;
        return result;
    }
};

Menu commands(std::initializer_list<unsigned> ids) {
    Menu menu;
    for (unsigned id : ids)
        menu.source.append({id, UString(L"command"), id == 0});
    return menu;
}
} // namespace

class FileMenuPolicyTests : public QObject {
    Q_OBJECT
  private slots:
    void dialogSizeUsesWorkAreaAndSourceDialogUnits() {
        class Screen final : public IQtDialogScreenServices {
          public:
            QRect area{0, 0, 600, 400};
            QSize GetDialogBaseUnits() const override { return {8, 16}; }
            QRect GetWorkAreaRect() const override { return area; }
        } screen;
        QVERIFY(NWindows::NControl::IsDialogSizeOK(300, 200, screen));
        screen.area.setWidth(599);
        QVERIFY(!NWindows::NControl::IsDialogSizeOK(300, 200, screen));
        screen.area = QRect(0, 0, 600, 399);
        QVERIFY(!NWindows::NControl::IsDialogSizeOK(300, 200, screen));
    }
    void readOnlyKeepsReadCommandsAndDisablesMutation() {
        auto menu = commands({IDM_OPEN, IDM_RENAME, IDM_COPY_TO, IDM_MOVE_TO, IDM_DELETE,
                              IDM_COMMENT, IDM_CREATE_FOLDER, IDM_PROPERTIES});
        CFileMenu policy;
        policy.readOnly = true;
        policy.Load(menu, 0);
        QCOMPARE(menu.items.size(), menu.source.size());
        QCOMPARE(menu.disabled(), QList<unsigned>({IDM_RENAME, IDM_MOVE_TO, IDM_DELETE, IDM_COMMENT,
                                                   IDM_CREATE_FOLDER}));
    }
    void compactContextOmitsUnavailableAndTrailingSeparators() {
        auto menu = commands({8, IDM_RENAME, 0, IDM_PROPERTIES, 0});
        menu.large = false;
        CFileMenu policy;
        policy.readOnly = true;
        policy.Load(menu, 0);
        QCOMPARE(menu.ids(), QList<unsigned>({IDM_PROPERTIES}));
        QVERIFY(menu.disabled().isEmpty());
    }
    void hashFolderRetainsPropertiesHashesRenameDelete() {
        auto menu = commands({IDM_OPEN, IDM_COPY_TO, IDM_FILE_EDIT, IDM_CREATE_FOLDER,
                              IDM_PROPERTIES, IDM_CRC32, IDM_RENAME, IDM_DELETE});
        CFileMenu policy;
        policy.isHashFolder = true;
        policy.Load(menu, 0);
        QCOMPARE(menu.disabled(),
                 QList<unsigned>({IDM_OPEN, IDM_COPY_TO, IDM_FILE_EDIT, IDM_CREATE_FOLDER}));
    }
    void removedCommandsStayOutOfFileMenu() {
        auto menu = commands({IDM_DIFF, IDM_VER_EDIT, IDM_VER_COMMIT, IDM_VER_REVERT, IDM_VER_DIFF,
                              IDM_BENCHMARK2, IDM_SPLIT, IDM_COMBINE, IDM_CREATE_FILE,
                              IDM_ALT_STREAMS, IDM_PROPERTIES});
        CFileMenu policy;
        policy.isFsFolder = policy.allAreFiles = true;
        policy.numItems = 1;
        policy.Load(menu, 0);
        QCOMPARE(menu.ids(), QList<unsigned>{IDM_PROPERTIES});
    }
};

QTEST_GUILESS_MAIN(FileMenuPolicyTests)
#include "file_menu_policy_tests.moc"
