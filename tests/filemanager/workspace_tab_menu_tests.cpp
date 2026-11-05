#include "filemanager/support/window_state_support.h"
#include "filemanager/window/qtwindowworkspace.h"
#include "filemanager/window/qtworkspaceapplication.h"
#include "support/window_activation.h"
#include <QMenu>
#include <QScopeGuard>
#include <QTabBar>
#include <QTimer>

class WorkspaceTabMenuTests final : public QObject {
    Q_OBJECT
    static QtWindowWorkspace *workspace(MainWindow &window) {
        for (auto *object : window.children())
            if (auto *owner = dynamic_cast<QtWindowWorkspace *>(object))
                return owner;
        return nullptr;
    }
    static bool contains(QTabBar &bar, const QString &id) {
        for (int index = 0; index < bar.count(); ++index)
            if (bar.tabData(index).toString() == id)
                return true;
        return false;
    }

  private slots:
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }
    void coldTabTransferPreservesActiveSource_data() {
        QTest::addColumn<bool>("detach");
        QTest::addColumn<bool>("navigateReceiver");
        QTest::newRow("other-panel") << false << false;
        QTest::newRow("new-window-menu") << true << false;
        QTest::newRow("new-window-preserves-navigation") << true << true;
    }
    void coldTabTransferPreservesActiveSource() {
        QFETCH(bool, detach);
        QFETCH(bool, navigateReceiver);
        const bool quit = qApp->quitOnLastWindowClosed();
        qApp->setQuitOnLastWindowClosed(false);
        const auto restoreQuit = qScopeGuard([quit] { qApp->setQuitOnLastWindowClosed(quit); });
        QtWorkspaceApplication application(*qApp);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QStringList paths;
        for (const auto &name : {QString("A folder"), QString("B папка"), QString("C folder")}) {
            paths.append(fixture.filePath(name));
            QVERIFY(QDir().mkpath(paths.last()));
        }
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        const auto closeUnexpectedWindows = qScopeGuard([&] {
            for (auto *widget : QApplication::topLevelWidgets())
                if (auto *other = dynamic_cast<MainWindow *>(widget); other && other != &window)
                    other->close();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        });
        auto *owner = workspace(window);
        QVERIFY(owner);
        window.openPath(paths[0]);
        auto *bar = window.findChild<QTabBar *>("panelTabs0");
        for (int i = 1; i < paths.size(); ++i) {
            owner->NewTab(0, paths[i]);
            QTRY_COMPARE(QDir::cleanPath(owner->CurrentLocation()), paths[i]);
        }
        bar->setCurrentIndex(0);
        QTRY_COMPARE(QDir::cleanPath(owner->CurrentLocation()), paths[0]);
        bool restored = false;
        owner->Restore(owner->Save(), [&] { restored = true; });
        QTRY_VERIFY(restored);
        const QString firstId = bar->tabData(0).toString();
        const QString lastId = bar->tabData(2).toString();
        if (detach) {
            bool navigated = false;
            QTimer navigate;
            connect(&navigate, &QTimer::timeout, &window, [&] {
                if (!navigateReceiver || QDir::cleanPath(owner->CurrentLocation()) != paths[2])
                    return;
                for (auto *widget : QApplication::topLevelWidgets())
                    if (auto *other = dynamic_cast<MainWindow *>(widget);
                        other && other != &window && other->isVisible()) {
                        navigated = true;
                        navigate.stop();
                        other->openPath(paths[1]);
                        return;
                    }
            });
            if (navigateReceiver)
                navigate.start(1);
            QTimer answer;
            bool selected = false;
            connect(&answer, &QTimer::timeout, &window, [&] {
                auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
                if (!menu)
                    return;
                answer.stop();
                for (auto *action : menu->actions())
                    if (action->text() ==
                        QCoreApplication::translate("QtWindowWorkspace", "Move to new window")) {
                        selected = true;
                        QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
                                          menu->actionGeometry(action).center());
                        return;
                    }
                menu->close();
            });
            answer.start(5);
            QVERIFY(QMetaObject::invokeMethod(bar, "customContextMenuRequested",
                                              Qt::DirectConnection,
                                              Q_ARG(QPoint, bar->tabRect(2).center())));
            QVERIFY(selected);
            QTRY_COMPARE(bar->count(), 2);
            QCOMPARE(bar->tabData(bar->currentIndex()).toString(), firstId);
            MainWindow *receiver = nullptr;
            for (auto *widget : QApplication::topLevelWidgets())
                if (auto *other = dynamic_cast<MainWindow *>(widget);
                    other && other != &window && other->isVisible())
                    receiver = other;
            QVERIFY(receiver);
            auto *destination = receiver->findChild<QTabBar *>("panelTabs0");
            QTRY_COMPARE(destination->count(), navigateReceiver ? 2 : 1);
            QCOMPARE(destination->tabData(destination->currentIndex()).toString(), lastId);
            QTRY_COMPARE(QDir::cleanPath(workspace(*receiver)->CurrentLocation()), paths[2]);
            QCOMPARE(QDir::cleanPath(owner->CurrentLocation()), paths[0]);
            if (navigateReceiver) {
                QVERIFY(navigated);
                destination->setCurrentIndex(0);
                QTRY_COMPARE(QDir::cleanPath(workspace(*receiver)->CurrentLocation()), paths[1]);
            }
            return;
        }
        auto *twoPanels = window.findChild<QAction *>("twoPanelsAction");
        QTRY_VERIFY(twoPanels->isEnabled());
        twoPanels->trigger();
        QTRY_VERIFY(twoPanels->isChecked());
        QTRY_VERIFY(twoPanels->isEnabled());
        bool completed = false;
        bool moved = false;
        QVERIFY(owner->TransferTab(lastId, *owner, 1, [&](bool success) {
            moved = success;
            completed = true;
        }));
        QTRY_VERIFY(completed);
        QVERIFY(moved);
        QCOMPARE(bar->tabData(bar->currentIndex()).toString(), firstId);
        QCOMPARE(bar->count(), 2);
        auto *destination = window.findChild<QTabBar *>("panelTabs1");
        QVERIFY(destination);
        QCOMPARE(destination->tabData(destination->currentIndex()).toString(), lastId);
        QCOMPARE(QDir::cleanPath(owner->CurrentLocation()), paths[2]);
        bar->setCurrentIndex(1);
        bar->setCurrentIndex(0);
        QTRY_COMPARE(QDir::cleanPath(owner->CurrentLocation()), paths[0]);
    }
    void destroyingSourceCompletesPendingTransferOnce() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString a = fixture.filePath("A folder");
        const QString b = fixture.filePath("B папка");
        QVERIFY(QDir().mkpath(a));
        QVERIFY(QDir().mkpath(b));
        ControlledBackend sourceBackend, targetBackend;
        auto source = std::make_unique<MainWindow>(&sourceBackend);
        MainWindow target(&targetBackend);
        source->show();
        target.show();
        auto *owner = workspace(*source);
        auto *receiver = workspace(target);
        source->openPath(a);
        QTRY_COMPARE(QDir::cleanPath(owner->CurrentLocation()), a);
        owner->NewTab(0, b);
        QTRY_COMPARE(QDir::cleanPath(owner->CurrentLocation()), b);
        auto *bar = source->findChild<QTabBar *>("panelTabs0");
        bar->setCurrentIndex(0);
        QTRY_COMPARE(QDir::cleanPath(owner->CurrentLocation()), a);
        bool restored = false;
        owner->Restore(owner->Save(), [&] { restored = true; });
        QTRY_VERIFY(restored);
        const QString id = bar->tabData(1).toString();
        int completions = 0;
        bool result = true;
        QVERIFY(owner->TransferTab(id, *receiver, 0, [&](bool moved) {
            ++completions;
            result = moved;
        }));
        QCOMPARE(completions, 0);
        source.reset();
        QTRY_COMPARE(completions, 1);
        QVERIFY(!result);
        QCOMPARE(target.findChild<QTabBar *>("panelTabs0")->count(), 1);
    }
    void removedTabDoesNotLeaveDetachedWindow_data() {
        QTest::addColumn<bool>("twoPanels");
        QTest::newRow("single-panel-startup") << false;
        QTest::newRow("two-panel-startup") << true;
    }
    void removedTabDoesNotLeaveDetachedWindow() {
        QFETCH(bool, twoPanels);
        const bool quit = qApp->quitOnLastWindowClosed();
        qApp->setQuitOnLastWindowClosed(false);
        const auto restoreQuit = qScopeGuard([quit] { qApp->setQuitOnLastWindowClosed(quit); });
        if (twoPanels) {
            ControlledBackend seedBackend;
            MainWindow seed(&seedBackend);
            seed.show();
            auto *action = seed.findChild<QAction *>("twoPanelsAction");
            QTRY_VERIFY(action->isEnabled());
            action->trigger();
            QTRY_VERIFY(action->isChecked());
            QVERIFY(seed.close());
        }
        QtWorkspaceApplication application(*qApp);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        const auto closeUnexpectedWindows = qScopeGuard([&] {
            for (auto *widget : QApplication::topLevelWidgets())
                if (auto *other = dynamic_cast<MainWindow *>(widget); other && other != &window)
                    other->close();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        });
        QCOMPARE(window.findChild<QAction *>("twoPanelsAction")->isChecked(), twoPanels);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        auto *owner = workspace(window);
        owner->NewTab(0, fixture.path());
        auto *bar = window.findChild<QTabBar *>("panelTabs0");
        QTRY_COMPARE(bar->count(), 2);
        QTRY_COMPARE(QDir::cleanPath(owner->CurrentLocation()), fixture.path());
        QTimer answer;
        bool selected = false;
        connect(&answer, &QTimer::timeout, &window, [&] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            if (!menu)
                return;
            answer.stop();
            for (auto *action : menu->actions())
                if (action->text() ==
                    QCoreApplication::translate("QtWindowWorkspace", "Move to new window")) {
                    selected = true;
                    QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
                                      menu->actionGeometry(action).center());
                    return;
                }
            menu->close();
        });
        answer.start(5);
        QVERIFY(QMetaObject::invokeMethod(bar, "customContextMenuRequested", Qt::DirectConnection,
                                          Q_ARG(QPoint, bar->tabRect(1).center())));
        QVERIFY(selected);
        owner->CloseTab(0, 1);
        QTRY_COMPARE(bar->count(), 1);
        const auto visibleWindows = [] {
            int count = 0;
            for (auto *widget : QApplication::topLevelWidgets())
                count += dynamic_cast<MainWindow *>(widget) && widget->isVisible();
            return count;
        };
        QTRY_COMPARE(visibleWindows(), 1);
        QVERIFY(window.isVisible());
    }
    void commandKeepsItsTabIdentity_data() {
        QTest::addColumn<QString>("command");
        QTest::addColumn<QString>("mutation");
        QTest::addColumn<int>("target");
        QTest::newRow("duplicate-reordered")
            << QString("Duplicate location") << QString("reorder") << 1;
        QTest::newRow("close-reordered") << QString("Close tab") << QString("reorder") << 1;
        QTest::newRow("duplicate-removed")
            << QString("Duplicate location") << QString("remove") << 1;
        QTest::newRow("close-removed") << QString("Close tab") << QString("remove") << 1;
        QTest::newRow("duplicate-last-removed")
            << QString("Duplicate location") << QString("remove") << 2;
        QTest::newRow("move-removed") << QString("Move to other panel") << QString("remove") << 1;
        QTest::newRow("detach-removed") << QString("Move to new window") << QString("remove") << 1;
        QTest::newRow("duplicate-moved") << QString("Duplicate location") << QString("move") << 1;
        QTest::newRow("close-moved") << QString("Close tab") << QString("move") << 1;
    }
    void commandKeepsItsTabIdentity() {
        QFETCH(QString, command);
        QFETCH(QString, mutation);
        QFETCH(int, target);
        const bool quit = qApp->quitOnLastWindowClosed();
        qApp->setQuitOnLastWindowClosed(false);
        const auto restoreQuit = qScopeGuard([quit] { qApp->setQuitOnLastWindowClosed(quit); });
        QtWorkspaceApplication application(*qApp);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QStringList paths;
        for (const QString &name :
             {QString("A folder"), QString("B папка 空"), QString("C folder")}) {
            paths.append(fixture.filePath(name));
            QVERIFY(QDir().mkpath(paths.last()));
        }
        ControlledBackend backend;
        MainWindow window(&backend);
        window.resize(980, 620);
        window.show();
        const auto closeUnexpectedWindows = qScopeGuard([&] {
            for (auto *widget : QApplication::topLevelWidgets())
                if (auto *other = dynamic_cast<MainWindow *>(widget); other && other != &window)
                    other->close();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        });
        QVERIFY(WindowActivation::waitForActive(window));
        auto *owner = workspace(window);
        QVERIFY(owner);
        auto *bar = window.findChild<QTabBar *>("panelTabs0");
        QVERIFY(bar);
        window.openPath(paths[0]);
        QTRY_COMPARE(QDir::cleanPath(owner->CurrentLocation()), paths[0]);
        for (int index = 1; index < paths.size(); ++index) {
            owner->NewTab(0, paths[index]);
            QTRY_COMPARE(bar->count(), index + 1);
            QTRY_COMPARE(QDir::cleanPath(owner->CurrentLocation()), paths[index]);
        }
        bar->setCurrentIndex(0);
        QTRY_COMPARE(QDir::cleanPath(owner->CurrentLocation()), paths[0]);
        auto *twoPanels = window.findChild<QAction *>("twoPanelsAction");
        QTRY_VERIFY(twoPanels->isEnabled());
        if (mutation == "move") {
            twoPanels->trigger();
            QTRY_VERIFY(twoPanels->isChecked());
            QTRY_VERIFY(window.findChild<QTabBar *>("panelTabs1"));
            QTRY_VERIFY(twoPanels->isEnabled());
        }
        const QString firstId = bar->tabData(0).toString();
        const QString targetId = bar->tabData(target).toString();
        const QString survivorId = bar->tabData(target == 1 ? 2 : 1).toString();
        const QPoint point = bar->tabRect(target).center();
        bool selected = false;
        QTimer answer;
        connect(&answer, &QTimer::timeout, &window, [&] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            if (!menu)
                return;
            answer.stop();
            const auto close = qScopeGuard([menu] { menu->close(); });
            if (mutation == "reorder")
                bar->moveTab(target, 0);
            else if (mutation == "remove")
                owner->CloseTab(0, target);
            else {
                QVERIFY(owner->TransferTab(targetId, *owner, 1));
            }
            const QString label =
                QCoreApplication::translate("QtWindowWorkspace", command.toUtf8());
            for (auto *action : menu->actions())
                if (action->text() == label) {
                    selected = true;
                    QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
                                      menu->actionGeometry(action).center());
                    break;
                }
        });
        answer.start(5);
        QVERIFY(QMetaObject::invokeMethod(bar, "customContextMenuRequested", Qt::DirectConnection,
                                          Q_ARG(QPoint, point)));
        answer.stop();
        QVERIFY(selected);
        QVERIFY(!QTest::currentTestFailed());
        QVERIFY(contains(*bar, firstId));
        QVERIFY(contains(*bar, survivorId));
        if (mutation == "remove") {
            QCOMPARE(bar->count(), 2);
            QVERIFY(!contains(*bar, targetId));
            QVERIFY(!window.findChild<QAction *>("twoPanelsAction")->isChecked());
            int windows = 0;
            for (auto *widget : QApplication::topLevelWidgets())
                windows += dynamic_cast<MainWindow *>(widget) && widget->isVisible();
            QCOMPARE(windows, 1);
        } else {
            auto *destination =
                mutation == "move" ? window.findChild<QTabBar *>("panelTabs1") : bar;
            QVERIFY(destination);
            if (command == "Duplicate location") {
                QTRY_COMPARE(destination->count(), mutation == "move" ? 3 : 4);
                QVERIFY(contains(*destination, targetId));
                QTRY_COMPARE(QDir::cleanPath(owner->CurrentLocation()), paths[target]);
            } else {
                QTRY_COMPARE(destination->count(), mutation == "move" ? 1 : 2);
                QVERIFY(!contains(*destination, targetId));
            }
        }
        for (const auto &path : paths)
            QVERIFY(QFileInfo(path).isDir());
    }
};

QTEST_MAIN(WorkspaceTabMenuTests)
#include "workspace_tab_menu_tests.moc"
