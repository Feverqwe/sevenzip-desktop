#include "filemanager/App.h"
#include "filemanager/panels/qtpaneldragmodel.h"
#include "filemanager/providers/FSFolder.h"
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace {
class Compression final : public IQtPanelDragServices {
  public:
    UString folder;
    bool wait = false;
    int calls = 0;
    HRESULT DoDragDrop(CDataObject &, CDropSource &, DWORD, DWORD &) override { return E_ABORT; }
    HRESULT CompressFiles(CPanel &, const UString &path, const UString &, const UStringVector &,
                          bool waitFinish) override {
        ++calls;
        folder = path;
        wait = waitFinish;
        return S_OK;
    }
};
class Target final : public IQtDropTargetPresentation {
  public:
    unsigned pane = 0;
    int menus = 0;
    int PanelAtPoint(int, int) const override { return int(pane); }
    bool PanelEnabled(unsigned) const override { return true; }
    int FolderAtPoint(unsigned, int, int, UString &) const override { return -1; }
    void Highlight(unsigned, int, bool) override {}
    bool IsSameDrive(const UString &, const UStringVector &) const override { return true; }
    UInt32 Drag_OnContextMenu(int, int, UInt32, DWORD) override {
        ++menus;
        return NDragMenu::k_Cancel;
    }
};
void BindDisk(CPanel &panel, const QString &path) {
    panel.SetNewFolder(new NFsFolder::CFSFolder(path));
    panel.LoadFullPath();
}
} // namespace

class DragProtocolTests final : public QObject {
    Q_OBJECT
  private slots:
    void sourceButtonFlagsAndNativeButtonsChooseMenu_data() {
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<DWORD>("keys");
        QTest::addColumn<int>("source");
        for (unsigned pane : {0u, 1u})
            for (DWORD keys : {DWORD(1), DWORD(2), DWORD(16)})
                for (int source : {0, 1, 2})
                    QTest::newRow(qPrintable(QString("%1-%2-%3").arg(pane).arg(keys).arg(source)))
                        << pane << keys << source;
    }
    void sourceButtonFlagsAndNativeButtonsChooseMenu() {
        QFETCH(unsigned, pane);
        QFETCH(DWORD, keys);
        QFETCH(int, source);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QFile file(fixture.filePath(QStringLiteral("protected файл δ.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("protocol control"), qint64(16));
        file.close();
        Target view;
        view.pane = pane;
        Compression compression;
        CApp app;
        BindDisk(app.Panels[pane], fixture.path());
        app.Panels[pane].AttachDragServices(&compression);
        auto &target = app.DropTarget();
        target.AttachPresentation(&view);
        CDataObject data;
        data.Names.Add(ToUString(QDir::toNativeSeparators(file.fileName())));
        if (source) {
            data.Panel = &app.Panels[1 - pane];
            data.IsRightButton = source == 2;
        }
        DWORD effect = DROPEFFECT_COPY | DROPEFFECT_MOVE;
        QCOMPARE(target.DragEnter(&data, keys, 0, 0, &effect), S_OK);
        effect = DROPEFFECT_COPY | DROPEFFECT_MOVE;
        QCOMPARE(target.Drop(&data, 0, 0, 0, &effect), S_OK);
        const bool menu = source ? source == 2 : keys == 2 || keys == 16;
        QCOMPARE(view.menus, menu ? 1 : 0);
        if (menu) {
            QCOMPARE(effect, DWORD(DROPEFFECT_NONE));
            QCOMPARE(compression.calls, 0);
        }
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("protocol control"));
    }

    void temporaryArchiveSourceUsesSourceRootAndWaitPolicy_data() {
        QTest::addColumn<UInt32>("flags");
        QTest::addColumn<bool>("wait");
        QTest::newRow("temp-fallback") << UInt32(0) << true;
        QTest::newRow("explicit-no-wait") << UInt32(k_SourceFlags_DoNotWaitFinish) << false;
        QTest::newRow("wait-precedence")
            << UInt32(k_SourceFlags_WaitFinish | k_SourceFlags_DoNotWaitFinish) << true;
    }
    void temporaryArchiveSourceUsesSourceRootAndWaitPolicy() {
        QFETCH(UInt32, flags);
        QFETCH(bool, wait);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QFile file(fixture.filePath(QStringLiteral("временный файл δ.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("protocol control"), qint64(16));
        file.close();
        Compression compression;
        CPanel panel;
        panel.AttachDragServices(&compression);
        UStringVector paths;
        paths.Add(ToUString(QDir::toNativeSeparators(file.fileName())));
        UInt32 targetFlags = 0;
        QCOMPARE(panel.CompressDropFiles(paths, {}, true, false, flags, targetFlags), S_OK);
        QCOMPARE(compression.calls, 1);
#ifdef _WIN32
        QCOMPARE(ToQString(compression.folder), QStringLiteral("C:\\"));
#else
        QCOMPARE(ToQString(compression.folder), QStringLiteral("/"));
#endif
        QCOMPARE(compression.wait, wait);
        QCOMPARE(targetFlags,
                 UInt32(wait ? k_TargetFlags_WaitFinish : k_TargetFlags_DoNotWaitFinish));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("protocol control"));
    }
};
QTEST_MAIN(DragProtocolTests)
#include "drag_protocol_tests.moc"
