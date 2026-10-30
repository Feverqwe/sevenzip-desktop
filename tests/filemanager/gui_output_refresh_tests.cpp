#include "filemanager/window/qtguioutputrefresh.h"
#include <QEventLoop>
#include <QtTest>

namespace {
QStringList Sorted(QStringList paths) {
    paths.sort();
    return paths;
}
} // namespace

class GuiOutputRefreshTests final : public QObject {
    Q_OBJECT
  private slots:
    void overlappingChildrenWaitAndReloadEachPanelOnce() {
        bool ready = false;
        unsigned attempts = 0;
        QList<unsigned> panels;
        QList<QtGuiOutputRefresh::Outputs> batches;
        QtGuiOutputRefresh refresh(
            [&] {
                ++attempts;
                return ready;
            },
            [&](unsigned panel, const auto &output) {
                panels.append(panel);
                batches.append(output);
            });
        refresh.Enqueue({"output 空/one"}, {"output 空/archive.zip"});
        refresh.Enqueue({"output 空/two", "output 空/one"}, {"output 空/archive.zip"});
        QTRY_VERIFY(attempts > 0);
        QVERIFY(panels.isEmpty());
        ready = true;
        QTRY_COMPARE(panels.size(), 2);
        QCOMPARE(panels.count(0), 1);
        QCOMPARE(panels.count(1), 1);
        for (const auto &output : batches) {
            QCOMPARE(Sorted(output.directories), QStringList({"output 空/one", "output 空/two"}));
            QCOMPARE(output.archives, QStringList({"output 空/archive.zip"}));
        }
    }
    void firstPanelScanRetainsPeerAndNewCompletions() {
        bool ready = true;
        QList<unsigned> panels;
        QList<QtGuiOutputRefresh::Outputs> batches;
        QtGuiOutputRefresh refresh([&] { return ready; },
                                   [&](unsigned panel, const auto &output) {
                                       panels.append(panel);
                                       batches.append(output);
                                       ready = false;
                                   });
        refresh.Enqueue({"first output"}, {});
        QTRY_COMPARE(panels.size(), 1);
        refresh.Enqueue({"second output"}, {});
        ready = true;
        QTRY_COMPARE(panels.size(), 2);
        ready = true;
        QTRY_COMPARE(panels.size(), 3);
        const unsigned firstPanel = panels.first();
        QCOMPARE(panels.count(firstPanel), 2);
        QCOMPARE(panels.count(1 - firstPanel), 1);
        for (int i = 1; i < panels.size(); ++i)
            QCOMPARE(Sorted(batches[i].directories),
                     panels[i] == firstPanel ? QStringList({"second output"})
                                             : QStringList({"first output", "second output"}));
    }
    void completionDuringReloadSurvivesNestedEvents() {
        QList<unsigned> panels;
        QList<QtGuiOutputRefresh::Outputs> batches;
        QtGuiOutputRefresh *owner = nullptr;
        QtGuiOutputRefresh refresh([] { return true; },
                                   [&](unsigned panel, const auto &output) {
                                       panels.append(panel);
                                       batches.append(output);
                                       if (panels.size() != 1)
                                           return;
                                       owner->Enqueue({"new output"}, {});
                                       QEventLoop nested;
                                       QTimer::singleShot(120, &nested, &QEventLoop::quit);
                                       nested.exec();
                                       QCOMPARE(panels.size(), 1);
                                   });
        owner = &refresh;
        refresh.Enqueue({"old output"}, {});
        QTRY_COMPARE(panels.size(), 3);
        const unsigned firstPanel = panels.first();
        QCOMPARE(panels.count(firstPanel), 2);
        QCOMPARE(panels.count(1 - firstPanel), 1);
        for (int i = 1; i < panels.size(); ++i)
            QCOMPARE(Sorted(batches[i].directories),
                     panels[i] == firstPanel ? QStringList({"new output"})
                                             : QStringList({"new output", "old output"}));
    }
    void shutdownDuringReloadDiscardsPeerAndLaterResults() {
        unsigned calls = 0;
        QtGuiOutputRefresh *owner = nullptr;
        QtGuiOutputRefresh refresh([] { return true; },
                                   [&](unsigned, const auto &) {
                                       ++calls;
                                       owner->Stop();
                                       owner->Enqueue({"late child output"}, {});
                                   });
        owner = &refresh;
        refresh.Enqueue({"pending output"}, {});
        QTRY_COMPARE(calls, 1u);
        QTest::qWait(120);
        QCOMPARE(calls, 1u);
    }
};

QTEST_GUILESS_MAIN(GuiOutputRefreshTests)
#include "gui_output_refresh_tests.moc"
