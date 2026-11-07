#include "filemanager/support/recent_places_menu.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "support/panel_keys.h"
#include "support/window_activation.h"
#include <QScopeGuard>
#include <algorithm>

extern bool g_CaseSensitive;

class MainWindowStateTests : public QObject, protected MainWindowTestSupport {
    Q_OBJECT

  private slots:
    void initTestCase() {
        QSettings settings(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"), QSettings::IniFormat);
        settings.clear();
    }

    void recentPlacesPersistAcrossWindows() {
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString first = temp.filePath("first folder");
        const QString second = temp.filePath(QStringLiteral("вторая папка"));
        QVERIFY(QDir().mkpath(first));
        QVERIFY(QDir().mkpath(second));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openPath(first);
        window.openPath(second);
        const QString canonicalFirst = QFileInfo(first).canonicalFilePath();
        const QString canonicalSecond = QFileInfo(second).canonicalFilePath();
        MainWindow reopened(&backend);
        reopened.show();
        action(reopened, "foldersHistoryAction")->trigger();
        QTRY_VERIFY(RecentPlacesMenu::Visible(reopened));
        auto *popup = RecentPlacesMenu::Visible(reopened);
        auto *firstItem = RecentPlacesMenu::Find(*popup, canonicalFirst);
        auto *secondItem = RecentPlacesMenu::Find(*popup, canonicalSecond);
        QVERIFY(firstItem && secondItem);
        QVERIFY(popup->actions().indexOf(secondItem) < popup->actions().indexOf(firstItem));
        RecentPlacesMenu::Choose(*popup, *firstItem);
        QTRY_COMPARE(QDir::cleanPath(reopened.findChild<QLineEdit *>("archivePath")->text()),
                     canonicalFirst);
    }

    void favoritesFollowFoldersAndArchivePaths() {
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString first = temp.filePath(QStringLiteral("папка с пробелом"));
        const QString second = temp.filePath("other");
        QVERIFY(QDir().mkpath(first));
        QVERIFY(QDir().mkpath(second));
        const QString archive = temp.filePath(QStringLiteral("архив с пробелом.7z"));
        QFile file(archive);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();

        ControlledBackend backend;
        MainWindow window(&backend);
        auto *address = window.findChild<QLineEdit *>("archivePath");
        auto *view = table(window);
        address->setText(first);
        QTest::keyClick(address, Qt::Key_Return);
        QTest::keyClick(view, Qt::Key_1, Qt::AltModifier | Qt::ShiftModifier);
        QCOMPARE(action(window, "openBookmark1Action")->isEnabled(), true);
        address->setText(second);
        QTest::keyClick(address, Qt::Key_Return);
        QTest::keyClick(view, Qt::Key_1, Qt::AltModifier);
        QCOMPARE(address->text(), QFileInfo(first).canonicalFilePath());

        backend.queueListAction([&] {
            const auto finish = qScopeGuard([&] {
                backend.completeList({ArchiveEntry{QStringLiteral("внутри/файл.txt"), 1, 1}});
            });
            QVERIFY(!action(window, "openBookmark1Action")->isEnabled());
        });
        window.openArchive(archive);
        openRow(window, rowWithPath(window, QStringLiteral("внутри")));
        action(window, "saveBookmark2Action")->trigger();
        action(window, "openBookmark1Action")->trigger();
        QCOMPARE(address->text(), QFileInfo(first).canonicalFilePath());
        backend.queueList({ArchiveEntry{QStringLiteral("внутри/файл.txt"), 1, 1}});
        action(window, "openBookmark2Action")->trigger();
        QCOMPARE(backend.listCalls, 2);
        QVERIFY(address->text().endsWith(QStringLiteral(" / внутри")));
        QCOMPARE(view->rowCount(), 1);

        MainWindow reopened;
        QVERIFY(action(reopened, "openBookmark1Action")->isEnabled());
        QVERIFY(action(reopened, "openBookmark2Action")->isEnabled());
        QFile::remove(archive);
        action(reopened, "openBookmark2Action")->trigger();
        QCOMPARE(reopened.findChild<QLineEdit *>("archivePath")->text(),
                 QFileInfo(temp.path()).canonicalFilePath());
    }

    void mainWindowFolderAndSelectionCommands() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString first = temp.filePath(QStringLiteral("файл с пробелом.txt"));
        QFile file(first);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        auto *address = window.findChild<QLineEdit *>("archivePath");
        PanelKeys::focusAddress(address);
        address->setText(temp.path());
        QTest::keyClick(address, Qt::Key_Return);
        QTRY_COMPARE(table(window)->rowCount(), 1);
        auto *createFolder = action(window, "createFolderAction");
        auto *refresh = action(window, "refreshAction");
        QVERIFY(createFolder->isEnabled());
        QVERIFY(refresh->isEnabled());

        createFolder->trigger();
        auto *name = window.findChild<QLineEdit *>("inlineFolderName");
        QVERIFY(name && name->isVisible());
        QTest::keyClick(name, Qt::Key_Escape);
        QCOMPARE(table(window)->rowCount(), 1);
        createFolder->trigger();
        const auto drafts = window.findChildren<QLineEdit *>("inlineFolderName");
        const auto visible =
            std::ranges::find_if(drafts, [](auto *field) { return field->isVisible(); });
        QVERIFY(visible != drafts.end());
        (*visible)->setText(QStringLiteral("папка с пробелом"));
        QTest::keyClick(*visible, Qt::Key_Return);
        const QString folder = temp.filePath(QStringLiteral("папка с пробелом"));
        QVERIFY(QFileInfo(folder).isDir());
        QCOMPARE(table(window)->selectionModel()->selectedRows().size(), 1);
        QCOMPARE(table(window)
                     ->model()
                     ->index(table(window)->selectionModel()->selectedRows().first().row(), 0)
                     .data()
                     .toString(),
                 QStringLiteral("папка с пробелом"));

        auto *deselect = action(window, "deselectAllAction");
        QTRY_VERIFY(deselect->isEnabled());
        deselect->trigger();
        QTRY_VERIFY(table(window)->selectionModel()->selectedRows().isEmpty());
        table(window)->selectRow(filesystemRow(window, QStringLiteral("файл с пробелом.txt")));
        const QString later = temp.filePath("later.txt");
        QFile newFile(later);
        QVERIFY(newFile.open(QIODevice::WriteOnly));
        newFile.close();
        refresh->trigger();
        QTRY_COMPARE(table(window)->rowCount(), 3);
        QCOMPARE(table(window)->selectionModel()->selectedRows().size(), 1);
        QCOMPARE(table(window)
                     ->model()
                     ->index(table(window)->selectionModel()->selectedRows().first().row(), 0)
                     .data()
                     .toString(),
                 QStringLiteral("файл с пробелом.txt"));
        QCOMPARE(backend.listCalls, 0);
    }

    void refreshArchiveKeepsFolderAndSelection() {
        ControlledBackend backend;
        MainWindow window(&backend);
        ArchiveEntry first;
        first.path = QStringLiteral("папка с пробелом/первый.txt");
        openListedArchive(window, backend, "example.7z", {first});
        openRow(window, rowWithPath(window, QStringLiteral("папка с пробелом")));
        QCOMPARE(table(window)->rowCount(), 1);
        table(window)->selectRow(0);
        auto *list = static_cast<FileTable *>(table(window));
        CMyComPtr<IFolderFolder> provider = list->sourcePanel->GetFolder();
        const QString address = window.findChild<QLineEdit *>("archivePath")->text();
        const int listings = backend.listCalls;
        QSignalSpy publication(list->model(), &QAbstractItemModel::rowsRemoved);
        for (int count = 1; count <= 3; ++count) {
            action(window, "refreshAction")->trigger();
            QCOMPARE(backend.listCalls, listings);
            QCOMPARE(list->sourcePanel->GetFolder(), (IFolderFolder *)provider);
            QCOMPARE(publication.count(), count);
            QCOMPARE(list->rowCount(), 1);
            QCOMPARE(list->selectionModel()->selectedRows().size(), 1);
            QCOMPARE(list->model()
                         ->index(list->selectionModel()->selectedRows().first().row(), 0)
                         .data()
                         .toString(),
                     QStringLiteral("первый.txt"));
            QCOMPARE(window.findChild<QLineEdit *>("archivePath")->text(), address);
            QVERIFY(action(window, "refreshAction")->isEnabled());
        }
    }
};

QTEST_MAIN(MainWindowStateTests)
#include "ui_state_tests.moc"
