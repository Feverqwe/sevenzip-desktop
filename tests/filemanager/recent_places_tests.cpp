#include "filemanager/support/recent_places_menu.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/window/qtrecentplacesstore.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "platform/support/external_editor_fixture.h"
#include "support/panel_keys.h"
#include "support/window_activation.h"
#include "ui/support/layout_inspection.h"
#include <QMenu>
#include <QToolButton>

class RecentPlacesTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    static QtRecentPlacesStore &store() {
        return *QtRecentPlacesStore::ForSettings(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
    }
    static FileTable *active(MainWindow &window) {
        for (auto *pane : window.findChildren<QWidget *>("filePanel0"))
            if (pane->isVisible())
                return pane->findChild<FileTable *>("archiveTable");
        return nullptr;
    }
    static QString location(MainWindow &window) {
        return QDir::cleanPath(ToQString(active(window)->sourcePanel->GetCurrentFolderPrefix()));
    }
    static void directory(const QString &path, int count = 1) {
        QVERIFY(QDir().mkpath(path));
        for (int index = 0; index < count; ++index)
            QVERIFY(ExternalEditorFixture::write(
                QDir(path).filePath(QString("item-%1.txt").arg(index)), "control payload"));
    }
  private slots:
    void initTestCase() { UiInspection::configureReviewAppearance(); }
    void init() {
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        store().Reload();
        SaveRegLang(UString("-"), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
    }

    void addressMenuLimitsHistoryAndEscapePreservesLocation_data() {
        QTest::addColumn<QString>("language");
        QTest::addColumn<QSize>("size");
        for (const QString &language : {QString("-"), QString("ru")})
            for (const QSize &size : {QSize(980, 620), QSize(640, 400)})
                QTest::newRow(qPrintable(language + QString::number(size.width())))
                    << language << size;
    }
    void addressMenuLimitsHistoryAndEscapePreservesLocation() {
        QFETCH(QString, language);
        QFETCH(QSize, size);
        SaveRegLang(ToUString(language), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString initial = fixture.filePath("Source");
        directory(initial);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.resize(size);
        window.show();
        window.openPath(initial);
        QTRY_COMPARE(location(window), initial);
        QStringList paths;
        for (int i = 0; i < 7; ++i) {
            const QString path = fixture.filePath(QString("Папка & 空 %1").arg(i));
            directory(path);
            store().Visit({path, {}, {}});
            paths.prepend(path);
        }
        auto *button = window.findChild<QToolButton *>("panelRecentPlacesButton0");
        QVERIFY(button && button->isVisible());
        QTest::mouseClick(button, Qt::LeftButton);
        QTRY_VERIFY(RecentPlacesMenu::Visible(window));
        auto *popup = RecentPlacesMenu::Visible(window);
        QCOMPARE(popup->actions().size(), 5);
        for (qsizetype i = 0; i < popup->actions().size(); ++i)
            QCOMPARE(QDir::cleanPath(popup->actions()[i]->data().toString()), paths[i]);
        QVERIFY(UiInspection::fullyVisible(*button, window));
        QVERIFY(UiInspection::writeReport(*popup, "recent-places-" + language +
                                                      QString::number(size.width())));
        QTest::keyClick(popup, Qt::Key_Escape);
        QTRY_VERIFY(!RecentPlacesMenu::Visible(window));
        QCOMPARE(location(window), initial);
        button->click();
        QTRY_VERIFY(RecentPlacesMenu::Visible(window));
        popup = RecentPlacesMenu::Visible(window);
        auto *entry = RecentPlacesMenu::Find(*popup, paths.first());
        QVERIFY(entry);
        RecentPlacesMenu::Choose(*popup, *entry);
        QTRY_COMPARE(location(window), paths.first());
        action(window, "backAction")->trigger();
        QTRY_COMPARE(location(window), initial);
    }

    void missingPlaceKeepsLocationAndHistoryCursor() {
        QTemporaryDir fixture;
        const QString a = fixture.filePath("Available"), missing = fixture.filePath("Missing");
        directory(a);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openPath(a);
        store().Visit({missing, {}, {}});
        action(window, "foldersHistoryAction")->trigger();
        QTRY_VERIFY(RecentPlacesMenu::Visible(window));
        auto *popup = RecentPlacesMenu::Visible(window);
        auto *entry = RecentPlacesMenu::Find(*popup, missing);
        QVERIFY(entry);
        const auto *provider = active(window)->sourcePanel->GetFolder();
        RecentPlacesMenu::Choose(*popup, *entry);
        QTRY_VERIFY(!window.statusBar()->currentMessage().isEmpty());
        QCOMPARE(location(window), a);
        QCOMPARE(active(window)->sourcePanel->GetFolder(), provider);
        QVERIFY(store().IndexOf(missing) >= 0);
        QVERIFY(!action(window, "forwardAction")->isEnabled());
        directory(missing);
        action(window, "foldersHistoryAction")->trigger();
        QTRY_VERIFY(RecentPlacesMenu::Visible(window));
        popup = RecentPlacesMenu::Visible(window);
        entry = RecentPlacesMenu::Find(*popup, missing);
        QVERIFY(entry);
        RecentPlacesMenu::Choose(*popup, *entry);
        QTRY_COMPARE(location(window), missing);
        action(window, "backAction")->trigger();
        QTRY_COMPARE(location(window), a);
    }

    void openingReturnsToCapturedPanel() {
        QTemporaryDir fixture;
        const QString a = fixture.filePath("A"), b = fixture.filePath("B"),
                      c = fixture.filePath("C");
        directory(a, 301);
        directory(b);
        directory(c);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openPath(a);
        QTRY_VERIFY(active(window)->isEnabled() && active(window)->rowCount() >= 301);
        action(window, "twoPanelsAction")->trigger();
        auto *right = window.findChild<QWidget *>("filePanel1");
        QVERIFY(right);
        auto *address = right->findChild<QLineEdit *>("archivePath");
        PanelKeys::focusAddress(address);
        address->setText(c);
        QTest::keyClick(address, Qt::Key_Return);
        auto *other = right->findChild<FileTable *>("archiveTable");
        QTRY_COMPARE(QDir::cleanPath(ToQString(other->sourcePanel->GetCurrentFolderPrefix())), c);
        QVERIFY(WindowActivation::waitForActive(window));
        active(window)->setFocus();
        QTRY_VERIFY(active(window)->hasFocus());
        store().Visit({b, {}, {}});
        action(window, "foldersHistoryAction")->trigger();
        QTRY_VERIFY(RecentPlacesMenu::Visible(window));
        auto *popup = RecentPlacesMenu::Visible(window);
        other->setFocus();
        auto *entry = RecentPlacesMenu::Find(*popup, b);
        QVERIFY(entry);
        RecentPlacesMenu::Choose(*popup, *entry);
        QTRY_COMPARE(location(window), b);
        QCOMPARE(QDir::cleanPath(ToQString(other->sourcePanel->GetCurrentFolderPrefix())), c);
    }

    void closedNestedArchiveReopensThroughRecordedNativeTarget() {
        QTemporaryDir fixture;
        const QString folder = QStringLiteral("dir 空/sub папка");
        const QString entry = folder + "/payload.txt";
        QVERIFY(QDir().mkpath(fixture.filePath(folder)));
        QVERIFY(ExternalEditorFixture::write(fixture.filePath(entry), "nested recent bytes"));
        const QString inner = fixture.filePath("inner δ.zip"),
                      outer = fixture.filePath("outer.zip");
        QVERIFY(ExternalEditorFixture::createArchive(fixture.path(), inner, "dir 空"));
        QVERIFY(ExternalEditorFixture::createArchive(fixture.path(), outer,
                                                     QFileInfo(inner).fileName()));
        const QString plain = fixture.filePath("Plain");
        directory(plain);
        ArchiveBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openArchive(outer);
        QTRY_VERIFY(rowWithPath(window, QFileInfo(inner).fileName()) >= 0);
        openRow(window, rowWithPath(window, QFileInfo(inner).fileName()));
        QTRY_VERIFY(rowWithPath(window, "dir 空") >= 0);
        openRow(window, rowWithPath(window, "dir 空"));
        QTRY_VERIFY(rowWithPath(window, folder) >= 0);
        openRow(window, rowWithPath(window, folder));
        QTRY_VERIFY(rowWithPath(window, entry) >= 0);
        const QString requested = ToQString(active(window)->sourcePanel->GetCurrentFolderPrefix());
        window.openPath(plain);
        action(window, "foldersHistoryAction")->trigger();
        QTRY_VERIFY(RecentPlacesMenu::Visible(window));
        auto *popup = RecentPlacesMenu::Visible(window);
        auto *recent = RecentPlacesMenu::Find(*popup, requested);
        QVERIFY(recent);
        RecentPlacesMenu::Choose(*popup, *recent);
        QTRY_COMPARE(ToQString(active(window)->sourcePanel->GetCurrentFolderPrefix()), requested);
        QCOMPARE(active(window)->sourcePanel->GetParentFolders().Size(), 2u);
        QCOMPARE(ToQString(active(window)->sourcePanel->GetCurrentFolderPrefix()), requested);
        const int row = rowWithPath(window, entry);
        QVERIFY(row >= 0);
        active(window)->selectRow(row);
        active(window)->setCurrentCell(row, 0, QItemSelectionModel::NoUpdate);
        const QString output = fixture.filePath("exported");
        window.extractTo(output, true);
        QCOMPARE(ExternalEditorFixture::read(output + "/payload.txt"), "nested recent bytes");
    }

    void computerRecentKeepsItsTypeAcrossLanguageChange() {
        QTemporaryDir fixture;
        directory(fixture.filePath("Plain"));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        action(window, "openRootAction")->trigger();
        QVERIFY(active(window)->sourcePanel->IsFSDrivesFolder());
        const QString previous = ToQString(active(window)->sourcePanel->GetCurrentFolderPrefix());
        window.openPath(fixture.filePath("Plain"));
        SaveRegLang(UString("ru"), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QtNotifyLanguageChanged();
        action(window, "foldersHistoryAction")->trigger();
        QTRY_VERIFY(RecentPlacesMenu::Visible(window));
        auto *popup = RecentPlacesMenu::Visible(window);
        auto *recent = RecentPlacesMenu::Find(*popup, previous);
        QVERIFY(recent);
        QCOMPARE(recent->text(), QString(action(window, "openRootAction")->text()).remove('&'));
        RecentPlacesMenu::Choose(*popup, *recent);
        QTRY_VERIFY(active(window)->sourcePanel->IsFSDrivesFolder());
        QVERIFY(!active(window)->sourcePanel->IsFSFolder());
        QVERIFY(!action(window, "createFolderAction")->isEnabled());
    }
};

QTEST_MAIN(RecentPlacesTests)
#include "recent_places_tests.moc"
