#include "filemanager/support/window_state_support.h"
#include "filemanager/window/qtfavoritestore.h"
#include "filemanager/window/qtwindowworkspace.h"
#include "filemanager/window/qtworkspaceapplication.h"
#include <QScopeGuard>
#include <QSplitterHandle>
#include <QTabBar>

class WorkspaceSessionTests final : public QObject {
    Q_OBJECT

    static QtWindowWorkspace *workspace(MainWindow &window) {
        for (auto *object : window.children())
            if (auto *service = dynamic_cast<QtWindowWorkspace *>(object))
                return service;
        return nullptr;
    }
    static FileTable *active(MainWindow &window) {
        for (auto *table : window.findChildren<FileTable *>())
            if (table->isVisible())
                return table;
        return nullptr;
    }
    static QList<MainWindow *> windows() {
        QList<MainWindow *> result;
        for (auto *widget : QApplication::topLevelWidgets())
            if (auto *window = dynamic_cast<MainWindow *>(widget); window && window->isVisible())
                result.append(window);
        return result;
    }
    static QString location(MainWindow &window) {
        const auto *table = active(window);
        return table ? QDir::cleanPath(ToQString(table->sourcePanel->GetCurrentFolderPrefix()))
                     : QString();
    }

  private slots:
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }
    void sessionRestoresWindowOrderAndIndependentTabs() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString a = fixture.filePath("A folder");
        const QString b = fixture.filePath("B folder");
        const QString c = fixture.filePath("C folder");
        for (const auto &path : {a, b, c})
            QVERIFY(QDir().mkpath(path));
        const bool quit = qApp->quitOnLastWindowClosed();
        qApp->setQuitOnLastWindowClosed(false);
        const auto restoreQuit = qScopeGuard([quit] { qApp->setQuitOnLastWindowClosed(quit); });
        {
            QtWorkspaceApplication application(*qApp);
            ControlledBackend firstBackend, secondBackend;
            MainWindow first(&firstBackend), second(&secondBackend);
            first.show();
            second.show();
            first.openPath(a);
            first.findChild<QAction *>("newTabAction")->trigger();
            QTRY_COMPARE(first.findChild<QTabBar *>("panelTabs0")->count(), 2);
            QTRY_VERIFY(active(first) && active(first)->isEnabled());
            first.openPath(b);
            second.openPath(c);
            application.Quit();
            QVERIFY(!first.isVisible() && !second.isVisible());
        }
        {
            QtWorkspaceApplication application(*qApp);
            ControlledBackend backend;
            MainWindow first(&backend);
            first.show();
            application.Restore(first);
            QTRY_COMPARE_WITH_TIMEOUT(windows().size(), 2, 10000);
            QTRY_COMPARE_WITH_TIMEOUT(location(first), b, 10000);
            auto *tabs = first.findChild<QTabBar *>("panelTabs0");
            QTRY_COMPARE(tabs->count(), 2);
            QCOMPARE(tabs->currentIndex(), 1);
            MainWindow *second = nullptr;
            for (auto *window : windows())
                if (window != &first)
                    second = window;
            QVERIFY(second);
            QTRY_COMPARE_WITH_TIMEOUT(location(*second), c, 10000);
            tabs->setCurrentIndex(0);
            QTRY_COMPARE(location(first), a);
            QCOMPARE(location(*second), c);
            application.Quit();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
    }

    void restoringCompactWindowKeepsItsCustomFileAreaSplit() {
        QJsonObject saved;
        QList<int> original;
        {
            ControlledBackend backend;
            MainWindow first(&backend);
            first.resize(640, 400);
            first.show();
            first.findChild<QAction *>("twoPanelsAction")->trigger();
            auto *splitter = first.findChild<QSplitter *>("filePanelSplitter");
            QTRY_VERIFY(splitter && splitter->widget(1)->isVisible());
            auto *handle = splitter->handle(1);
            QTest::keyClick(handle, Qt::Key_Right, Qt::ShiftModifier);
            original = splitter->sizes();
            saved = workspace(first)->Save();
        }
        ControlledBackend backend;
        MainWindow restored(&backend);
        restored.resize(980, 620);
        restored.show();
        bool done = false;
        workspace(restored)->Restore(saved, [&] { done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        auto *splitter = restored.findChild<QSplitter *>("filePanelSplitter");
        QTRY_COMPARE(splitter->sizes(), original);
        restored.resize(980, 620);
        QApplication::processEvents();
        restored.resize(640, 400);
        QApplication::processEvents();
        const auto resized = splitter->sizes();
        QVERIFY(qAbs(resized.value(0) - original.value(0)) <= 1);
        auto *places = restored.findChild<QAction *>("sidebarAction");
        places->setChecked(false);
        places->setChecked(true);
        QApplication::processEvents();
        QVERIFY(qAbs(splitter->sizes().value(0) - original.value(0)) <= 1);
    }

    void missingSavedLocationDoesNotExposeFallbackFolderCommands() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString gone = fixture.filePath("Removed folder");
        const QString usable = fixture.filePath("Available folder");
        QtWorkspaceApplication application(*qApp);
        QVERIFY(QDir().mkpath(gone));
        QVERIFY(QDir().mkpath(usable));
        QJsonObject saved;
        {
            ControlledBackend backend;
            MainWindow window(&backend);
            window.show();
            window.openPath(gone);
            saved = workspace(window)->Save();
        }
        QVERIFY(QDir().rmdir(gone));
        ControlledBackend backend;
        MainWindow restored(&backend);
        restored.show();
        bool done = false;
        workspace(restored)->Restore(saved, [&] { done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        QVERIFY(active(restored));
        QVERIFY(!active(restored)->isEnabled());
        QVERIFY(!restored.findChild<QAction *>("createFolderAction")->isEnabled());
        QCOMPARE(QDir::cleanPath(restored.findChild<QLineEdit *>("archivePath")->text()), gone);
        restored.openPath(usable);
        QTRY_VERIFY(active(restored)->isEnabled());
        QCOMPARE(location(restored), usable);
        QVERIFY(restored.findChild<QAction *>("createFolderAction")->isEnabled());
        auto *favorites = restored.findChild<QListView *>("favoritePlaces0");
        auto *store = dynamic_cast<QtFavoriteStore *>(favorites->model());
        QVERIFY(store);
        store->Add(gone, "Unavailable location");
        const QModelIndex favorite = store->index(store->rowCount() - 1);
        favorites->scrollTo(favorite);
        QTest::mouseClick(favorites->viewport(), Qt::LeftButton, Qt::NoModifier,
                          favorites->visualRect(favorite).center());
        QTRY_VERIFY(!active(restored)->isEnabled());
        QCOMPARE(QDir::cleanPath(restored.findChild<QLineEdit *>("archivePath")->text()), gone);
        restored.findChild<QAction *>("newWindowAction")->trigger();
        QPointer<MainWindow> copy;
        const auto closeCopy = qScopeGuard([&] {
            if (copy)
                copy->close();
        });
        QTRY_VERIFY(windows().size() == 2);
        for (auto *window : windows())
            if (window != &restored)
                copy = window;
        QVERIFY(copy);
        QTRY_COMPARE(QDir::cleanPath(copy->findChild<QLineEdit *>("archivePath")->text()), gone);
        QTRY_VERIFY(active(*copy) && !active(*copy)->isEnabled());
    }
};

QTEST_MAIN(WorkspaceSessionTests)
#include "workspace_session_tests.moc"
