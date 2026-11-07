#include "dialogs/password/PasswordDialog.h"
#include "filemanager/support/window_state_support.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "platform/support/external_editor_fixture.h"
#include "support/widget_dialogs.h"
#include "support/window_activation.h"
#include "ui/support/layout_inspection.h"
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
        table(window)->setFocus();
        QTest::keySequence(table(window), forward->shortcut());
        QTRY_COMPARE(location(window), b);
        QTest::mouseClick(window.findChild<QToolButton *>("panelBackButton0"), Qt::LeftButton);
        QTRY_COMPARE(location(window), a);
        QTest::mouseClick(window.findChild<QToolButton *>("panelForwardButton0"), Qt::LeftButton);
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

    void forwardParentBranchAndLargeDirectoryViewport() {
        QTemporaryDir fixture;
        const QString a = fixture.filePath("A 空");
        const QString b = fixture.filePath("B");
        const QString c = fixture.filePath("C");
        createDirectory(a, 301);
        createDirectory(b);
        createDirectory(c);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.resize(980, 620);
        window.show();
        window.openPath(a);
        auto *view = table(window);
        QTRY_VERIFY(view->isEnabled() && view->rowCount() >= 301);
        const int row = 230;
        view->selectRow(row);
        view->setCurrentCell(row, 0, QItemSelectionModel::NoUpdate);
        view->scrollTo(view->model()->index(row, 0), QAbstractItemView::PositionAtCenter);
        const QString selected = view->model()->index(row, 0).data().toString();
        const int scroll = view->verticalScrollBar()->value();
        QVERIFY(scroll > 0);
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
        QVERIFY(!action(window, "forwardAction")->isEnabled());
        action(window, "backAction")->trigger();
        QTRY_COMPARE(location(window), b);
        action(window, "backAction")->trigger();
        QTRY_VERIFY(view->isEnabled() && location(window) == a && view->rowCount() >= 301);
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
        reject = true;
        prompts = 0;
        action(window, "backAction")->trigger();
        QTRY_VERIFY(table(window)->isEnabled());
        QCOMPARE(prompts, 1);
        QCOMPARE(location(window), folder);
        QVERIFY(table(window)->sourcePanel->ShouldProcessNotifications());
        QVERIFY(action(window, "backAction")->isEnabled());
        QVERIFY(!action(window, "forwardAction")->isEnabled());
        reject = false;
        action(window, "backAction")->trigger();
        QTRY_VERIFY(table(window)->isEnabled() && table(window)->sourcePanel->IsArcFolder());
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
        action(first, "backAction")->trigger();
        QTRY_COMPARE(location(first), a);
        QCOMPARE(location(second), c);
        action(first, "forwardAction")->trigger();
        QTRY_COMPARE(location(first), b);
        QCOMPARE(location(second), c);
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
        action(window, "parentFolderAction")->trigger();
        QTRY_VERIFY(rowWithPath(window, "folders/second") >= 0);
        openRow(window, rowWithPath(window, "folders/second"));
        QTRY_VERIFY(rowWithPath(window, "folders/second/item-0000.txt") >= 0);
        const QString second = location(window);
        window.openPath(fixture.path());
        action(window, "backAction")->trigger();
        QTRY_COMPARE(location(window), second); // Fresh reader before same-chain travel.
        action(window, "backAction")->trigger();
        QTRY_VERIFY(rowWithPath(window, "folders/first") >= 0);
        action(window, "backAction")->trigger();
        QTRY_COMPARE(location(window), first);
        action(window, "forwardAction")->trigger();
        QTRY_VERIFY(rowWithPath(window, "folders/second") >= 0);
        action(window, "forwardAction")->trigger();
        QTRY_COMPARE(location(window), second);
    }
};

QTEST_MAIN(WorkspaceHistoryTests)
#include "workspace_history_tests.moc"
