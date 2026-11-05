#include "Common/MyCom.h"
#include "filemanager/providers/RootFolder.h"
#include "filemanager/providers/qtfolderprojection.h"
#include <QtTest>
#include <optional>

class FolderProjectionTests final : public QObject {
    Q_OBJECT
  private slots:
    void joinedResult_data() {
        QTest::addColumn<int>("outcome");
        QTest::newRow("complete") << 0;
        QTest::newRow("entry-error") << 1;
        QTest::newRow("cancel") << 2;
    }
    void joinedResult() {
        QFETCH(int, outcome);
        auto *root = new CRootFolder;
        root->Init();
        CMyComPtr<IFolderFolder> source = root;
        QtFolderProjection projection;
        int appended = 0;
        std::optional<HRESULT> published;
        projection.start(
            source, 96,
            [&](quint32 index) {
                ++appended;
                if (index == 17 && outcome == 2)
                    projection.cancel();
                return index == 65 && outcome == 1 ? E_FAIL : S_OK;
            },
            [&](HRESULT result) { published = result; });
        QCOMPARE(projection.waitForFinished(), outcome == 2   ? E_ABORT
                                               : outcome == 1 ? E_FAIL
                                                              : S_OK);
        QVERIFY(!projection.active());
        if (outcome == 2)
            QVERIFY(!published);
        else {
            QVERIFY(published);
            QCOMPARE(*published, outcome == 1 ? E_FAIL : S_OK);
            QCOMPARE(appended, outcome == 1 ? 66 : 96);
        }
    }
};

QTEST_MAIN(FolderProjectionTests)
#include "folder_projection_tests.moc"
