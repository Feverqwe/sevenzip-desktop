#include "filemanager/SysIconUtils.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "ui/support/layout_inspection.h"

#include <QMenuBar>
#include <QScopeGuard>
#include <QThread>

namespace {
struct CurrentDirectoryGuard {
    const QString path = QDir::currentPath();
    ~CurrentDirectoryGuard() { QDir::setCurrent(path); }
};

QLabel *status(QWidget *panel, const char *name) { return panel->findChild<QLabel *>(name); }
QAction *menuAction(QMenu &menu, const char *name) {
    for (QAction *action : menu.actions())
        if (action->objectName() == QLatin1String(name))
            return action;
        else if (action->menu())
            if (auto *found = menuAction(*action->menu(), name))
                return found;
    return nullptr;
}
} // namespace

class PanelShellTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT

  private slots:
    void init() {
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settings);
        SaveRegLang(UString("-"), settings);
    }

    void perPanelStatusAndSourceContextFollowFocus() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        CurrentDirectoryGuard guard;
        const QString left = fixture.filePath("left");
        const QString right = fixture.filePath("right space");
        QVERIFY(QDir().mkpath(left));
        QVERIFY(QDir().mkpath(right));
        QFile leftFile(QDir(left).filePath(QStringLiteral("левый.txt")));
        QVERIFY(leftFile.open(QIODevice::WriteOnly));
        QCOMPARE(leftFile.write("1234567"), 7);
        leftFile.close();
        QFile rightFile(QDir(right).filePath(QStringLiteral("right.txt")));
        QVERIFY(rightFile.open(QIODevice::WriteOnly));
        QCOMPARE(rightFile.write("123456789012"), 12);
        rightFile.close();
        QVERIFY(QDir::setCurrent(left));
        const QString settingsPath = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        SavePanelPath(1, ToUString(right), settingsPath);
        QSettings settings(settingsPath, QSettings::IniFormat);
        settings.setValue("FM/ShowSystemMenu", true);
        settings.setValue("FM/ShowRealFileIcons", true);
        settings.sync();
        g_Ext_to_Icon_Map.Clear();
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *leftPanel = window.findChild<QWidget *>("filePanel0");
        QVERIFY(leftPanel);
        auto *leftTable = leftPanel->findChild<FileTable *>("archiveTable");
        QTRY_COMPARE(status(leftPanel, "selectedItemsStatus")->text(),
                     QStringLiteral("Selected: 0 / 1"));
        QCOMPARE(leftTable->rowCount(), 1);
        QVERIFY(!leftTable->model()->index(0, 0).data(Qt::DecorationRole).value<QIcon>().isNull());
        QIcon sourceIcon;
        QCOMPARE(static_cast<FileTable *>(leftTable)->sourcePanel->GetItemIcon(
                     leftTable->model()->index(0, 0).data(Qt::UserRole + 6).toUInt(), sourceIcon),
                 S_OK);
        QCOMPARE(leftTable->model()->index(0, 0).data(Qt::DecorationRole).value<QIcon>().cacheKey(),
                 sourceIcon.cacheKey());
        leftTable->selectRow(0);
        leftTable->setCurrentCell(0, 0);
        QTRY_COMPARE(status(leftPanel, "selectedItemsStatus")->text(),
                     QStringLiteral("Selected: 1 / 1"));
        QCOMPARE(status(leftPanel, "selectedSizeStatus")->text(), QStringLiteral("7"));
        QCOMPARE(status(leftPanel, "focusedSizeStatus")->text(), QStringLiteral("7"));
        QVERIFY(!status(leftPanel, "focusedDateStatus")->text().isEmpty());

        auto inspectContextMenu = [&](FileTable *view, const QPoint &point, const auto &inspect) {
            window.activateWindow();
            QVERIFY(QTest::qWaitForWindowActive(&window));
            view->setFocus();
            QTRY_COMPARE(QApplication::focusWidget(), view);
            const int outerLoopLevel = QThread::currentThread()->loopLevel();
            bool observed = false;
            QTimer timer;
            connect(&timer, &QTimer::timeout, &window, [&] {
                auto *menu = window.findChild<QMenu *>("panelContextMenu");
                if (!menu || !menu->isVisible() ||
                    QThread::currentThread()->loopLevel() <= outerLoopLevel)
                    return;
                timer.stop();
                observed = true;
                const auto close = qScopeGuard([&] { menu->close(); });
                inspect(menu);
            });
            timer.start(5);
            QVERIFY(QMetaObject::invokeMethod(view, "customContextMenuRequested",
                                              Qt::DirectConnection, Q_ARG(QPoint, point)));
            QVERIFY(observed);
        };
        inspectContextMenu(
            leftTable, leftTable->visualRect(leftTable->model()->index(0, 0)).center(),
            [&](QMenu *menu) {
                auto *system = menuAction(*menu, "systemContextGroup");
                QVERIFY(system);
#ifdef Q_OS_WIN
                QVERIFY(system->isEnabled());
                QVERIFY(system->menu());
                QVERIFY(!system->menu()->actions().isEmpty());
#else
                QVERIFY(!system->isEnabled());
#endif
                auto *sevenZip = menuAction(*menu, "sevenZipContextGroup");
                QVERIFY(sevenZip && !sevenZip->isEnabled());
                auto *open = menuAction(*menu, "openItemAction");
                auto *rename = menuAction(*menu, "renameAction");
                auto *create = menuAction(*menu, "createFolderAction");
                QVERIFY(open && rename && create);
                QVERIFY(menu->actions().indexOf(open) < menu->actions().indexOf(rename));
                auto *more = menu->findChild<QMenu *>("moreCommandsMenu");
                QVERIFY(more && more->actions().contains(create));
                QVERIFY(!menuAction(*menu, "exitAction"));
                QVERIFY(UiInspection::writeReport(*menu, QStringLiteral("panel-context-disk")));
            });
        inspectContextMenu(leftTable, QPoint(-1, -1),
                           [&](QMenu *menu) { QVERIFY(menuAction(*menu, "openItemAction")); });

        window.findChild<QAction *>("twoPanelsAction")->trigger();
        auto *rightPanel = window.findChild<QWidget *>("filePanel1");
        QVERIFY(rightPanel && rightPanel->isVisible());
        auto *rightTable = rightPanel->findChild<FileTable *>("archiveTable");
        QTRY_COMPARE(status(rightPanel, "selectedItemsStatus")->text(),
                     QStringLiteral("Selected: 0 / 1"));
        rightTable->selectRow(0);
        rightTable->setCurrentCell(0, 0);
        rightTable->setFocus();
        QTRY_COMPARE(status(rightPanel, "selectedItemsStatus")->text(),
                     QStringLiteral("Selected: 1 / 1"));
        QCOMPARE(status(rightPanel, "selectedSizeStatus")->text(), QStringLiteral("12"));
        QCOMPARE(status(leftPanel, "selectedSizeStatus")->text(), QStringLiteral("7"));
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(size);
            QApplication::processEvents();
            for (QWidget *panel : {leftPanel, rightPanel}) {
                auto *bar = panel->findChild<QStatusBar *>("panelStatusBar");
                QVERIFY(bar);
                QVERIFY(UiInspection::fullyVisible(*bar, window));
                for (const char *name : {"selectedItemsStatus", "selectedSizeStatus",
                                         "focusedSizeStatus", "focusedDateStatus"})
                    QVERIFY(UiInspection::fullyVisible(*status(panel, name), window));
            }
            QVERIFY(UiInspection::writeReport(
                window, QStringLiteral("panel-shell-%1x%2").arg(size.width()).arg(size.height())));
        }
        inspectContextMenu(
            rightTable, rightTable->visualRect(rightTable->model()->index(0, 0)).center(),
            [&](QMenu *menu) {
                QVERIFY(UiInspection::writeReport(*menu, QStringLiteral("panel-context-disk-640")));
            });
        QCOMPARE(status(leftPanel, "selectedSizeStatus")->text(), QStringLiteral("7"));
    }

    void sourceIconCacheUsesExtensionsAndSplitFamily() {
        CExtToIconMap cache;
        const int text = cache.GetIconIndex(FILE_ATTRIBUTE_ARCHIVE, L"file.txt");
        QVERIFY(text > 0);
        QVERIFY(!cache.GetIcon(0).isNull());
        QCOMPARE(cache.GetIconIndex(FILE_ATTRIBUTE_ARCHIVE, L"another.TXT"), text);
        const int split = cache.GetIconIndex(FILE_ATTRIBUTE_ARCHIVE, L"file.001");
        QCOMPARE(cache.GetIconIndex(FILE_ATTRIBUTE_ARCHIVE, L"file.002"), split);
        const int folder = cache.GetIconIndex(FILE_ATTRIBUTE_DIRECTORY, L"folder");
        QCOMPARE(cache.GetIconIndex(FILE_ATTRIBUTE_DIRECTORY, L"another"), folder);
        QVERIFY(!cache.GetIcon(FILE_ATTRIBUTE_ARCHIVE, QStringLiteral("file.txt")).isNull());
        cache.Clear();
        QVERIFY(!cache.GetIcon(FILE_ATTRIBUTE_DIRECTORY, QStringLiteral("folder")).isNull());
    }

    void archiveContextKeepsSourceActionsAndArchiveWorkflows() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        ControlledBackend backend;
        QSettings settings(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"), QSettings::IniFormat);
        settings.setValue("FM/ShowRealFileIcons", true);
        settings.sync();
        g_Ext_to_Icon_Map.Clear();
        MainWindow window(&backend);
        window.show();
        openListedArchive(window, backend, fixture.filePath("with space.zip"),
                          {ArchiveEntry{QStringLiteral("имя.txt"), 7, 5}});
        auto *files = window.findChild<FileTable *>("archiveTable");
        QTRY_VERIFY(files->rowCount() > 0);
        int fileRow = -1;
        for (int row = 0; row < files->rowCount(); ++row)
            if (files->model()->index(row, 0).data().toString() == QStringLiteral("имя.txt"))
                fileRow = row;
        QVERIFY(fileRow >= 0);
        QIcon sourceIcon;
        QCOMPARE(static_cast<FileTable *>(files)->sourcePanel->GetItemIcon(
                     files->model()->index(fileRow, 0).data(Qt::UserRole + 6).toUInt(), sourceIcon),
                 S_OK);
        QVERIFY(!sourceIcon.isNull());
        QCOMPARE(
            files->model()->index(fileRow, 0).data(Qt::DecorationRole).value<QIcon>().cacheKey(),
            sourceIcon.cacheKey());
        files->selectRow(fileRow);
        files->setCurrentCell(fileRow, 0);
        QTimer::singleShot(0, [&] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            QVERIFY(menu);
            const auto close = qScopeGuard([menu] { menu->close(); });
            QVERIFY(menuAction(*menu, "openItemAction"));
            QVERIFY(menuAction(*menu, "copyAction"));
            QVERIFY(menuAction(*menu, "extractArchiveAction"));
            QVERIFY(!menu->findChild<QAction *>("systemContextGroup"));
            QVERIFY(!menuAction(*menu, "exitAction"));
            QVERIFY(UiInspection::writeReport(*menu, QStringLiteral("panel-context-archive")));
            menu->close();
        });
        QVERIFY(QMetaObject::invokeMethod(
            files, "customContextMenuRequested", Qt::DirectConnection,
            Q_ARG(QPoint, files->visualRect(files->model()->index(fileRow, 0)).center())));
        window.resize(640, 400);
        QApplication::processEvents();
        auto *bar = window.findChild<QStatusBar *>("panelStatusBar");
        QVERIFY(bar);
        QVERIFY(UiInspection::fullyVisible(*bar, window));
        QVERIFY(UiInspection::writeReport(window, QStringLiteral("panel-shell-archive-640")));
        QTimer::singleShot(0, [&] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            QVERIFY(menu);
            QVERIFY(UiInspection::writeReport(*menu, QStringLiteral("panel-context-archive-640")));
            menu->close();
        });
        QVERIFY(QMetaObject::invokeMethod(
            files, "customContextMenuRequested", Qt::DirectConnection,
            Q_ARG(QPoint, files->visualRect(files->model()->index(fileRow, 0)).center())));
    }
};

QTEST_MAIN(PanelShellTests)
#include "panel_shell_tests.moc"
