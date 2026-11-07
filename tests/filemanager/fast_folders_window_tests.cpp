#include "filemanager/support/window_state_support.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include <QScopeGuard>

class FastFoldersWindowTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    static QString location(MainWindow &window) {
        return QDir::cleanPath(ToQString(table(window)->sourcePanel->GetCurrentFolderPrefix()));
    }
    static void folder(const QString &path) {
        QVERIFY(QDir().mkpath(path));
        QFile file(QDir(path).filePath("control.txt"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("bookmark control"), 16);
    }
  private slots:
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }
    void nativeBookmarkReopensAfterChangingLanguage() {
        const QByteArray settings = qgetenv("SEVENZIP_SETTINGS_PATH");
        const QString settingsPath = QString::fromUtf8(settings);
        SaveRegLang(UString("-"), settingsPath);
        ReloadLang(settingsPath);
        const auto restore = qScopeGuard([&] {
            qputenv("SEVENZIP_SETTINGS_PATH", settings);
            SaveRegLang(UString("-"), settingsPath);
            ReloadLang(settingsPath);
        });
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString directory = fixture.filePath("ordinary папка");
        folder(directory);

        ControlledBackend firstBackend, secondBackend;
        MainWindow first(&firstBackend), second(&secondBackend);
        first.show();
        second.show();
        first.openPath(directory);
        second.openPath(directory);
        action(first, "openRootAction")->trigger();
        QVERIFY(table(first)->sourcePanel->IsFSDrivesFolder());
        action(first, "saveBookmark4Action")->trigger();
        const QString originalLabel = action(second, "openBookmark4Action")->toolTip();
        QCOMPARE(originalLabel, ToQString(table(first)->sourcePanel->GetCurrentFolderPrefix()));

        SaveRegLang(UString("ru"), settingsPath);
        ReloadLang(settingsPath);
        QtNotifyLanguageChanged();
        action(second, "openBookmark4Action")->trigger();
        QVERIFY(table(second)->sourcePanel->IsFSDrivesFolder());
        const QString translatedLabel =
            ToQString(table(second)->sourcePanel->GetCurrentFolderPrefix());
        QCOMPARE(action(second, "openBookmark4Action")->toolTip(), translatedLabel);
#ifndef Q_OS_WIN
        QVERIFY(translatedLabel != originalLabel);
#endif

        first.close();
        second.close();
        const QString restoredSettings = fixture.filePath("restored.ini");
        QVERIFY(QFile::copy(settingsPath, restoredSettings));
        qputenv("SEVENZIP_SETTINGS_PATH", restoredSettings.toUtf8());
        ControlledBackend restoredBackend;
        MainWindow restored(&restoredBackend);
        restored.show();
        restored.openPath(directory);
        auto *bookmark = action(restored, "openBookmark4Action");
        QCOMPARE(bookmark->toolTip(), translatedLabel);
        bookmark->trigger();
        QVERIFY(table(restored)->sourcePanel->IsFSDrivesFolder());

        restored.openPath(directory);
        action(restored, "saveBookmark4Action")->trigger();
        action(restored, "openRootAction")->trigger();
        bookmark->trigger();
        QCOMPARE(location(restored), QFileInfo(directory).canonicalFilePath());
    }
    void sharedSlotsSurviveWindowClose_data() {
        QTest::addColumn<bool>("closeFirst");
        QTest::newRow("close-first-editor") << true;
        QTest::newRow("close-second-editor") << false;
    }
    void sharedSlotsSurviveWindowClose() {
        QFETCH(bool, closeFirst);
        QTemporaryDir fixture;
        const QString a = fixture.filePath("first 空"), b = fixture.filePath("second папка"),
                      legacyPath = fixture.filePath("native slot");
        folder(a);
        folder(b);
        folder(legacyPath);
        ControlledBackend firstBackend, secondBackend;
        MainWindow first(&firstBackend), second(&secondBackend);
        first.show();
        second.show();
        first.openPath(a);
        second.openPath(b);
        action(first, "saveBookmark4Action")->trigger();
        QTRY_COMPARE(QDir::cleanPath(action(second, "openBookmark4Action")->toolTip()), a);
        action(second, "openBookmark4Action")->trigger();
        QTRY_COMPARE(location(second), a);

        // Preserve another slot changed through the existing native API too.
        CFastFolders legacy;
        legacy.Read(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        CPanel::SetBookmark(legacy, 2, ToUString(legacyPath));
        legacy.Save(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        second.openPath(b);
        action(second, "saveBookmark9Action")->trigger();
        QTRY_COMPARE(QDir::cleanPath(action(first, "openBookmark9Action")->toolTip()), b);
        QCOMPARE(QDir::cleanPath(action(first, "openBookmark2Action")->toolTip()), legacyPath);
        if (closeFirst)
            first.close();
        else
            second.close();
        CFastFolders persisted;
        persisted.Read(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QCOMPARE(QDir::cleanPath(ToQString(persisted.GetString(4))), a);
        QCOMPARE(QDir::cleanPath(ToQString(persisted.GetString(9))), b);
        QCOMPARE(QDir::cleanPath(ToQString(persisted.GetString(2))), legacyPath);
        if (closeFirst)
            second.close();
        else
            first.close();
        ControlledBackend restoredBackend;
        MainWindow restored(&restoredBackend);
        restored.show();
        action(restored, "openBookmark4Action")->trigger();
        QTRY_COMPARE(location(restored), a);
        action(restored, "openBookmark9Action")->trigger();
        QTRY_COMPARE(location(restored), b);
        action(restored, "openBookmark2Action")->trigger();
        QTRY_COMPARE(location(restored), legacyPath);
        QVERIFY(QFileInfo::exists(a + "/control.txt"));
        QVERIFY(QFileInfo::exists(b + "/control.txt"));
    }
};

QTEST_MAIN(FastFoldersWindowTests)
#include "fast_folders_window_tests.moc"
