#include "filemanager/support/window_state_support.h"

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
