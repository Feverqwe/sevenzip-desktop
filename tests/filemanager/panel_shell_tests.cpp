#include "filemanager/SysIconUtils.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "ui/formatting/qtregionalformat.h"
#include "ui/support/layout_inspection.h"

#include <QMenuBar>
#include <QScopeGuard>
#include <QThread>

namespace {
struct CurrentDirectoryGuard {
    const QString path = QDir::currentPath();
    ~CurrentDirectoryGuard() { QDir::setCurrent(path); }
};

QLabel *status(QWidget *panel, const char *name) {
    return panel->window()->findChild<QLabel *>(name);
}
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

    void selectionSizeKeepsZeroUnknownAndOverflowDistinct() {
        QTemporaryDir fixture;
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        ArchiveEntry huge{"huge.bin", -1, -1};
        huge.sizeValue = UInt64(-1);
        openListedArchive(window, backend, fixture.filePath("sizes.zip"),
                          {{"empty.bin", 0, 0}, {"one.bin", 1, 1}, {"unknown.bin", -1, -1}, huge});
        auto *files = window.findChild<FileTable *>("archiveTable");
        auto *size = window.findChild<QLabel *>("selectedSizeStatus");
        const auto item = [&](const QString &name) {
            for (int row = 0; row < files->rowCount(); ++row)
                if (files->model()->index(row, 0).data().toString() == name)
                    return files->model()->index(row, 0);
            return QModelIndex();
        };
        const auto select = [&](const QString &name) {
            const auto index = item(name);
            QVERIFY(index.isValid());
            files->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect |
                                                       QItemSelectionModel::Rows);
            files->setCurrentIndex(index);
        };
        select("empty.bin");
        QTRY_COMPARE(size->text(), QtRegionalFormat::bytes(0));
        QVERIFY(size->isVisible());
        select("unknown.bin");
        QTRY_VERIFY(size->text().isEmpty());
        QCOMPARE(item("unknown.bin").siblingAtColumn(2).data().toString(), QString::fromUtf8("—"));
        select("huge.bin");
        QTRY_COMPARE(size->text(), QtRegionalFormat::bytes(UInt64(-1)));
        QVERIFY(size->toolTip().contains(QLocale::system().toString(qulonglong(-1))));
        files->selectionModel()->select(item("one.bin"),
                                        QItemSelectionModel::Select | QItemSelectionModel::Rows);
        QTRY_VERIFY(size->text().isEmpty());
        QVERIFY(!size->isVisible());
    }

    void uncalculatedFilesystemFolderDoesNotBecomeZero() {
        QTemporaryDir fixture;
        CurrentDirectoryGuard guard;
        QVERIFY(QDir().mkpath(fixture.filePath("folder")));
        QFile file(fixture.filePath("one.bin"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("x"), qint64(1));
        file.close();
        QVERIFY(QDir::setCurrent(fixture.path()));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        auto *files = window.findChild<FileTable *>("archiveTable");
        QModelIndex folder, regular;
        for (int row = 0; row < files->rowCount(); ++row) {
            const auto index = files->model()->index(row, 0);
            if (index.data().toString() == "folder")
                folder = index;
            if (index.data().toString() == "one.bin")
                regular = index;
        }
        QVERIFY(folder.isValid() && regular.isValid());
        QVERIFY(folder.siblingAtColumn(2).data().toString().isEmpty());
        files->selectionModel()->select(regular, QItemSelectionModel::ClearAndSelect |
                                                     QItemSelectionModel::Rows);
        auto *size = window.findChild<QLabel *>("selectedSizeStatus");
        QTRY_COMPARE(size->text(), QtRegionalFormat::bytes(1));
        files->selectionModel()->select(folder,
                                        QItemSelectionModel::Select | QItemSelectionModel::Rows);
        QTRY_VERIFY(size->text().isEmpty());
        QVERIFY(!size->isVisible());
    }

    void alternativeCurrentItemIsNotAnExplicitSelection() {
        QTemporaryDir fixture;
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        openListedArchive(window, backend, fixture.filePath("current.zip"), {{"file.bin", 7, 7}});
        auto *files = window.findChild<FileTable *>("archiveTable");
        files->sourcePanel->SetAlternativeSelection(true);
        files->selectRow(0);
        files->setCurrentCell(0, 0);
        const auto summary = files->sourcePanel->Refresh_StatusBar();
        QCOMPARE(summary.selectedCount, 0);
        QVERIFY(!summary.selectedSize);
        QVERIFY(window.findChild<QLabel *>("selectedSizeStatus")->text().isEmpty());
        // The ordinary command contract still operates on current when needed.
        CRecordVector<UInt32> operated;
        files->sourcePanel->Get_ItemIndices_Operated(operated);
        QCOMPARE(operated.Size(), 1u);
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
                     QStringLiteral("Objects: 1"));
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
        QCOMPARE(status(leftPanel, "selectedSizeStatus")->text(), QtRegionalFormat::bytes(7));

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
#ifdef Q_OS_WIN
                QVERIFY(system);
                QVERIFY(system->isEnabled());
                QVERIFY(system->menu());
                QVERIFY(!system->menu()->actions().isEmpty());
#else
                QVERIFY(!system);
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
        QTRY_VERIFY(rightTable->isEnabled());
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        rightTable->setFocus();
        QTRY_COMPARE(QApplication::focusWidget(), rightTable);
        QTRY_COMPARE(status(rightPanel, "selectedItemsStatus")->text(),
                     QStringLiteral("Right · Objects: 1"));
        rightTable->selectRow(0);
        rightTable->setCurrentCell(0, 0);
        rightTable->setFocus();
        QTRY_COMPARE(status(rightPanel, "selectedItemsStatus")->text(),
                     QStringLiteral("Right · Selected: 1 / 1"));
        QCOMPARE(status(rightPanel, "selectedSizeStatus")->text(), QtRegionalFormat::bytes(12));
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(size);
            QApplication::processEvents();
            QCOMPARE(window.findChildren<QStatusBar *>().size(), 1);
            QVERIFY(UiInspection::fullyVisible(*window.statusBar(), window));
            QVERIFY(UiInspection::fullyVisible(*status(rightPanel, "selectedSizeStatus"), window));
            QVERIFY(UiInspection::writeReport(
                window, QStringLiteral("panel-shell-%1x%2").arg(size.width()).arg(size.height())));
        }
        inspectContextMenu(
            rightTable, rightTable->visualRect(rightTable->model()->index(0, 0)).center(),
            [&](QMenu *menu) {
                QVERIFY(UiInspection::writeReport(*menu, QStringLiteral("panel-context-disk-640")));
            });
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        leftTable->setFocus();
        QTRY_COMPARE(QApplication::focusWidget(), leftTable);
        QTRY_COMPARE(status(leftPanel, "selectedSizeStatus")->text(), QtRegionalFormat::bytes(7));
        QVERIFY(status(leftPanel, "selectedItemsStatus")->text().startsWith("Left · "));
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
        auto *bar = window.statusBar();
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
