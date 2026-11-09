#include "filemanager/panels/Panel.h"
#include "filemanager/support/window_state_support.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "settings/RegistryUtils.h"

class BookmarkTests : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
  private slots:
    void sourceMenuCaptionsHotSwitchAndLongPaths() {
        const QString settingsPath = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settingsPath);
        const QString longPath =
            "/" + QString(60, 'a') + QStringLiteral("/目录/") + QString(60, 'z');
        {
            QSettings settings(settingsPath, QSettings::IniFormat);
            settings.setValue("FolderShortcuts", QStringList{longPath});
        }
        ControlledBackend backend;
        MainWindow window(&backend);
        auto *open = action(window, "openBookmark0Action");
        auto *empty = action(window, "openBookmark1Action");
        auto *save = action(window, "saveBookmark0Action");
        const QKeySequence openShortcut = open->shortcut(), saveShortcut = save->shortcut();
        const QString caption = longPath.left(50) + " ... " + longPath.right(50) + "\tAlt+0";
        for (const char *lang : {"-", "ru", "-"}) {
            SaveRegLang(UString(lang), settingsPath);
            ReloadLang(settingsPath);
            QtNotifyLanguageChanged();
            QCOMPARE(action(window, "openBookmark0Action"), open);
            QCOMPARE(action(window, "saveBookmark0Action"), save);
            QCOMPARE(open->text(), caption);
            QCOMPARE(open->toolTip(), longPath);
            QCOMPARE(empty->text(), QString("-\tAlt+1"));
            QCOMPARE(save->text(), QString(lang) == "ru" ? QStringLiteral("Закладка 0\tAlt+Shift+0")
                                                         : QString("Bookmark 0\tAlt+Shift+0"));
            QCOMPARE(open->shortcut(), openShortcut);
            QCOMPARE(save->shortcut(), saveShortcut);
            QCOMPARE(backend.listCalls, 0);
        }
    }
    void upstreamSparseSlotsAndPersistence() {
        QTemporaryDir temp;
        const QString path = temp.filePath("favorites.ini");
        CFastFolders folders;
        const QString value = QStringLiteral("/папка с пробелом/😀.zip / внутри");
        CPanel::SetBookmark(folders, 9, ToUString(value));
        QCOMPARE(folders.Strings.Size(), 10u);
        for (unsigned i = 0; i < 9; ++i)
            QVERIFY(CPanel::OpenBookmark(folders, i).IsEmpty());
        QVERIFY(CPanel::OpenBookmark(folders, 10).IsEmpty());
        folders.Save(path);
        CFastFolders restored;
        restored.Read(path);
        QCOMPARE(ToQString(CPanel::OpenBookmark(restored, 9)), value);
        QCOMPARE(restored.Strings.Size(), 10u);
        CPanel::SetBookmark(restored, 9, UString());
        restored.Save(path);
        restored.Read(path);
        QVERIFY(restored.GetString(9).IsEmpty());
    }

    void mvpSettingsAreIgnored() {
        QTemporaryDir temp;
        const QString path = temp.filePath("favorites.ini");
        {
            QSettings settings(path, QSettings::IniFormat);
            settings.setValue("favorites/2/path", QStringLiteral("/архив 😀.7z"));
            settings.setValue("favorites/2/folder", QStringLiteral("папка/внутри"));
            settings.setValue("favorites/8/path", QStringLiteral("/обычная папка"));
            settings.setValue("history/paths", QStringList{"unrelated history"});
        }
        CFastFolders folders;
        folders.Read(path);
        QVERIFY(folders.Strings.IsEmpty());
        QVERIFY(folders.GetString(0).IsEmpty());
        folders.Strings.Clear();
        folders.Save(path);
        folders.Read(path);
        QVERIFY(folders.Strings.IsEmpty());
        // FolderShortcuts must never accidentally read MVP history/favorites.
    }

    void bookmarksNavigateAndSavePersistsFullPath() {
        const QString settingsPath = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settingsPath);
        QTemporaryDir temp;
        const QString directory = temp.filePath(QStringLiteral("папка & данные"));
        QVERIFY(QDir().mkpath(directory));
        {
            QSettings settings(settingsPath, QSettings::IniFormat);
            QStringList paths;
            for (int i = 0; i < 10; ++i)
                paths.append(i == 3 ? directory : QString());
            settings.setValue("FolderShortcuts", paths);
        }
        ControlledBackend backend;
        MainWindow window(&backend);
        auto *bookmark = action(window, "openBookmark3Action");
        QVERIFY(QKeySequence::mnemonic(bookmark->text()).isEmpty());
        QCOMPARE(bookmark->toolTip(), directory);
        QVERIFY(bookmark->text().endsWith(QStringLiteral("папка && данные\tAlt+3")));
        bookmark->trigger();
        auto *address = window.findChild<QLineEdit *>("archivePath");
        QCOMPARE(address->text(), QFileInfo(directory).canonicalFilePath());
        const QString archivePath = temp.filePath(QStringLiteral("архив 😀.zip"));
        QFile file(archivePath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        openListedArchive(window, backend, archivePath,
                          {ArchiveEntry{QStringLiteral("папка/внутри/файл.txt"), 1, 1}});
        openRow(window, rowWithPath(window, QStringLiteral("папка")));
        openRow(window, rowWithPath(window, QStringLiteral("папка/внутри")));
        action(window, "saveBookmark7Action")->trigger();
        const QString location = archivePath + QStringLiteral(" / папка/внутри");
        QSettings settings(settingsPath, QSettings::IniFormat);
        const QStringList paths = settings.value("FolderShortcuts").toStringList();
        QCOMPARE(paths.size(), 10);
        QCOMPARE(paths[3], directory);
        QCOMPARE(QDir::fromNativeSeparators(paths[7]),
                 archivePath + QStringLiteral("/папка/внутри/"));
        ControlledBackend reopenedBackend;
        MainWindow reopened(&reopenedBackend);
        reopenedBackend.queueList({ArchiveEntry{QStringLiteral("папка/внутри/файл.txt"), 1, 1}});
        action(reopened, "openBookmark7Action")->trigger();
        QCOMPARE(QDir::fromNativeSeparators(reopenedBackend.lastArchive), archivePath);
        QCOMPARE(reopened.findChild<QLineEdit *>("archivePath")->text(), location);
    }

    void missingArchiveFolderUsesSurvivingParentWithoutReload() {
        const QString settingsPath = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settingsPath);
        QTemporaryDir temp;
        const QString archivePath = temp.filePath(QStringLiteral("архив с пробелом.zip"));
        QFile file(archivePath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        ControlledBackend backend;
        MainWindow window(&backend);
        openListedArchive(window, backend, archivePath,
                          {ArchiveEntry{QStringLiteral("parent/deep/file.txt"), 1, 1}});
        openRow(window, rowWithPath(window, "parent"));
        openRow(window, rowWithPath(window, "parent/deep"));
        action(window, "saveBookmark2Action")->trigger();
        auto *address = window.findChild<QLineEdit *>("archivePath");
        address->setText(temp.path());
        QTest::keyClick(address, Qt::Key_Return);
        openListedArchive(window, backend, archivePath, {ArchiveEntry{"parent/other.txt", 1, 1}});
        const int calls = backend.listCalls;
        action(window, "openBookmark2Action")->trigger();
        QCOMPARE(backend.listCalls, calls);
        QCOMPARE(window.findChild<QLineEdit *>("archivePath")->text(), archivePath + " / parent");
        QVERIFY(QFile::remove(archivePath));
        address->setText(temp.path());
        QTest::keyClick(address, Qt::Key_Return);
        action(window, "openBookmark2Action")->trigger();
        QCOMPARE(address->text(), QFileInfo(temp.path()).canonicalFilePath());
        QCOMPARE(backend.listCalls, calls);
    }
};
QTEST_MAIN(BookmarkTests)
#include "bookmark_tests.moc"
