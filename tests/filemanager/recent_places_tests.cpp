#include "dialogs/recentplaces/qtrecentplacesdialog.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/window/qtrecentplacesstore.h"
#include "filemanager/window/qtwindowworkspace.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "platform/support/external_editor_fixture.h"
#include "support/window_activation.h"
#include "ui/controls/qtsourcesummary.h"
#include "ui/support/layout_inspection.h"
#include <QListWidget>
#include <QTabBar>
#include <QToolButton>

class RecentPlacesTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    static QtRecentPlacesStore &store() {
        return *QtRecentPlacesStore::ForSettings(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
    }
    static QtWindowWorkspace &workspace(MainWindow &window) {
        for (auto *object : window.children())
            if (auto *owner = dynamic_cast<QtWindowWorkspace *>(object))
                return *owner;
        qFatal("Missing workspace");
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
    static QtRecentPlacesDialog *dialog(MainWindow &window) {
        for (auto *item : window.findChildren<QDialog *>("foldersHistoryDialog"))
            if (item->isVisible())
                return dynamic_cast<QtRecentPlacesDialog *>(item);
        return nullptr;
    }
    static QListWidget *list(QtRecentPlacesDialog &dialog) {
        return dialog.findChild<QListWidget *>("foldersHistoryList");
    }
    static void choose(QtRecentPlacesDialog &dialog, const QString &path) {
        auto *items = list(dialog);
        for (int row = 0; row < items->count(); ++row)
            if (items->item(row)->data(Qt::UserRole).toString() == path) {
                items->setCurrentRow(row);
                return;
            }
        QFAIL("Requested recent place is absent");
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

    void clearWithFilterIsImmediateAndPreservesOtherOwners() {
        QTemporaryDir fixture;
        const QString a = fixture.filePath("Previous 空"), b = fixture.filePath("Current папка");
        directory(a);
        directory(b);
        ControlledBackend firstBackend, secondBackend;
        MainWindow first(&firstBackend), second(&secondBackend);
        first.show();
        second.show();
        first.openPath(a);
        action(first, "saveBookmark4Action")->trigger();
        first.openPath(b);
        action(first, "backAction")->trigger();
        QTRY_COMPARE(location(first), a);
        second.openPath(b);
        UStringVector destinations;
        destinations.Add(ToUString(a));
        SaveCopyHistory(destinations, qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        action(first, "foldersHistoryAction")->trigger();
        auto *form = dialog(first);
        QVERIFY(form);
        form->findChild<QLineEdit *>("recentPlacesSearch")->setText("Previous");
        QCOMPARE(list(*form)->count(), 1);
        form->findChild<QPushButton *>("recentPlacesRemove")->click();
        QCOMPARE(store().IndexOf(a + '/'), -1);
        QVERIFY(QFileInfo::exists(a + "/item-0.txt"));
        form->findChild<QPushButton *>("recentPlacesClear")->click();
        QCOMPARE(store().rowCount(), 0);
        QCOMPARE(form->findChild<QLineEdit *>("recentPlacesSearch")->text(), "Previous");
        QVERIFY(!form->findChild<QPushButton *>("recentPlacesOpen")->isEnabled());
        form->reject();
        second.close(); // Its native AppState must not resurrect old history.
        CFastFolders bookmarks;
        bookmarks.Read(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QCOMPARE(QDir::cleanPath(ToQString(bookmarks.GetString(4))), a);
        CFolderHistory saved;
        saved.Read(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        UStringVector paths;
        saved.GetList(paths);
        QVERIFY(paths.IsEmpty());
        UStringVector retained;
        ReadCopyHistory(retained, qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QCOMPARE(retained.Size(), 1u);
        QCOMPARE(ToQString(retained[0]), a);
        action(first, "forwardAction")->trigger();
        QTRY_COMPARE(location(first), b);
        QVERIFY(QFileInfo::exists(b + "/item-0.txt"));
    }

    void searchKeyboardEmptyStatesAndWrappedRows_data() {
        QTest::addColumn<QString>("language");
        QTest::newRow("en") << QString("-");
        QTest::newRow("ru") << QString("ru");
    }
    void searchKeyboardEmptyStatesAndWrappedRows() {
        QFETCH(QString, language);
        SaveRegLang(ToUString(language), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        const QString longPath =
            "/" + QString("длинный path 空/").repeated(18) + "source.7z/folder";
        store().Visit({"/Other place", {}, {}});
        store().Visit({longPath, {}, {}});
        QtRecentPlacesDialog form(store());
        QString requested;
        form.SetOpenHandler([&](const QtRecentPlace &place) {
            requested = place.path;
            form.Complete(
                false, QCoreApplication::translate("QtWindowWorkspace",
                                                   "The location is unavailable or disconnected."));
        });
        form.show();
        QVERIFY(WindowActivation::waitForActive(form));
        QApplication::processEvents();
        QVERIFY(
            UiInspection::writeReport(form, QString("recent-places-%1-populated").arg(language)));
        auto *search = form.findChild<QLineEdit *>("recentPlacesSearch");
        auto *items = list(form);
        QCOMPARE(items->currentRow(), 0);
        search->setFocus();
        QTest::keyClick(search, Qt::Key_Down);
        QTRY_VERIFY(items->hasFocus());
        QTest::keyClick(items, Qt::Key_End);
        QCOMPARE(items->currentRow(), items->count() - 1);
        QTest::keyClick(items, Qt::Key_Home);
        QCOMPARE(items->currentRow(), 0);
        search->setText("SOURCE.7Z");
        QCOMPARE(items->count(), 1);
        search->setFocus();
        QTest::keyClick(search, Qt::Key_Return);
        QCOMPARE(requested, longPath);
        QVERIFY(form.isVisible());
        QCOMPARE(search->text(), "SOURCE.7Z");
        QVERIFY(form.findChild<QLabel *>("recentPlacesError")->isVisible());
        for (const QSize size : {QSize(600, 480), QSize(480, 640)}) {
            form.resize(size);
            QApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(*items, form));
            for (auto *button : form.findChildren<QPushButton *>())
                if (button->isVisible())
                    QVERIFY(UiInspection::fullyVisible(*button, form));
            auto *summary = static_cast<QtSourceSummary *>(items->itemWidget(items->item(0)));
            for (auto *label : summary->findChildren<QLabel *>())
                QVERIFY(label->geometry().right() <= summary->width());
            QVERIFY(UiInspection::writeReport(
                form, QString("recent-places-%1-%2").arg(language).arg(size.width())));
        }
        search->setText("no such result");
        QApplication::processEvents();
        auto *empty = form.findChild<QLabel *>("recentPlacesEmpty");
        const QString noMatch = empty->text();
        QVERIFY(!form.findChild<QPushButton *>("recentPlacesOpen")->isEnabled());
        QVERIFY(
            UiInspection::writeReport(form, QString("recent-places-%1-no-results").arg(language)));
        form.findChild<QPushButton *>("recentPlacesClear")->click();
        QApplication::processEvents();
        QVERIFY(empty->text() != noMatch);
        QVERIFY(search->isEnabled());
        QVERIFY(UiInspection::writeReport(form, QString("recent-places-%1-empty").arg(language)));
        search->setFocus();
        QTest::keyClick(search, Qt::Key_Escape);
        QTRY_VERIFY(!form.isVisible());
    }

    void missingPlaceKeepsFormSourceAndHistoryCursor() {
        QTemporaryDir fixture;
        const QString a = fixture.filePath("Available"), missing = fixture.filePath("Missing");
        directory(a);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openPath(a);
        store().Visit({missing, {}, {}});
        action(window, "foldersHistoryAction")->trigger();
        auto *form = dialog(window);
        QVERIFY(form);
        form->findChild<QLineEdit *>("recentPlacesSearch")->setText("Missing");
        choose(*form, missing);
        const auto *provider = active(window)->sourcePanel->GetFolder();
        form->accept();
        QTRY_VERIFY(form->findChild<QLabel *>("recentPlacesError")->isVisible());
        QVERIFY(UiInspection::writeReport(*form, "recent-places-unavailable"));
        QCOMPARE(location(window), a);
        QCOMPARE(active(window)->sourcePanel->GetFolder(), provider);
        QCOMPARE(form->findChild<QLineEdit *>("recentPlacesSearch")->text(), "Missing");
        QCOMPARE(list(*form)->currentItem()->data(Qt::UserRole).toString(), missing);
        QVERIFY(store().IndexOf(missing) >= 0);
        QVERIFY(!action(window, "forwardAction")->isEnabled());
        directory(missing);
        form->accept();
        QTRY_VERIFY(!dialog(window));
        QCOMPARE(location(window), missing);
        action(window, "backAction")->trigger();
        QTRY_COMPARE(location(window), a);
    }

    void openingReturnsToCapturedTabAndClosedOwnerReportsError() {
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
        auto *original = active(window)->sourcePanel;
        store().Visit({b, {}, {}});
        action(window, "foldersHistoryAction")->trigger();
        auto *form = dialog(window);
        QVERIFY(form);
        workspace(window).NewTab(0, c);
        QTRY_COMPARE(location(window), c);
        workspace(window).MarkDirty(*original);
        choose(*form, b);
        form->accept();
        QTRY_VERIFY(!dialog(window));
        QCOMPARE(location(window), b);
        auto *tabs = window.findChild<QTabBar *>("panelTabs0");
        QCOMPARE(tabs->currentIndex(), 0);
        action(window, "foldersHistoryAction")->trigger();
        form = dialog(window);
        QVERIFY(form);
        workspace(window).CloseTab(0, 0);
        QTRY_COMPARE(location(window), c);
        choose(*form, b);
        form->accept();
        QTRY_VERIFY(form->findChild<QLabel *>("recentPlacesError")->isVisible());
        QCOMPARE(tabs->count(), 1);
        QCOMPARE(location(window), c);
    }

    void closingPendingFormCancelsItsPublication() {
        QTemporaryDir fixture;
        const QString a = fixture.filePath("A"), b = fixture.filePath("B"),
                      c = fixture.filePath("C");
        directory(a);
        directory(b, 301);
        directory(c);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openPath(a);
        store().Visit({b, {}, {}});
        store().Visit({c, {}, {}});
        action(window, "foldersHistoryAction")->trigger();
        auto *form = dialog(window);
        QVERIFY(form);
        choose(*form, b);
        form->accept();
        form->reject();
        QTRY_COMPARE(location(window), a);
        QTRY_VERIFY(active(window)->sourcePanel->ShouldProcessNotifications());
        QCOMPARE(QDir::cleanPath(store().Place(0).path), c);
        QCOMPARE(active(window)->rowCount(), 1);
        QVERIFY(!action(window, "forwardAction")->isEnabled());
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
        auto *form = dialog(window);
        QVERIFY(form);
        choose(*form, requested);
        form->accept();
        QTRY_VERIFY(!dialog(window));
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
        auto *form = dialog(window);
        QVERIFY(form);
        choose(*form, previous);
        auto *summary =
            static_cast<QtSourceSummary *>(list(*form)->itemWidget(list(*form)->currentItem()));
        QCOMPARE(summary->accessibleName(),
                 QString(action(window, "openRootAction")->text()).remove('&'));
        form->accept();
        QTRY_VERIFY(!dialog(window));
        QVERIFY(active(window)->sourcePanel->IsFSDrivesFolder());
        QVERIFY(!active(window)->sourcePanel->IsFSFolder());
        QVERIFY(!action(window, "createFolderAction")->isEnabled());
    }
};

QTEST_MAIN(RecentPlacesTests)
#include "recent_places_tests.moc"
