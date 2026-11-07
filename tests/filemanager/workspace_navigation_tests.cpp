#include "filemanager/support/window_state_support.h"
#include "filemanager/window/qtfavoritestore.h"
#include "filemanager/window/qtnavigationbar.h"
#include "filemanager/window/qtwindowworkspace.h"
#include "filemanager/window/qtworkspaceapplication.h"
#include "mount/qtarchivemountmanager.h"
#include "support/panel_keys.h"
#include "support/progress_dialog.h"
#include "support/widget_dialogs.h"
#include "support/window_activation.h"
#include "ui/support/layout_inspection.h"
#include <QClipboard>
#include <QCloseEvent>
#include <QListWidget>
#include <QTabBar>
#include <QToolButton>

class CloseProtectedWindow final : public MainWindow {
  public:
    using MainWindow::MainWindow;

  protected:
    void closeEvent(QCloseEvent *event) override { event->ignore(); }
};

class WorkspaceNavigationTests final : public QObject {
    Q_OBJECT
    WidgetDialogFixture dialogs_;

    static FileTable *visibleTable(MainWindow &window) {
        for (auto *table : window.findChildren<FileTable *>())
            if (table->isVisible())
                return table;
        return nullptr;
    }
    static void writeFile(const QString &path, const QByteArray &bytes) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), bytes.size());
    }
    static QtWindowWorkspace *workspace(MainWindow &window) {
        for (auto *object : window.children())
            if (auto *service = dynamic_cast<QtWindowWorkspace *>(object))
                return service;
        return nullptr;
    }

  private slots:
    void initTestCase() { UiInspection::configureReviewAppearance(); }
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }
    void locationPublicationWaitsForSourceGuard() {
        QTemporaryDir fixture;
        ControlledBackend backend;
        const QString archive = fixture.filePath("guarded archive.zip");
        QVERIFY(backend.prepareArchiveFixture(archive));
        MainWindow window(&backend);
        window.show();
        window.openPath(fixture.path());
        auto *table = visibleTable(window);
        QTRY_VERIFY(table && table->isEnabled());
        auto *bar = window.findChild<QWidget *>("panelNavigation0");
        QVERIFY(bar);
        const QString previous = bar->toolTip();
        QVERIFY(!previous.isEmpty());
        bool guarded = false;
        bool keptPrevious = false;
        backend.queueListAction([&] {
            guarded = !table->sourcePanel->ShouldProcessNotifications();
            workspace(window)->Refresh();
            keptPrevious = bar->toolTip() == previous;
            backend.completeList({ArchiveEntry{QStringLiteral("new.txt"), 1, 1}});
        });
        window.openArchive(archive);
        QVERIFY(guarded);
        QVERIFY(keptPrevious);
        QTRY_VERIFY(table->isEnabled());
        QTRY_COMPARE(QDir::cleanPath(bar->toolTip()), archive);
    }
    void locationBarUsesNativeParentTargets() {
        QTemporaryDir fixture;
        const QString parent = fixture.filePath(QStringLiteral("folder with archive.zip"));
        const QString child = parent + QStringLiteral("/child 空");
        QVERIFY(QDir().mkpath(child));
        writeFile(child + "/keep.txt", "unchanged");
        ControlledBackend backend;
        MainWindow window(&backend);
        window.resize(980, 620);
        window.show();
        window.openPath(child);
        auto *table = visibleTable(window);
        QTRY_VERIFY(table && table->isEnabled());
        auto *bar =
            dynamic_cast<QtNavigationBar *>(window.findChild<QWidget *>("panelNavigation0"));
        QVERIFY(bar);
        QToolButton *parentButton = nullptr;
        for (auto *button : bar->findChildren<QToolButton *>())
            if (QDir::cleanPath(button->toolTip()) == parent && button->isVisible())
                parentButton = button;
        QVERIFY(parentButton);
        QVERIFY(parentButton->icon().isNull());
        QTest::mouseClick(parentButton, Qt::LeftButton);
        QTRY_COMPARE(QDir::cleanPath(ToQString(table->sourcePanel->GetCurrentFolderPrefix())),
                     parent);
        QVERIFY(table->sourcePanel->IsFSFolder());
        QCOMPARE(backend.listCalls, 0);
        for (int width : {980, 640}) {
            window.resize(width, 620);
            QApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(*bar, window));
            QVERIFY(UiInspection::writeReport(window, QString("native-location-%1").arg(width)));
        }
    }
    void closingLeftPanelKeepsRightContextAndReopensHome() {
        QTemporaryDir fixture;
        const QString left = fixture.filePath("Left folder");
        const QString right = fixture.filePath("Right folder");
        QVERIFY(QDir().mkpath(left));
        QVERIFY(QDir().mkpath(right));
        writeFile(right + "/keep selected.txt", "right context");
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openPath(left);
        auto *toggle = window.findChild<QAction *>("twoPanelsAction");
        QTRY_VERIFY(toggle->isEnabled());
        toggle->trigger();
        auto *pane = window.findChild<QWidget *>("filePanel1");
        QVERIFY(pane);
        auto *address = pane->findChild<QLineEdit *>("archivePath");
        PanelKeys::focusAddress(address);
        address->setText(right);
        QTest::keyClick(address, Qt::Key_Return);
        auto *view = pane->findChild<FileTable *>("archiveTable");
        QTRY_VERIFY(view->isEnabled());
        int selected = -1;
        for (int row = 0; row < view->rowCount(); ++row)
            if (view->model()->index(row, 0).data().toString() == "keep selected.txt")
                selected = row;
        QVERIFY(selected >= 0);
        view->selectRow(selected);
        view->setCurrentCell(selected, 0, QItemSelectionModel::NoUpdate);
        QTRY_VERIFY(toggle->isEnabled());
        workspace(window)->CloseTab(0, 0);
        QTRY_VERIFY(!toggle->isChecked());
        QVERIFY(view->isVisible());
        QCOMPARE(QDir::cleanPath(ToQString(view->sourcePanel->GetCurrentFolderPrefix())), right);
        QCOMPARE(view->model()->index(view->currentRow(), 0).data().toString(),
                 "keep selected.txt");
        QVERIFY(view->selectionModel()->isRowSelected(view->currentRow(), {}));
        toggle->trigger();
        QTRY_VERIFY(toggle->isChecked());
        auto *reopened =
            window.findChild<QWidget *>("filePanel0")->findChild<FileTable *>("archiveTable");
        QTRY_VERIFY(reopened->isEnabled());
        QCOMPARE(QDir::cleanPath(ToQString(reopened->sourcePanel->GetCurrentFolderPrefix())),
                 QDir::homePath());
        QCOMPARE(QDir::cleanPath(ToQString(view->sourcePanel->GetCurrentFolderPrefix())), right);
    }

    void computerOpensInFocusedPanelAndKeepsPeerLocation() {
        QTemporaryDir fixture;
        const QString left = fixture.filePath("Kept left folder");
        QVERIFY(QDir().mkpath(left));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.resize(980, 620);
        window.show();
        window.openPath(left);
        auto *toggle = window.findChild<QAction *>("twoPanelsAction");
        QTRY_VERIFY(toggle->isEnabled());
        toggle->trigger();
        auto *splitter = window.findChild<QSplitter *>("filePanelSplitter");
        auto *leftTable = splitter->widget(0)->findChild<FileTable *>("archiveTable");
        auto *rightTable = splitter->widget(1)->findChild<FileTable *>("archiveTable");
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        rightTable->setFocus();
        QTRY_VERIFY(rightTable->hasFocus());
        const auto path = [](const FileTable &table) {
            return QDir::cleanPath(
                QDir::fromNativeSeparators(ToQString(table.sourcePanel->GetCurrentFolderPrefix())));
        };
        window.findChild<QAction *>("openRootAction")->trigger();
        QTRY_VERIFY(rightTable->sourcePanel->IsFSDrivesFolder());
        QCOMPARE(path(*leftTable), left);
    }

    void tabsRetainSourceSelectionAndRemainUsableAfterClose() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString first = fixture.filePath("First папка");
        const QString second = fixture.filePath("Second folder");
        QVERIFY(QDir().mkpath(first));
        QVERIFY(QDir().mkpath(second));
        writeFile(first + "/selected.txt", "first");
        writeFile(second + "/other.txt", "second");
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openPath(first);
        auto *original = visibleTable(window);
        QVERIFY(original);
        QTRY_VERIFY(original->isEnabled());
        int selected = -1;
        for (int row = 0; row < original->rowCount(); ++row)
            if (original->model()->index(row, 0).data().toString() == "selected.txt")
                selected = row;
        QVERIFY(selected >= 0);
        original->selectRow(selected);
        original->setCurrentCell(selected, 0, QItemSelectionModel::NoUpdate);
        auto *tabs = window.findChild<QTabBar *>("panelTabs0");
        QVERIFY(tabs);
        window.findChild<QAction *>("newTabAction")->trigger();
        QTRY_COMPARE(tabs->count(), 2);
        QTRY_VERIFY(visibleTable(window) && visibleTable(window)->isEnabled());
        window.openPath(second);
        tabs->setCurrentIndex(0);
        QTRY_COMPARE(
            QDir::cleanPath(ToQString(visibleTable(window)->sourcePanel->GetCurrentFolderPrefix())),
            first);
        auto *returned = visibleTable(window);
        QCOMPARE(QDir::cleanPath(ToQString(returned->sourcePanel->GetCurrentFolderPrefix())),
                 first);
        QCOMPARE(returned->model()->index(returned->currentRow(), 0).data().toString(),
                 "selected.txt");
        QVERIFY(returned->selectionModel()->isRowSelected(returned->currentRow(), {}));
        window.findChild<QAction *>("closeWindowAction")->trigger();
        QTRY_COMPARE(tabs->count(), 1);
        QVERIFY(visibleTable(window));
        QCOMPARE(
            QDir::cleanPath(ToQString(visibleTable(window)->sourcePanel->GetCurrentFolderPrefix())),
            second);
    }

    void fileClipboardCopiesRealBytesAcrossTabs() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath("Source");
        const QString target = fixture.filePath("Target");
        QVERIFY(QDir().mkpath(source));
        QVERIFY(QDir().mkpath(target));
        writeFile(source + "/данные file.txt", QByteArrayLiteral("clipboard bytes\0with zero"));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openPath(source);
        auto *table = visibleTable(window);
        QTRY_VERIFY(table && table->isEnabled());
        int row = -1;
        for (int i = 0; i < table->rowCount(); ++i)
            if (table->model()->index(i, 0).data().toString() == "данные file.txt")
                row = i;
        QVERIFY(row >= 0);
        table->selectRow(row);
        window.findChild<QAction *>("copyFilesAction")->trigger();
        QVERIFY(QApplication::clipboard()->mimeData()->hasUrls());
        window.openPath(target);
        auto *paste = window.findChild<QAction *>("pasteFilesAction");
        QTRY_VERIFY(paste->isEnabled());
        paste->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(target + "/данные file.txt"), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(table->isEnabled(), 10000);
        QFile result(target + "/данные file.txt");
        QVERIFY(result.open(QIODevice::ReadOnly));
        QCOMPARE(result.readAll(), QByteArrayLiteral("clipboard bytes\0with zero"));
        QVERIFY(QFileInfo::exists(source + "/данные file.txt"));
        QApplication::clipboard()->clear();
    }

    void favoriteMigrationRenameReorderAndRemovalLeaveFilesIntact() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString first = fixture.filePath("Folder A");
        const QString second = fixture.filePath("Folder B");
        QVERIFY(QDir().mkpath(first));
        QVERIFY(QDir().mkpath(second));
        const QString settings = fixture.filePath("favorites.ini");
        CFastFolders legacy;
        legacy.SetString(3, ToUString(first));
        legacy.Save(settings);
        QString identity;
        {
            QtFavoriteStore store(settings);
            QCOMPARE(store.rowCount(), 1);
            identity = store.Favorite(0).id;
            QVERIFY(store.setData(store.index(0), "Work", Qt::EditRole));
            store.Add(second, "Other");
            store.Add(first, "Duplicate");
            store.Add(first + QDir::separator(), "Same directory from navigation");
            QCOMPARE(store.rowCount(), 2);
            QVERIFY(store.moveRows({}, 0, 1, {}, 2));
            QCOMPARE(store.Favorite(1).id, identity);
        }
        QtFavoriteStore restored(settings);
        QCOMPARE(restored.Favorite(1).label, "Work");
        QCOMPARE(restored.Favorite(1).id, identity);
        QVERIFY(restored.removeRow(1));
        QVERIFY(QFileInfo(first).isDir());
        QVERIFY(QFileInfo(second).isDir());
    }

    void canceledCutPastePreservesSourceAndClipboardThenSuccessfulPasteMovesBytes() {
        QTemporaryDir fixture;
        const QString source = fixture.filePath("Cut source");
        const QString destination = fixture.filePath("Cut target");
        QVERIFY(QDir().mkpath(source));
        QVERIFY(QDir().mkpath(destination));
        const QString name = QStringLiteral("перенос file.txt");
        writeFile(source + '/' + name, "original bytes");
        writeFile(destination + '/' + name, "existing bytes");
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openPath(source);
        auto *table = visibleTable(window);
        QTRY_VERIFY(table->isEnabled());
        for (int row = 0; row < table->rowCount(); ++row)
            if (table->model()->index(row, 0).data().toString() == name)
                table->selectRow(row);
        auto *cut = window.findChild<QAction *>("cutFilesAction");
        QTRY_VERIFY(cut->isEnabled());
        QVERIFY(WindowActivation::waitForActive(window));
        table->setFocus();
        QTRY_VERIFY(table->hasFocus());
        QTest::keySequence(table, QKeySequence::Cut);
        window.openPath(destination);
        auto *paste = window.findChild<QAction *>("pasteFilesAction");
        QTRY_VERIFY(paste->isEnabled());
        bool canceled = false;
        QTimer responder;
        dialogs_.connectResponder(responder, &window, [&] {
            if (auto *result = TestProgressDialog::ResultWindow(dialogs_.activeDialog())) {
                TestProgressDialog::CloseResult(*result);
                return;
            }
            if (auto *box = qobject_cast<QMessageBox *>(dialogs_.activeDialog()))
                box->accept();
            if (auto *dialog = dialogs_.activeDialog())
                if (auto *cancel = dialog->findChild<QPushButton *>("overwriteCancel")) {
                    canceled = true;
                    cancel->click();
                }
        });
        responder.start(1);
        table->setFocus();
        QTRY_VERIFY(table->hasFocus());
        QTest::keySequence(table, QKeySequence::Paste);
        QTRY_VERIFY_WITH_TIMEOUT(canceled && table->isEnabled(), 10000);
        responder.stop();
        QFile old(destination + '/' + name);
        QVERIFY(old.open(QIODevice::ReadOnly));
        QCOMPARE(old.readAll(), "existing bytes");
        old.close();
        QVERIFY(QFileInfo::exists(source + '/' + name));
        QVERIFY(QApplication::clipboard()->mimeData()->hasUrls());
        window.openPath(source);
        QTRY_VERIFY(table->isEnabled());
        bool stillCut = false;
        for (int row = 0; row < table->rowCount(); ++row)
            if (table->model()->index(row, 0).data().toString() == name)
                stillCut = table->model()->index(row, 0).data(QtPanelList::CutRole).toBool();
        QVERIFY(stillCut);
        window.openPath(destination);
        QTRY_VERIFY(table->isEnabled());
        QVERIFY(QFile::remove(destination + '/' + name));
        QTRY_VERIFY(paste->isEnabled());
        paste->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(source + '/' + name) && table->isEnabled(),
                                 10000);
        QVERIFY(old.open(QIODevice::ReadOnly));
        QCOMPARE(old.readAll(), "original bytes");
        const auto *cleared = QApplication::clipboard()->mimeData();
        QVERIFY(!cleared || !cleared->hasUrls());
        for (int row = 0; row < table->rowCount(); ++row)
            QVERIFY(!table->model()->index(row, 0).data(QtPanelList::CutRole).toBool());
    }

    void defaultPlacesCanBeRenamedRemovedAndStayEmptyAfterRestart() {
        QTemporaryDir fixture;
        const QString settings = fixture.filePath("default-places.ini");
        QString id;
        {
            QtFavoriteStore store(settings);
            QVERIFY(store.rowCount() > 0);
            id = store.Favorite(0).id;
            QVERIFY(store.setData(store.index(0), "My personal label", Qt::EditRole));
            store.RefreshLabels();
            QCOMPARE(store.Favorite(0).label, "My personal label");
        }
        {
            QtFavoriteStore restored(settings);
            QCOMPARE(restored.Favorite(0).id, id);
            QCOMPARE(restored.Favorite(0).label, "My personal label");
            QVERIFY(restored.removeRows(0, restored.rowCount()));
        }
        QtFavoriteStore empty(settings);
        QCOMPARE(empty.rowCount(), 0);
    }

    void mountManagersFollowTabsAcrossWindows() {
        ControlledBackend firstBackend, secondBackend;
        auto first = std::make_unique<MainWindow>(&firstBackend);
        MainWindow second(&secondBackend);
        first->show();
        second.show();
        first->findChild<QAction *>("manageMountsAction")->trigger();
        const QPointer<QtArchiveMountManager> firstMounts = dynamic_cast<QtArchiveMountManager *>(
            first->findChild<QDialog *>("archiveMountManager"));
        QVERIFY(firstMounts && firstMounts->isVisible());
        workspace(*first)->NewTab(0);
        auto *tabs = first->findChild<QTabBar *>("panelTabs0");
        QTRY_COMPARE(tabs->count(), 2);
        first->findChild<QAction *>("manageMountsAction")->trigger();
        QPointer<QtArchiveMountManager> secondMounts;
        for (auto *dialog : first->findChildren<QDialog *>())
            if (auto *manager = dynamic_cast<QtArchiveMountManager *>(dialog);
                manager && manager != firstMounts)
                secondMounts = manager;
        QVERIFY(secondMounts && secondMounts->isVisible());
        const auto moving = tabs->tabData(tabs->currentIndex()).toString();
        workspace(*first)->CloseTab(0, 0);
        QTRY_COMPARE(tabs->count(), 1);
        QVERIFY(!firstMounts);
        QVERIFY(secondMounts && secondMounts->isVisible());
        QVERIFY(workspace(*first)->TransferTab(moving, *workspace(second), 0));
        first.reset();
        QVERIFY(secondMounts && secondMounts->isVisible());
        QCOMPARE(secondMounts->parentWidget(), &second);
        auto *receiverTabs = second.findChild<QTabBar *>("panelTabs0");
        QTRY_COMPARE(receiverTabs->count(), 2);
        workspace(second)->CloseTab(0, receiverTabs->currentIndex());
        QTRY_COMPARE(receiverTabs->count(), 1);
        QVERIFY(!secondMounts);
        QVERIFY(second.isVisible());
    }
    void movingTabToAnotherWindowPreservesSelectionAndSurvivesSourceClose_data() {
        QTest::addColumn<bool>("refuseClose");
        QTest::newRow("ordinary-close") << false;
        QTest::newRow("protected-origin") << true;
    }
    void movingTabToAnotherWindowPreservesSelectionAndSurvivesSourceClose() {
        QFETCH(bool, refuseClose);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath("Moving source");
        const QString destination = fixture.filePath("Paste target");
        QVERIFY(QDir().mkpath(source));
        QVERIFY(QDir().mkpath(destination));
        writeFile(source + "/moved context.txt", "move context");
        auto sourceBackend = std::make_unique<ControlledBackend>();
        std::unique_ptr<MainWindow> first;
        if (refuseClose)
            first = std::make_unique<CloseProtectedWindow>(sourceBackend.get());
        else
            first = std::make_unique<MainWindow>(sourceBackend.get());
        ControlledBackend receiverBackend;
        MainWindow second(&receiverBackend);
        first->show();
        second.show();
        first->openPath(source);
        second.openPath(destination);
        auto *table = visibleTable(*first);
        QTRY_VERIFY(table && table->isEnabled());
        int selected = -1;
        for (int row = 0; row < table->rowCount(); ++row)
            if (table->model()->index(row, 0).data().toString() == "moved context.txt")
                selected = row;
        QVERIFY(selected >= 0);
        table->selectRow(selected);
        table->setCurrentCell(selected, 0, QItemSelectionModel::NoUpdate);
        auto *tabs = first->findChild<QTabBar *>("panelTabs0");
        QVERIFY(workspace(*first)->TransferTab(tabs->tabData(tabs->currentIndex()).toString(),
                                               *workspace(second), 0));
        if (refuseClose) {
            QVERIFY(first->isVisible());
            QTRY_VERIFY(visibleTable(*first));
            QTRY_COMPARE(QDir::cleanPath(ToQString(
                             visibleTable(*first)->sourcePanel->GetCurrentFolderPrefix())),
                         QDir::homePath());
        }
        first.reset();
        sourceBackend.reset();
        auto *received = visibleTable(second);
        QVERIFY(received);
        QCOMPARE(QDir::cleanPath(ToQString(received->sourcePanel->GetCurrentFolderPrefix())),
                 source);
        QCOMPARE(received->model()->index(received->currentRow(), 0).data().toString(),
                 "moved context.txt");
        QVERIFY(received->selectionModel()->isRowSelected(received->currentRow(), {}));
        second.findChild<QAction *>("copyFilesAction")->trigger();
        second.openPath(destination);
        second.findChild<QAction *>("pasteFilesAction")->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(destination + "/moved context.txt"), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(received->isEnabled(), 10000);
        QFile result(destination + "/moved context.txt");
        QVERIFY(result.open(QIODevice::ReadOnly));
        QCOMPARE(result.readAll(), "move context");
        QApplication::clipboard()->clear();
    }
};

QTEST_MAIN(WorkspaceNavigationTests)
#include "workspace_navigation_tests.moc"
