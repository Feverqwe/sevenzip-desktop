#include "filemanager/support/recent_places_menu.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/window/qtfavoritestore.h"
#include "filemanager/window/qtrecentplacesstore.h"
#include "fileops/scanner/filesystemscanner.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "platform/support/external_editor_fixture.h"
#include "support/panel_keys.h"
#include "support/widget_dialogs.h"
#include "support/window_activation.h"
#include "ui/support/layout_inspection.h"
#include <QInputDialog>
#include <QMenu>
#include <QTimer>
#include <QToolButton>

class RecentPlacesTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    WidgetDialogFixture dialogs_;
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
    static void addFavorite(MainWindow &window, const QString &name) {
        auto *add = window.findChild<QAction *>("addFavoriteAction");
        QVERIFY(add);
        bool entered = false;
        QTimer responder;
        connect(&responder, &QTimer::timeout, &window, [&] {
            if (auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget())) {
                dialog->setTextValue(name);
                entered = true;
                dialog->accept();
            }
        });
        responder.start(1);
        add->trigger();
        QVERIFY(entered);
    }
    static void openFavorite(MainWindow &window, const QString &name) {
        auto *menu = window.findChild<QMenu *>("savedPlacesMenu");
        QVERIFY(menu);
        menu->popup(window.mapToGlobal(QPoint(20, 20)));
        QTRY_VERIFY(menu->isVisible());
        for (auto *entry : menu->actions())
            if (entry->text() == name) {
                RecentPlacesMenu::Choose(*menu, *entry);
                return;
            }
        menu->hide();
        QFAIL("Saved place missing from menu");
    }
  private slots:
    void initTestCase() { UiInspection::configureReviewAppearance(); }
    void init() {
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        store().Reload();
        SaveRegLang(UString("-"), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
    }

    void recentPlacesReenableAfterFolderScan() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString first = fixture.filePath("first");
        const QString large = fixture.filePath("large");
        directory(first);
        directory(large, 301);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        window.openPath(first);
        QTRY_COMPARE(location(window), first);
        action(window, "twoPanelsAction")->trigger();
        auto *peer =
            window.findChild<QWidget *>("filePanel1")->findChild<FileTable *>("archiveTable");
        QVERIFY(peer && peer->isVisible());
        QTRY_VERIFY(peer->isEnabled());
        active(window)->setFocus();
        auto *button = window.findChild<QToolButton *>("panelRecentPlacesButton0");
        QVERIFY(button && button->isEnabled());
        window.openPath(large);
        QVERIFY(!active(window)->isEnabled());
        QVERIFY(!button->isEnabled());
        peer->setFocus();
        QVERIFY(peer->hasFocus());
        auto *history = action(window, "foldersHistoryAction");
        QVERIFY(!history->isEnabled());
        QTRY_VERIFY(button->isEnabled());
        QTRY_VERIFY(history->isEnabled());
        QTest::mouseClick(button, Qt::LeftButton);
        QTRY_VERIFY(RecentPlacesMenu::Visible(window));
        auto *menu = RecentPlacesMenu::Visible(window);
        auto *entry = RecentPlacesMenu::Find(*menu, first);
        QVERIFY(entry);
        RecentPlacesMenu::Choose(*menu, *entry);
        QTRY_COMPARE(location(window), first);
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
        auto *parent = window.findChild<QPushButton *>("panelParentButton0");
        QVERIFY(parent && parent->isEnabled());
        QVERIFY(WindowActivation::waitForActive(window));
        parent->setFocus();
        auto *focus = window.focusWidget();
        for (int step = 0; step < 16 && focus && focus != button; ++step) {
            QTest::keyClick(focus, Qt::Key_Tab);
            focus = window.focusWidget();
            if (focus == parent || focus == active(window))
                break;
        }
        QCOMPARE(window.focusWidget(), button);
        QTest::keyClick(button, Qt::Key_Space);
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

    void backgroundFailureReportsToTheInitiatingPane() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString origin = fixture.filePath("Origin");
        const QString peer = fixture.filePath("Peer");
        const QString target = fixture.filePath(QStringLiteral("Large 空"));
        directory(origin);
        directory(peer);
        directory(target, 301);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        action(window, "twoPanelsAction")->trigger();
        auto *left = window.findChild<QWidget *>("filePanel0");
        auto *right = window.findChild<QWidget *>("filePanel1");
        QVERIFY(left && right);
        auto *source = left->findChild<FileTable *>("archiveTable");
        auto *other = right->findChild<FileTable *>("archiveTable");
        QVERIFY(source && other);
        const auto bind = [&](QWidget &pane, const QString &path) {
            auto *address = pane.findChild<QLineEdit *>("archivePath");
            QVERIFY(address);
            PanelKeys::focusAddress(address);
            address->setText(path);
            QTest::keyClick(address, Qt::Key_Return);
            QTRY_COMPARE(address->text(), QFileInfo(path).canonicalFilePath());
        };
        bind(*right, peer);
        bind(*left, origin);
        store().Visit({target, {}, {}});

        const auto scanners = window.findChildren<FilesystemScanner *>();
        QCOMPARE(scanners.size(), 2);
        auto *scanner = scanners[0];
        QSignalSpy failure(scanner, &FilesystemScanner::failed);
        bool switched = false;
        const QString peerStatus = QStringLiteral("Feedback for the other pane");
        const auto interrupt = connect(scanner, &FilesystemScanner::advanced, &window, [&] {
            if (switched)
                return;
            switched = true;
            QTest::mouseClick(other->viewport(), Qt::LeftButton);
            window.statusBar()->showMessage(peerStatus);
            scanner->start(fixture.filePath("missing"), false);
        });
        QTimer responder;
        dialogs_.connectResponder(responder, &window, [&] {
            if (auto *message = qobject_cast<QMessageBox *>(dialogs_.activeDialog()))
                message->accept();
        });
        responder.start(1);
        auto *button = left->findChild<QToolButton *>("panelRecentPlacesButton0");
        QVERIFY(button);
        button->click();
        QTRY_VERIFY(RecentPlacesMenu::Visible(window));
        auto *popup = RecentPlacesMenu::Visible(window);
        auto *entry = RecentPlacesMenu::Find(*popup, target);
        QVERIFY(entry);
        RecentPlacesMenu::Choose(*popup, *entry);
        QTRY_COMPARE(failure.size(), 1);
        responder.stop();
        disconnect(interrupt);
        QVERIFY(switched);
        QTRY_VERIFY(source->isEnabled());
        QCOMPARE(window.statusBar()->currentMessage(), peerStatus);
        QCOMPARE(location(window), origin);
        QCOMPARE(QDir::cleanPath(ToQString(other->sourcePanel->GetCurrentFolderPrefix())), peer);

        QTest::mouseClick(source->viewport(), Qt::LeftButton);
        QVERIFY(!window.statusBar()->currentMessage().isEmpty());
        QVERIFY(window.statusBar()->currentMessage() != peerStatus);
        button->click();
        QTRY_VERIFY(RecentPlacesMenu::Visible(window));
        popup = RecentPlacesMenu::Visible(window);
        entry = RecentPlacesMenu::Find(*popup, target);
        QVERIFY(entry);
        RecentPlacesMenu::Choose(*popup, *entry);
        QTRY_VERIFY(source->isEnabled() && location(window) == target);
        QVERIFY(window.statusBar()->currentMessage().isEmpty());
    }

    void recentPlacesReopenFoldersThatDifferOnlyByCase() {
#ifndef Q_OS_LINUX
        QSKIP("The native Linux path comparison is case-sensitive");
#else
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString upper = fixture.filePath("Reports");
        const QString lower = fixture.filePath("reports");
        directory(upper);
        if (QFileInfo::exists(lower))
            QSKIP("The fixture requires a case-sensitive filesystem");
        directory(lower);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openPath(upper);
        QTRY_COMPARE(location(window), upper);
        QTRY_VERIFY(active(window)->isEnabled());
        window.openPath(lower);
        QTRY_COMPARE(location(window), lower);
        QTRY_VERIFY(active(window)->isEnabled());
        for (const auto &path : {upper, lower}) {
            action(window, "foldersHistoryAction")->trigger();
            QTRY_VERIFY(RecentPlacesMenu::Visible(window));
            auto *popup = RecentPlacesMenu::Visible(window);
            auto *entry = RecentPlacesMenu::Find(*popup, path);
            QVERIFY(entry);
            RecentPlacesMenu::Choose(*popup, *entry);
            QTRY_COMPARE(location(window), path);
            QTRY_VERIFY(active(window)->isEnabled());
        }
#endif
    }

    void openingReturnsToCapturedPanel_data() {
        QTest::addColumn<bool>("editPeer");
        QTest::newRow("return-to-list") << false;
        QTest::newRow("preserve-new-peer-input") << true;
    }

    void openingReturnsToCapturedPanel() {
        QFETCH(bool, editPeer);
        QTemporaryDir fixture;
        const QString a = fixture.filePath("A"), b = fixture.filePath("B"),
                      c = fixture.filePath("C");
        directory(a, 301);
        directory(b, 301);
        directory(c);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
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
        store().Visit({b, {}, {}});
        const QString draft = QStringLiteral("new peer draft");
        QWidget *editFocus = nullptr;
        if (editPeer) {
            const auto scanners = window.findChildren<FilesystemScanner *>();
            QCOMPARE(scanners.size(), 2);
            connect(scanners.front(), &FilesystemScanner::advanced, &window, [&] {
                if (editFocus)
                    return;
                PanelKeys::focusAddress(address);
                address->selectAll();
                QTest::keyClicks(address, draft);
                editFocus = window.focusWidget();
            });
        }
        auto *leftRecent = window.findChild<QToolButton *>("panelRecentPlacesButton0");
        QVERIFY(leftRecent);
        leftRecent->click();
        QTRY_VERIFY(RecentPlacesMenu::Visible(window));
        auto *popup = RecentPlacesMenu::Visible(window);
        auto *entry = RecentPlacesMenu::Find(*popup, b);
        QVERIFY(entry);
        RecentPlacesMenu::Choose(*popup, *entry);
        QTRY_COMPARE(location(window), b);
        auto *left = window.findChild<QWidget *>("filePanel0");
        QVERIFY(left);
        auto *list = left->findChild<FileTable *>("archiveTable");
        QVERIFY(list);
        QTRY_VERIFY(list->isEnabled() && list->rowCount() >= 301);
        if (editPeer) {
            QVERIFY(editFocus);
            QCOMPARE(window.focusWidget(), editFocus);
            QVERIFY(address->isVisible());
            QCOMPARE(address->text(), draft);
        } else {
            QTRY_COMPARE(window.focusWidget(), list);
            if (window.isActiveWindow())
                QVERIFY(list->hasFocus());
        }
        QCOMPARE(QDir::cleanPath(ToQString(other->sourcePanel->GetCurrentFolderPrefix())), c);
    }

    void closedNestedArchiveReopensThroughRecordedNativeTarget_data() {
        QTest::addColumn<bool>("favorite");
        QTest::addColumn<bool>("fromUnavailable");
        QTest::addColumn<QString>("parentCase");
        QTest::newRow("recent") << false << false << QString();
        QTest::newRow("favorite") << true << false << QString();
        QTest::newRow("recent-from-unavailable") << false << true << QString();
        QTest::newRow("favorite-from-unavailable") << true << true << QString();
        QTest::newRow("parent-missing-outer") << true << false << QString("outer");
        QTest::newRow("parent-missing-inner-archive") << true << false << QString("inner");
        QTest::newRow("parent-missing-inner-folder") << true << false << QString("folder");
    }
    void closedNestedArchiveReopensThroughRecordedNativeTarget() {
        QFETCH(bool, favorite);
        QFETCH(bool, fromUnavailable);
        QFETCH(QString, parentCase);
        QTemporaryDir fixture;
        const QString folder = QStringLiteral("dir 空/sub папка");
        const QString entry = folder + "/payload.txt";
        QVERIFY(QDir().mkpath(fixture.filePath(folder)));
        QVERIFY(ExternalEditorFixture::write(fixture.filePath(entry), "nested recent bytes"));
        QVERIFY(ExternalEditorFixture::write(fixture.filePath("dir 空/kept.txt"), "parent bytes"));
        const QString inner = fixture.filePath("inner δ.zip"),
                      outer = fixture.filePath("outer.zip");
        QVERIFY(ExternalEditorFixture::createArchive(fixture.path(), inner, "dir 空"));
        QVERIFY(ExternalEditorFixture::createArchive(fixture.path(), outer,
                                                     QFileInfo(inner).fileName()));
        const QString plain = fixture.filePath("Plain");
        directory(plain);
        const QString missing = fixture.filePath("Moved folder");
        const QString fixtureName = QFileInfo(fixture.path()).fileName();
        const QString missingLabel = QStringLiteral("Moved folder ") + fixtureName;
        ArchiveBackend backend;
        MainWindow window(&backend);
        window.show();
        if (fromUnavailable) {
            QVERIFY(QDir().mkpath(missing));
            window.openPath(missing);
            QTRY_COMPARE(location(window), missing);
            addFavorite(window, missingLabel);
        }
        window.openArchive(outer);
        QTRY_VERIFY(rowWithPath(window, QFileInfo(inner).fileName()) >= 0);
        const auto outerRoot = active(window)->sourcePanel->GetCurrentFolderPrefix();
        openRow(window, rowWithPath(window, QFileInfo(inner).fileName()));
        QTRY_VERIFY(rowWithPath(window, "dir 空") >= 0);
        UString archivedParent;
        if (parentCase != "inner") {
            openRow(window, rowWithPath(window, "dir 空"));
            QTRY_VERIFY(rowWithPath(window, folder) >= 0);
            archivedParent = active(window)->sourcePanel->GetCurrentFolderPrefix();
            openRow(window, rowWithPath(window, folder));
            QTRY_VERIFY(rowWithPath(window, entry) >= 0);
        }
        const QString requested = ToQString(active(window)->sourcePanel->GetCurrentFolderPrefix());
        const QString savedName = QStringLiteral("Nested archive ") + fixtureName;
        if (favorite)
            addFavorite(window, savedName);
        window.openPath(plain);
        if (!parentCase.isEmpty()) {
            QVERIFY(QFile::remove(outer));
            if (parentCase == "folder") {
                QVERIFY(QFile::remove(inner));
                QVERIFY(QDir(fixture.filePath(folder)).removeRecursively());
                QVERIFY(ExternalEditorFixture::createArchive(fixture.path(), inner, "dir 空"));
                QVERIFY(ExternalEditorFixture::createArchive(fixture.path(), outer,
                                                             QFileInfo(inner).fileName()));
            } else if (parentCase == "inner") {
                QVERIFY(ExternalEditorFixture::createArchive(fixture.path(), outer, "Plain"));
            }
            openFavorite(window, savedName);
            auto *parent = window.findChild<QPushButton *>("navigationParent0");
            QVERIFY(parent);
            QTRY_VERIFY(parent->isVisible());
            QVERIFY(!active(window)->isEnabled());
            parent->click();
            QTRY_VERIFY(active(window)->isEnabled() && !parent->isVisible());
            if (parentCase == "outer") {
                QCOMPARE(location(window), fixture.path());
                QVERIFY(active(window)->sourcePanel->IsFSFolder());
            } else if (parentCase == "inner") {
                QCOMPARE(active(window)->sourcePanel->GetCurrentFolderPrefix(), outerRoot);
                QCOMPARE(active(window)->sourcePanel->GetParentFolders().Size(), 1u);
                QVERIFY(rowWithPath(window, "Plain") >= 0);
            } else {
                QCOMPARE(active(window)->sourcePanel->GetCurrentFolderPrefix(), archivedParent);
                QCOMPARE(active(window)->sourcePanel->GetParentFolders().Size(), 2u);
                QVERIFY(rowWithPath(window, "dir 空/kept.txt") >= 0);
            }
            return;
        }
        const auto openRecent = [&](const QString &path) {
            action(window, "foldersHistoryAction")->trigger();
            QTRY_VERIFY(RecentPlacesMenu::Visible(window));
            auto *popup = RecentPlacesMenu::Visible(window);
            auto *recent = RecentPlacesMenu::Find(*popup, path);
            QVERIFY(recent);
            RecentPlacesMenu::Choose(*popup, *recent);
        };
        const auto refuseRecent = [&](const QString &path, const QString &retryPath) {
            auto *address = window.findChild<QLineEdit *>("archivePath");
            QVERIFY(address);
            window.statusBar()->clearMessage();
            openRecent(path);
            QTRY_VERIFY(!window.statusBar()->currentMessage().isEmpty());
            QVERIFY(!active(window)->isEnabled());
            QCOMPARE(active(window)->rowCount(), 0);
            QCOMPARE(QDir::cleanPath(QDir::fromNativeSeparators(address->text())),
                     QDir::cleanPath(QDir::fromNativeSeparators(retryPath)));
        };
        if (fromUnavailable) {
            QVERIFY(QDir().rmdir(missing));
            openFavorite(window, missingLabel);
            QTRY_VERIFY(!active(window)->isEnabled());
            QVERIFY(window.findChild<QPushButton *>("navigationRetry0")->isVisible());
        }
        if (fromUnavailable && !favorite) {
            const QString absent = fixture.filePath("Another missing folder");
            store().Visit({absent, {}, {}});
            refuseRecent(absent, missing);
            const QString parked = fixture.filePath("temporarily missing.zip");
            QVERIFY(QFile::rename(outer, parked));
            refuseRecent(requested, missing);
            QVERIFY(QFile::rename(parked, outer));
        }
        if (favorite && !fromUnavailable) {
            const QString parked = fixture.filePath("temporarily missing.zip");
            QVERIFY(QFile::rename(outer, parked));
            openFavorite(window, savedName);
            auto *retry = window.findChild<QPushButton *>("navigationRetry0");
            QVERIFY(retry);
            QTRY_VERIFY(retry->isVisible());
            const QString absent = fixture.filePath("Another missing folder");
            store().Visit({absent, {}, {}});
            refuseRecent(absent, requested);
            QVERIFY(QFile::rename(parked, outer));
            retry->click();
            QTRY_VERIFY(!retry->isVisible());
        } else if (favorite) {
            openFavorite(window, savedName);
        } else {
            openRecent(requested);
        }
        QTRY_COMPARE(ToQString(active(window)->sourcePanel->GetCurrentFolderPrefix()), requested);
        QTRY_VERIFY(active(window)->isEnabled());
        QVERIFY(!window.findChild<QPushButton *>("navigationRetry0")->isVisible());
        QCOMPARE(active(window)->sourcePanel->GetParentFolders().Size(), 2u);
        QCOMPARE(ToQString(active(window)->sourcePanel->GetCurrentFolderPrefix()), requested);
        const int row = rowWithPath(window, entry);
        QVERIFY(row >= 0);
        active(window)->selectRow(row);
        active(window)->setCurrentCell(row, 0, QItemSelectionModel::NoUpdate);
        const QString output = fixture.filePath("exported");
        window.extractTo(output, true);
        QCOMPARE(ExternalEditorFixture::read(output + "/payload.txt"), "nested recent bytes");
        if (fromUnavailable) {
            action(window, "backAction")->trigger();
            QTRY_COMPARE(location(window), plain);
            QTRY_VERIFY(active(window)->isEnabled());
        }
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

    void computerFavoriteReopensAfterReloadAndLanguageChange() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString name = QStringLiteral("Saved computer");
        const QString plain = fixture.filePath("Plain");
        directory(plain);
        ControlledBackend backend;
        {
            MainWindow window(&backend);
            window.show();
            action(window, "openRootAction")->trigger();
            QVERIFY(active(window)->sourcePanel->IsFSDrivesFolder());
            addFavorite(window, name);
        }
        // Reload the settings-scoped model as a new application would.
        const auto settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        for (auto *object : qApp->children())
            if (auto *favorites = dynamic_cast<QtFavoriteStore *>(object);
                favorites && favorites->property("settingsPath").toString() == settings) {
                delete favorites;
                break;
            }
        SaveRegLang(UString("ru"), settings);
        ReloadLang(settings);
        QtNotifyLanguageChanged();
        MainWindow window(&backend);
        window.show();
        window.openPath(plain);
        QTRY_COMPARE(location(window), plain);
        openFavorite(window, name);
        QTRY_VERIFY(active(window)->sourcePanel->IsFSDrivesFolder());
        QVERIFY(!active(window)->sourcePanel->IsFSFolder());
    }
};

QTEST_MAIN(RecentPlacesTests)
#include "recent_places_tests.moc"
