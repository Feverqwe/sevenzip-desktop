#include "dialogs/password/PasswordDialog.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/window/qtfavoritestore.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "platform/support/external_editor_fixture.h"
#include "support/panel_keys.h"
#include "support/widget_dialogs.h"
#include "support/window_activation.h"
#include "ui/support/layout_inspection.h"
#include <QMenu>
#include <QScrollBar>
#include <QStatusBar>
#include <QToolButton>

class WorkspaceHistoryTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    WidgetDialogFixture dialogs_;
    static QString location(MainWindow &window) {
        return QDir::cleanPath(ToQString(table(window)->sourcePanel->GetCurrentFolderPrefix()));
    }
    static void createDirectory(const QString &path, int count = 1) {
        QVERIFY(QDir().mkpath(path));
        for (int index = 0; index < count; ++index) {
            QFile file(QDir(path).filePath(QString("item-%1.txt").arg(index, 4, 10, QChar('0'))));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write("history payload"), 15);
        }
    }
  private slots:
    void initTestCase() { UiInspection::configureReviewAppearance(); }
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }

    void missingFolderKeepsHistoryPosition_data() {
        QTest::addColumn<bool>("forward");
        QTest::newRow("back") << false;
        QTest::newRow("forward") << true;
    }
    void missingFolderKeepsHistoryPosition() {
        QFETCH(bool, forward);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString first = fixture.filePath("first");
        const QString second = fixture.filePath("second");
        createDirectory(first);
        createDirectory(second);
        ArchiveBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openPath(first);
        QTRY_COMPARE(location(window), first);
        window.openPath(second);
        QTRY_COMPARE(location(window), second);
        if (forward) {
            action(window, "backAction")->trigger();
            QTRY_COMPARE(location(window), first);
        }
        const QString current = forward ? first : second;
        const QString missing = forward ? second : first;
        auto *travel = action(window, forward ? "forwardAction" : "backAction");
        QTRY_VERIFY(travel->isEnabled());
        const QString parked = fixture.filePath("renamed");
        QVERIFY(QDir().rename(missing, parked));
        travel->trigger();
        QTRY_VERIFY(window.statusBar()->currentMessage().contains(missing));
        QCOMPARE(location(window), current);
        QTRY_VERIFY(travel->isEnabled());
        const QString archive = fixture.filePath("replacement.7z");
        QVERIFY(ExternalEditorFixture::createArchive(
            fixture.path(), archive, QFileInfo(parked).fileName(), "7z", "history password"));
        QVERIFY(QFile::rename(archive, missing));
        int prompts = 0;
        QTimer answer;
        dialogs_.connectResponder(answer, &window, [&] {
            if (auto *dialog = dynamic_cast<CPasswordDialog *>(dialogs_.activeDialog())) {
                ++prompts;
                dialog->reject();
            }
        });
        answer.start(1);
        travel->trigger();
        answer.stop();
        QCOMPARE(prompts, 0);
        QCOMPARE(location(window), current);
        QVERIFY(QFile::remove(missing));
        QVERIFY(QDir().rename(parked, missing));
        travel->trigger();
        QTRY_COMPARE(location(window), missing);
    }

    void keyboardAndNavigationGeometry_data() {
        QTest::addColumn<QString>("language");
        QTest::newRow("en") << QString("-");
        QTest::newRow("ru") << QString("ru");
    }
    void keyboardAndNavigationGeometry() {
        QFETCH(QString, language);
        SaveRegLang(ToUString(language), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir fixture;
        const QString a = fixture.filePath("Previous 空");
        const QString b = fixture.filePath("Current папка");
        createDirectory(a);
        createDirectory(b);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openPath(a);
        window.openPath(b);
        QVERIFY(WindowActivation::waitForActive(window));
        table(window)->setFocus();
        QTRY_VERIFY(table(window)->hasFocus());
        auto *back = action(window, "backAction");
        auto *forward = action(window, "forwardAction");
        QTest::keySequence(table(window), back->shortcut());
        QTRY_COMPARE(location(window), a);
        QTRY_VERIFY(table(window)->hasFocus());
        QVERIFY(forward->isEnabled());
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(size);
            QApplication::processEvents();
            auto *previous = window.findChild<QToolButton *>("panelBackButton0");
            auto *next = window.findChild<QToolButton *>("panelForwardButton0");
            QVERIFY(previous && next);
            QVERIFY(UiInspection::fullyVisible(*previous, window));
            QVERIFY(UiInspection::fullyVisible(*next, window));
            QVERIFY(previous->geometry().right() < next->geometry().left());
            QVERIFY(UiInspection::fullyVisible(*window.findChild<QWidget *>("panelNavigation0"),
                                               window));
            QVERIFY(UiInspection::writeReport(
                window, QString("history-%1-%2").arg(language).arg(size.width())));
        }
        QTest::keySequence(window.focusWidget(), forward->shortcut());
        QTRY_COMPARE(location(window), b);
        QTRY_VERIFY(table(window)->hasFocus());
        auto *parent = window.findChild<QPushButton *>("panelParentButton0");
        auto *previous = window.findChild<QToolButton *>("panelBackButton0");
        auto *next = window.findChild<QToolButton *>("panelForwardButton0");
        QVERIFY(parent && previous && next);
        QVERIFY(!next->isEnabled());
        parent->setFocus();
        QTest::keyClick(parent, Qt::Key_Backtab);
        QCOMPARE(window.focusWidget(), previous);
        QTest::keyClick(previous, Qt::Key_Space);
        QTRY_COMPARE(location(window), a);
        parent->setFocus();
        QTest::keyClick(parent, Qt::Key_Backtab);
        QCOMPARE(window.focusWidget(), next);
        QTest::keyClick(next, Qt::Key_Space);
        QTRY_COMPARE(location(window), b);
    }

    void folderActivationRetainsListFocus() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString parent =
            QDir(QFileInfo(fixture.path()).canonicalFilePath()).filePath("parent");
        const QString child = QDir(parent).filePath("child");
        const QString nested = QDir(child).filePath("nested");
        createDirectory(child, 300);
        createDirectory(nested);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openPath(parent);
        QVERIFY(WindowActivation::waitForActive(window));
        auto *view = table(window);
        view->setFocus();
        QTRY_VERIFY(view->hasFocus());
        QTRY_VERIFY(filesystemRow(window, "child") >= 0);
        view->setCurrentCell(filesystemRow(window, "child"), 0);
#ifdef Q_OS_MACOS
        QTest::keyClick(view, Qt::Key_Return, Qt::ControlModifier);
        QTRY_VERIFY(view->findChild<QLineEdit *>());
        QCOMPARE(location(window), parent);
        QTest::keyClick(view->findChild<QLineEdit *>(), Qt::Key_Escape);
        QTRY_VERIFY(!view->findChild<QLineEdit *>());
#endif
        QTest::keyClick(view, Qt::Key_Return);
        QTRY_COMPARE(location(window), child);
        QTRY_VERIFY(view->hasFocus());
        QTRY_VERIFY(filesystemRow(window, "nested") >= 0);
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Home);
        const int firstRow = view->currentRow();
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Down);
        QVERIFY(view->currentRow() > firstRow);
        view->setCurrentCell(filesystemRow(window, "nested"), 0);
        const int sizeColumn = view->columnForProperty(kpidSize);
        QVERIFY(sizeColumn >= 0);
        const QPoint cell =
            view->visualRect(view->model()->index(view->currentRow(), sizeColumn)).center();
        QVERIFY(view->viewport()->rect().contains(cell));
        QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, cell);
        QTest::mouseDClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, cell);
        QTRY_COMPARE(location(window), nested);
        QTRY_VERIFY(view->hasFocus());
    }

    void keyboardHistoryWorksWhileThePeerRefreshes() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString previous = fixture.filePath("previous");
        const QString current = fixture.filePath("current");
        const QString peerPath = fixture.filePath("peer");
        createDirectory(previous);
        createDirectory(current);
        createDirectory(peerPath, 0);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        auto *view = table(window);
        window.openPath(previous);
        window.openPath(current);
        QTRY_VERIFY(view->isEnabled() && view->rowCount() == 1);
        view->setCurrentCell(0, 0);
        view->selectRow(0);
        action(window, "twoPanelsAction")->trigger();
        auto *peerPanel = window.findChild<QWidget *>("filePanel1");
        QVERIFY(peerPanel && peerPanel->isVisible());
        auto *peer = peerPanel->findChild<FileTable *>("archiveTable");
        auto *address = peerPanel->findChild<QLineEdit *>("archivePath");
        QVERIFY(peer && address);
        PanelKeys::focusAddress(address);
        QTRY_VERIFY(address->hasFocus());
        window.openPath(peerPath);
        QTRY_VERIFY(peer->isEnabled() && peer->rowCount() == 0);
        peer->sourcePanel->Set_AutoRefresh_Mode(false);
        const auto panelPath = [](const FileTable &files) {
            return QDir::cleanPath(ToQString(files.sourcePanel->GetCurrentFolderPrefix()));
        };
        for (const bool forward : {false, true}) {
            const int count = forward ? 302 : 301;
            createDirectory(peerPath, count);
            peer->setFocus();
            QVERIFY(peer->hasFocus());
            QTest::keySequence(peer, action(window, "refreshAction")->shortcut());
            QVERIFY(peer->hasFocus());
            QVERIFY(!action(window, "refreshAction")->isEnabled());
            QTest::keyClick(peer, Qt::Key_Tab);
            QVERIFY(view->hasFocus());
            auto *travel = action(window, forward ? "forwardAction" : "backAction");
            QVERIFY(travel->isEnabled());
            const QString target = forward ? current : previous;
            const QString parked = fixture.filePath("temporarily unavailable");
            QVERIFY(QDir().rename(target, parked));
            QTest::keySequence(view, travel->shortcut());
            QVERIFY(window.statusBar()->currentMessage().contains(target));
            QCOMPARE(panelPath(*view), forward ? previous : current);
            QVERIFY(view->hasFocus());
            QVERIFY(travel->isEnabled());
            QVERIFY(QDir().rename(parked, target));
            QTest::keySequence(view, travel->shortcut());
            QTRY_COMPARE(panelPath(*view), target);
            QVERIFY(window.statusBar()->currentMessage().isEmpty());
            QTRY_VERIFY(view->hasFocus());
            QTRY_VERIFY(peer->isEnabled() && peer->rowCount() == count);
            QCOMPARE(panelPath(*peer), peerPath);
        }
        QCOMPARE(view->currentIndex().data().toString(), QString("item-0000.txt"));
        QVERIFY(view->selectionModel()->isRowSelected(view->currentRow(), {}));
    }

    void forwardParentBranchAndDirectoryViewport_data() {
        QTest::addColumn<int>("count");
        QTest::newRow("immediate") << 100;
        QTest::newRow("deferred") << 301;
    }
    void forwardParentBranchAndDirectoryViewport() {
        QFETCH(int, count);
        QTemporaryDir fixture;
        const QString a = fixture.filePath("A 空");
        const QString b = fixture.filePath("B");
        const QString c = fixture.filePath("C");
        createDirectory(a, count);
        createDirectory(b);
        createDirectory(c);
        if (count > 256)
            createDirectory(fixture.path(), count);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.resize(980, 620);
        window.show();
        window.openPath(a);
        auto *view = table(window);
        QTRY_VERIFY(view->isEnabled() && view->rowCount() >= count);
        const int row = count * 3 / 4;
        view->selectRow(row);
        view->setCurrentCell(row, 0, QItemSelectionModel::NoUpdate);
        view->scrollTo(view->model()->index(row, 0), QAbstractItemView::PositionAtCenter);
        const QString selected = view->model()->index(row, 0).data().toString();
        const int scroll = view->verticalScrollBar()->value();
        QVERIFY(scroll > 0);
        if (count > 256) {
            action(window, "refreshAction")->trigger();
            QVERIFY(!action(window, "refreshAction")->isEnabled());
        }
        window.openPath(b);
        window.openPath(c);
        action(window, "backAction")->trigger();
        QTRY_COMPARE(location(window), b);
        QVERIFY(action(window, "forwardAction")->isEnabled());
        action(window, "forwardAction")->trigger();
        QTRY_COMPARE(location(window), c);
        action(window, "backAction")->trigger();
        QTRY_COMPARE(location(window), b);
        action(window, "parentFolderAction")->trigger();
        QTRY_COMPARE(location(window), fixture.path());
        QTRY_VERIFY(view->isEnabled());
        QCOMPARE(view->currentIndex().siblingAtColumn(0).data().toString(), QString("B"));
        QVERIFY(!action(window, "forwardAction")->isEnabled());
        action(window, "backAction")->trigger();
        QTRY_COMPARE(location(window), b);
        action(window, "backAction")->trigger();
        if (count == 100) {
            QCOMPARE(location(window), a);
            action(window, "forwardAction")->trigger();
            QCOMPARE(location(window), b);
            action(window, "backAction")->trigger();
        }
        QTRY_VERIFY(view->isEnabled() && location(window) == a && view->rowCount() >= count);
        QTRY_COMPARE(view->model()->index(view->currentRow(), 0).data().toString(), selected);
        QVERIFY(view->selectionModel()->isRowSelected(view->currentRow(), {}));
        QTRY_COMPARE(view->verticalScrollBar()->value(), scroll);
    }

    void historyRetriesUnavailableDirectory_data() {
        QTest::addColumn<bool>("forward");
        QTest::addColumn<bool>("background");
        QTest::newRow("back") << false << false;
        QTest::newRow("forward") << true << false;
        QTest::newRow("back-background") << false << true;
        QTest::newRow("forward-background") << true << true;
    }
    void historyRetriesUnavailableDirectory() {
        QFETCH(bool, forward);
        QFETCH(bool, background);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString first = fixture.filePath(QStringLiteral("first 空"));
        const QString second = fixture.filePath(QStringLiteral("second папка"));
        const QString target = forward ? second : first;
        const QString origin = forward ? first : second;
        createDirectory(origin, background ? 257 : 1);
        createDirectory(target);

        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        auto *address = window.findChild<QLineEdit *>("archivePath");
        auto *view = table(window);
        auto *back = window.findChild<QAction *>("backAction");
        auto *travel = forward ? window.findChild<QAction *>("forwardAction") : back;
        QVERIFY(address && view && back && travel);
        window.openPath(first);
        QTRY_COMPARE(address->text(), QFileInfo(first).canonicalFilePath());
        QTRY_VERIFY(view->isEnabled());
        window.openPath(second);
        QTRY_COMPARE(address->text(), QFileInfo(second).canonicalFilePath());
        QTRY_VERIFY(view->isEnabled());
        if (forward)
            back->trigger();
        else
            window.openPath(origin); // Reopening the current location is not another visit.
        const QString displayedOrigin = QFileInfo(origin).canonicalFilePath();
        QTRY_COMPARE(address->text(), displayedOrigin);
        QTRY_VERIFY(travel->isEnabled());
        const int row = filesystemRow(window, "item-0000.txt");
        QVERIFY(row >= 0);
        view->setCurrentCell(row, 0);
        view->selectRow(row);

        QVERIFY(QDir(target).removeRecursively());
        travel->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(travel->isEnabled(), 3000);
        QVERIFY(window.statusBar()->currentMessage().contains(QFileInfo(target).fileName()));
        QCOMPARE(address->text(), displayedOrigin);
        QVERIFY(view->selectionModel()->isRowSelected(view->currentRow(), {}));
        QCOMPARE(view->currentRow(), filesystemRow(window, "item-0000.txt"));
        if (!forward)
            QVERIFY(!action(window, "forwardAction")->isEnabled());

        createDirectory(target);
        travel->trigger();
        QTRY_COMPARE(address->text(), QFileInfo(target).canonicalFilePath());
        QVERIFY(window.statusBar()->currentMessage().isEmpty());
        QVERIFY(filesystemRow(window, "item-0000.txt") >= 0);
    }

    void supersededLargeDirectoryDoesNotBecomeAVisit() {
        QTemporaryDir fixture;
        const QString a = fixture.filePath("A");
        const QString b = fixture.filePath("B");
        const QString c = fixture.filePath("C");
        createDirectory(a);
        createDirectory(b, 301);
        createDirectory(c);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openPath(a);
        window.openPath(b);
        window.openPath(c);
        QTRY_VERIFY(table(window)->isEnabled() && location(window) == c);
        QVERIFY(table(window)->sourcePanel->ShouldProcessNotifications());
        action(window, "backAction")->trigger();
        QTRY_VERIFY(table(window)->isEnabled() && location(window) == a);
        QVERIFY(table(window)->sourcePanel->ShouldProcessNotifications());
        action(window, "forwardAction")->trigger();
        QTRY_COMPARE(location(window), c);
    }

    void addressNavigationSupersedesPendingBack() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString large = fixture.filePath("large");
        const QString current = fixture.filePath("current");
        const QString replacement = fixture.filePath("replacement");
        createDirectory(large, 301);
        createDirectory(current);
        createDirectory(replacement, 2);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        auto *view = table(window);
        window.openPath(large);
        QTRY_VERIFY(view->isEnabled() && view->rowCount() == 301);
        window.openPath(current);
        QTRY_COMPARE(location(window), current);
        auto *back = action(window, "backAction");
        auto *forward = action(window, "forwardAction");
        QVERIFY(back->isEnabled());
        back->trigger();
        QVERIFY(!view->isEnabled());
        auto *address = window.findChild<QLineEdit *>("archivePath");
        PanelKeys::focusAddress(address);
        address->setText(replacement);
        QTest::keyClick(address, Qt::Key_Return);
        QTRY_VERIFY(view->isEnabled() && location(window) == replacement);
        back->trigger();
        QTRY_VERIFY(view->isEnabled() && location(window) == current);
        forward->trigger();
        QTRY_VERIFY(view->isEnabled() && location(window) == replacement);
        QCOMPARE(view->rowCount(), 2);
    }

    void backRevealsFolderCreatedBeforeLeavingDuringRefresh() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString large = fixture.filePath("large");
        const QString other = fixture.filePath("other");
        createDirectory(large, 301);
        createDirectory(other);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        window.openPath(large);
        auto *view = table(window);
        QTRY_VERIFY(view->isEnabled() && view->rowCount() == 301);
        view->setFocus();
        view->selectRow(280);
        view->scrollTo(view->model()->index(280, 0), QAbstractItemView::PositionAtCenter);
        QVERIFY(view->verticalScrollBar()->value() > 0);

        QTest::keyClick(view, Qt::Key_F7);
        auto *editor = window.findChild<QLineEdit *>("inlineFolderName");
        QVERIFY(editor && editor->isVisible());
        const QString created = QStringLiteral("новая папка 空");
        editor->setText(created);
        QTest::keyClick(editor, Qt::Key_Return);
        QVERIFY(QFileInfo(QDir(large).filePath(created)).isDir());
        QVERIFY(!action(window, "refreshAction")->isEnabled());
        window.openPath(other);
        QTRY_VERIFY(view->isEnabled() && location(window) == other);
        action(window, "backAction")->trigger();
        QTRY_VERIFY(view->isEnabled() && location(window) == large && view->rowCount() == 302);
        QCOMPARE(view->currentIndex().data().toString(), created);
        QVERIFY(view->selectionModel()->isRowSelected(view->currentRow(), {}));
        QVERIFY(view->viewport()->rect().intersects(view->visualRect(view->currentIndex())));
    }

    void paneBackButtonIgnoresTheClosingViewMenu() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString left = fixture.filePath("left");
        const QString first = fixture.filePath("right first");
        const QString second = fixture.filePath("right second");
        for (const auto &path : {left, first, second})
            createDirectory(path);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        action(window, "twoPanelsAction")->trigger();
        const auto navigate = [&](unsigned pane, const QString &path) {
            auto *panel = window.findChild<QWidget *>(QString("filePanel%1").arg(pane));
            auto *address = panel->findChild<QLineEdit *>("archivePath");
            PanelKeys::focusAddress(address);
            address->setText(path);
            QTest::keyClick(address, Qt::Key_Return);
            QTRY_COMPARE(QDir::cleanPath(address->text()), path);
        };
        navigate(1, first);
        navigate(1, second);
        navigate(0, left);
        auto *menu = window.findChild<QMenu *>("viewMenu");
        auto *back = window.findChild<QToolButton *>("panelBackButton1");
        auto *forward = window.findChild<QToolButton *>("panelForwardButton1");
        QVERIFY(menu && back && forward && back->isEnabled());
        menu->popup(window.mapToGlobal(QPoint(20, 20)));
        QVERIFY(menu->isVisible());
        menu->hide();
        back->click();
        auto *right = window.findChild<QWidget *>("filePanel1")->findChild<FileTable *>();
        QTRY_COMPARE(QDir::cleanPath(ToQString(right->sourcePanel->GetCurrentFolderPrefix())),
                     first);
        QVERIFY(forward->isEnabled());
        forward->click();
        QTRY_COMPARE(QDir::cleanPath(ToQString(right->sourcePanel->GetCurrentFolderPrefix())),
                     second);
        auto *leftView = window.findChild<QWidget *>("filePanel0")->findChild<FileTable *>();
        QCOMPARE(QDir::cleanPath(ToQString(leftView->sourcePanel->GetCurrentFolderPrefix())), left);
        leftView->setFocus();
        QTRY_VERIFY(leftView->hasFocus());
        menu->popup(window.mapToGlobal(QPoint(20, 20)));
        QVERIFY(menu->isVisible());
        menu->hide();
        right->setFocus();
        QVERIFY(right->hasFocus());
        const auto shortcut = action(window, "upAction")->shortcut()[0];
        QKeyEvent up(QEvent::KeyPress, shortcut.key(), shortcut.keyboardModifiers());
        QCoreApplication::sendEvent(right, &up);
        QTRY_COMPARE(QDir::cleanPath(ToQString(right->sourcePanel->GetCurrentFolderPrefix())),
                     fixture.path());
        QCOMPARE(QDir::cleanPath(ToQString(leftView->sourcePanel->GetCurrentFolderPrefix())), left);
    }

    void canceledEncryptedBackKeepsItsCursorAndDoesNotPromptTwice() {
        QTemporaryDir fixture;
        const QString folder = fixture.filePath("source");
        createDirectory(folder);
        const QString archive = fixture.filePath("encrypted.7z");
        const QString password = QStringLiteral("history 密碼");
        QVERIFY(ExternalEditorFixture::createArchive(fixture.path(), archive, "source", "7z",
                                                     password));
        ArchiveBackend backend;
        MainWindow window(&backend);
        window.show();
        int prompts = 0;
        bool reject = false;
        QTimer answer;
        dialogs_.connectResponder(answer, &window, [&] {
            if (auto *dialog = dynamic_cast<CPasswordDialog *>(dialogs_.activeDialog())) {
                ++prompts;
                if (reject)
                    dialog->reject();
                else {
                    dialog->findChild<QLineEdit *>("passwordEdit")->setText(password);
                    dialog->accept();
                }
            }
        });
        answer.start(1);
        window.openArchive(archive);
        QTRY_VERIFY(table(window)->isEnabled() && table(window)->sourcePanel->IsArcFolder());
        window.openPath(folder);
        QTRY_VERIFY(table(window)->isEnabled() && location(window) == folder);
        QVERIFY(WindowActivation::waitForActive(window));
        auto *address = window.findChild<QLineEdit *>("archivePath");
        PanelKeys::focusAddress(address);
        QTRY_VERIFY(address->hasFocus());
        address->selectAll();
        const QString draft = QStringLiteral("unsubmitted path");
        QTest::keyClicks(address, draft);
        address->setSelection(11, -4);
        reject = true;
        prompts = 0;
        action(window, "backAction")->trigger();
        QTRY_VERIFY(table(window)->isEnabled());
        QCOMPARE(prompts, 1);
        QCOMPARE(location(window), folder);
        QCOMPARE(address->text(), draft);
        QCOMPARE(address->selectedText(), draft.mid(7, 4));
        QCOMPARE(address->cursorPosition(), 7);
        QVERIFY(table(window)->sourcePanel->ShouldProcessNotifications());
        QVERIFY(action(window, "backAction")->isEnabled());
        QVERIFY(!action(window, "forwardAction")->isEnabled());
        reject = false;
        action(window, "backAction")->trigger();
        QTRY_VERIFY(table(window)->isEnabled() && table(window)->sourcePanel->IsArcFolder());
        QVERIFY(address->text() != draft);
        action(window, "forwardAction")->trigger();
        QTRY_COMPARE(location(window), folder);
    }

    void historyRemainsIndependentAcrossWindows() {
        QTemporaryDir fixture;
        const QString a = fixture.filePath("A");
        const QString b = fixture.filePath("B");
        const QString c = fixture.filePath("C");
        createDirectory(a);
        createDirectory(b);
        createDirectory(c);
        ControlledBackend firstBackend, secondBackend;
        MainWindow first(&firstBackend), second(&secondBackend);
        first.show();
        second.show();
        first.openPath(a);
        first.openPath(b);
        second.openPath(c);
        QTRY_COMPARE(location(first), b);
        QTRY_COMPARE(location(second), c);
        QVERIFY(WindowActivation::waitForActive(second));
        auto *peer = table(second);
        const int row = filesystemRow(second, "item-0000.txt");
        QVERIFY(row >= 0);
        peer->setCurrentCell(row, 0);
        peer->selectRow(row);
        peer->setFocus();
        QVERIFY(WindowActivation::waitForActive(first));
        auto *view = table(first);
        view->setFocus();
        QTRY_VERIFY(view->hasFocus());
        QTest::keySequence(first.focusWidget(), action(first, "backAction")->shortcut());
        QTRY_COMPARE(location(first), a);
        QCOMPARE(location(second), c);
        QTRY_VERIFY(view->hasFocus());
        QTest::keySequence(first.focusWidget(), action(first, "forwardAction")->shortcut());
        QTRY_COMPARE(location(first), b);
        QCOMPARE(location(second), c);
        QVERIFY(WindowActivation::waitForActive(second));
        QTRY_VERIFY(peer->hasFocus());
        QCOMPARE(peer->currentRow(), row);
        QVERIFY(peer->selectionModel()->isRowSelected(row, {}));
    }

    void archiveBackIsAVisitAndParentIsANewTransition() {
        QTemporaryDir fixture;
        const QString folders = fixture.filePath("folders");
        createDirectory(folders + "/first");
        createDirectory(folders + "/second");
        const QString archive = fixture.filePath("history.zip");
        QVERIFY(ExternalEditorFixture::createArchive(fixture.path(), archive, "folders"));
        ArchiveBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openArchive(archive);
        QTRY_VERIFY(table(window)->isEnabled());
        openRow(window, rowWithPath(window, "folders"));
        QTRY_VERIFY(rowWithPath(window, "folders/first") >= 0);
        openRow(window, rowWithPath(window, "folders/first"));
        QTRY_VERIFY(rowWithPath(window, "folders/first/item-0000.txt") >= 0);
        const QString first = location(window);
        const QString missing = fixture.filePath("missing folder");
        QtFavoriteStore *favorites = nullptr;
        for (auto *object : qApp->children())
            if (auto *store = dynamic_cast<QtFavoriteStore *>(object);
                store && store->property("settingsPath").toString() ==
                             qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"))
                favorites = store;
        QVERIFY(favorites);
        favorites->Add(missing, "Unavailable place");
        auto *menu = window.findChild<QMenu *>("savedPlacesMenu");
        QVERIFY(menu);
        menu->popup(window.mapToGlobal(QPoint(20, 20)));
        QTRY_VERIFY(menu->isVisible());
        QAction *target = nullptr;
        for (auto *entry : menu->actions())
            if (entry->toolTip() == missing)
                target = entry;
        QVERIFY(target);
        menu->setActiveAction(target);
        QTest::keyClick(menu, Qt::Key_Return);
        QTRY_VERIFY(!table(window)->isEnabled());
        QVERIFY(action(window, "backAction")->isEnabled());
        action(window, "backAction")->trigger();
        QTRY_VERIFY(table(window)->isEnabled());
        QCOMPARE(location(window), first);
        QVERIFY(rowWithPath(window, "folders/first/item-0000.txt") >= 0);
        action(window, "parentFolderAction")->trigger();
        QTRY_VERIFY(rowWithPath(window, "folders/second") >= 0);
        QCOMPARE(table(window)->currentRow(), rowWithPath(window, "folders/first"));
        openRow(window, rowWithPath(window, "folders/second"));
        QTRY_VERIFY(rowWithPath(window, "folders/second/item-0000.txt") >= 0);
        const QString second = location(window);
        window.openPath(fixture.path());
        QVERIFY(WindowActivation::waitForActive(window));
        auto *view = table(window);
        view->setFocus();
        QTRY_VERIFY(view->hasFocus());
        const auto back = action(window, "backAction")->shortcut();
        const auto forward = action(window, "forwardAction")->shortcut();
        QTest::keySequence(window.focusWidget(), back);
        QTRY_COMPARE(location(window), second); // Fresh reader before same-chain travel.
        QVERIFY(WindowActivation::waitForActive(window));
        QTRY_VERIFY(view->hasFocus());
        QTest::keySequence(window.focusWidget(), back);
        QTRY_VERIFY(rowWithPath(window, "folders/first") >= 0);
        QTRY_VERIFY(view->hasFocus());
        QTest::keySequence(window.focusWidget(), back);
        QTRY_COMPARE(location(window), first);
        QTRY_VERIFY(view->hasFocus());
        QTest::keySequence(window.focusWidget(), forward);
        QTRY_VERIFY(rowWithPath(window, "folders/second") >= 0);
        QTRY_VERIFY(view->hasFocus());
        QTest::keySequence(window.focusWidget(), forward);
        QTRY_COMPARE(location(window), second);
        QTRY_VERIFY(view->hasFocus());
    }
};

QTEST_MAIN(WorkspaceHistoryTests)
#include "workspace_history_tests.moc"
