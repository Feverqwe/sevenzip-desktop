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
#include <QAccessible>
#include <QClipboard>
#include <QListWidget>
#include <QMenu>
#include <QScopeGuard>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>

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

    void addressTabReturnsToTheListAfterRefresh_data() {
        QTest::addColumn<int>("mode");
        QTest::addColumn<int>("key");
        QTest::newRow("details-tab") << 3 << int(Qt::Key_Tab);
        QTest::newRow("icons-backtab") << 0 << int(Qt::Key_Backtab);
    }

    void addressTabReturnsToTheListAfterRefresh() {
        QFETCH(int, mode);
        QFETCH(int, key);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        window.openPath(fixture.path());
        auto *table = visibleTable(window);
        QVERIFY(table);
        QTRY_VERIFY(table->isEnabled());
        window.findChild<QAction *>(QString("viewMode%1Action").arg(mode))->trigger();
        auto *view = mode == 3 ? static_cast<QAbstractItemView *>(table)
                               : window.findChild<QListView *>("iconList");
        QVERIFY(view && view->isVisible());
        table->sourcePanel->Set_AutoRefresh_Mode(false);
        for (int i = 0; i < 301; ++i) {
            QFile file(fixture.filePath(QString("item-%1.txt").arg(i)));
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        window.findChild<QAction *>("refreshAction")->trigger();
        QVERIFY(!view->isEnabled());
        auto *address = window.findChild<QLineEdit *>("archivePath");
        PanelKeys::focusAddress(address);
        QVERIFY(address->hasFocus());
        QTest::keyClick(address, static_cast<Qt::Key>(key));
        QTRY_VERIFY(view->isEnabled() && view->hasFocus());
        QCOMPARE(table->rowCount(), 301);
    }
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
    void locationBarShowsFullPathWhenItFits() {
        QWidget host;
        auto *address = new QComboBox(&host);
        address->setEditable(true);
        QString opened;
        QtNavigationBar bar(*address, [&](const QString &target) { opened = target; }, &host);
        const QString longName = QStringLiteral("long folder name ").repeated(4).trimmed();
        const QString parent = QStringLiteral("/Users/Projects & Docs");
        const std::vector<QtAddressSegment> segments = {
            {L"/", L"/", 0, QtAddressSegmentKind::Root},
            {L"Users", L"/Users", 0, QtAddressSegmentKind::Folder},
            {L"Projects & Docs", ToUString(parent), 0, QtAddressSegmentKind::Folder},
            {ToUString(longName), ToUString(parent + '/' + longName), 0,
             QtAddressSegmentKind::Folder},
            {L"newsBot", ToUString(parent + '/' + longName + "/newsBot"), 0,
             QtAddressSegmentKind::Folder}};
        bar.resize(1400, 36);
        host.resize(1400, 36);
        bar.SetLocation(ToQString(segments.back().target), segments);
        host.show();
        const auto visibleLabels = [&] {
            QStringList labels;
            for (auto *button : bar.findChildren<QToolButton *>())
                if (button->isVisible() && button->objectName() != "editLocationButton")
                    labels.append(button->text());
            return labels;
        };
        const QStringList fullPath{"/", "Users", "Projects && Docs", longName, "newsBot"};
        QTRY_COMPARE(visibleLabels(), fullPath);
        QVERIFY(UiInspection::writeReport(bar, "literal-breadcrumb-wide"));
        bar.resize(260, 36);
        QTRY_COMPARE(visibleLabels(), (QStringList{QStringLiteral("…"), "newsBot"}));
        QVERIFY(QTest::qWaitForWindowActive(&host));
        QToolButton *overflowButton = nullptr;
        QToolButton *edit = nullptr;
        for (auto *button : bar.findChildren<QToolButton *>()) {
            if (!button->isVisible())
                continue;
            if (button->menu())
                overflowButton = button;
            if (button->objectName() == "editLocationButton")
                edit = button;
        }
        QVERIFY(overflowButton && edit);
        const auto *accessible = QAccessible::queryAccessibleInterface(overflowButton);
        QVERIFY(accessible);
        QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("Parent folders"));
        QCOMPARE(overflowButton->toolTip(), accessible->text(QAccessible::Name));
        edit->setFocus();
        QTest::keyClick(edit, Qt::Key_Backtab);
        QVERIFY(bar.isAncestorOf(host.focusWidget()));
        QTest::keyClick(host.focusWidget(), Qt::Key_Backtab);
        QCOMPARE(host.focusWidget(), overflowButton);
        auto *overflow = overflowButton->menu();
        QAction *entry = nullptr;
        for (auto *action : overflow->actions())
            if (action->toolTip() == parent)
                entry = action;
        QVERIFY(entry);
        QCOMPARE(entry->text(), QStringLiteral("Projects && Docs"));
        QVERIFY(QKeySequence::mnemonic(entry->text()).isEmpty());
        bool selectedWithKeyboard = false;
        connect(overflow, &QMenu::aboutToShow, overflow, [&] {
            QTimer::singleShot(0, overflow, [&] {
                const auto close = qScopeGuard([&] { overflow->hide(); });
                QVERIFY(overflow->isVisible() && entry->isVisible());
                QVERIFY(UiInspection::writeReport(*overflow, "literal-breadcrumb-overflow"));
                for (int i = 0; i < overflow->actions().size() && overflow->activeAction() != entry;
                     ++i)
                    QTest::keyClick(overflow, Qt::Key_Down);
                QCOMPARE(overflow->activeAction(), entry);
                QTest::keyClick(overflow, Qt::Key_Return);
                selectedWithKeyboard = true;
            });
        });
        QTest::keyClick(overflowButton, Qt::Key_Space);
        QTRY_VERIFY(selectedWithKeyboard);
        QCOMPARE(opened, parent);
        bar.resize(1400, 36);
        QTRY_COMPARE(visibleLabels(), fullPath);
    }
    void locationBarUsesNativeParentTargets() {
        QTemporaryDir fixture;
        const QString parent = fixture.filePath(QStringLiteral("folder with archive.zip"));
        const QString child = parent + QStringLiteral("/child 空");
        QVERIFY(QDir().mkpath(child));
        QVERIFY(QDir(parent).mkdir("another folder"));
        writeFile(child + "/keep.txt", "unchanged");
        ControlledBackend backend;
        MainWindow window(&backend);
        window.resize(980, 620);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.openPath(child);
        auto *table = visibleTable(window);
        QTRY_VERIFY(table && table->isEnabled());
        auto *bar =
            dynamic_cast<QtNavigationBar *>(window.findChild<QWidget *>("panelNavigation0"));
        QVERIFY(bar);
        QToolButton *parentButton = nullptr;
        QToolButton *currentButton = nullptr;
        QToolButton *edit = nullptr;
        for (auto *button : bar->findChildren<QToolButton *>()) {
            if (!button->isVisible())
                continue;
            if (QDir::cleanPath(button->toolTip()) == parent)
                parentButton = button;
            if (QDir::cleanPath(button->toolTip()) == child)
                currentButton = button;
            if (button->objectName() == "editLocationButton")
                edit = button;
        }
        QVERIFY(parentButton && currentButton && edit);
        QVERIFY(parentButton->icon().isNull());
        const auto matches =
            table->model()->match(table->model()->index(0, 0), Qt::DisplayRole, "keep.txt");
        QCOMPARE(matches.size(), 1);
        table->selectRow(matches.front().row());
        QTest::mouseClick(currentButton, Qt::LeftButton);
        QTRY_VERIFY(table->hasFocus());
        QCOMPARE(table->selectionModel()->selectedRows().size(), 1);
        QCOMPARE(table->selectionModel()->selectedRows().front().data().toString(),
                 QString("keep.txt"));
        edit->setFocus();
        QTest::keyClick(edit, Qt::Key_Backtab);
        QVERIFY(bar->isAncestorOf(window.focusWidget()));
        QTest::keyClick(window.focusWidget(), Qt::Key_Backtab);
        QTRY_VERIFY(parentButton->hasFocus());
        QTest::keyClick(parentButton, Qt::Key_Space);
        QTRY_COMPARE(QDir::cleanPath(ToQString(table->sourcePanel->GetCurrentFolderPrefix())),
                     parent);
        QTRY_VERIFY(table->hasFocus());
        QVERIFY(table->sourcePanel->IsFSFolder());
        QCOMPARE(table->currentIndex().siblingAtColumn(0).data().toString(),
                 QStringLiteral("child 空"));
        QCOMPARE(backend.listCalls, 0);
        for (int width : {980, 640}) {
            window.resize(width, 620);
            QApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(*bar, window));
            QVERIFY(UiInspection::writeReport(window, QString("native-location-%1").arg(width)));
        }
        QTest::keyClick(table, Qt::Key_Return);
        QTRY_COMPARE(QDir::cleanPath(ToQString(table->sourcePanel->GetCurrentFolderPrefix())),
                     child);
    }
    void archiveParentBreadcrumbReturnsToTheChildRow() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString archive = fixture.filePath("folders.zip");
        QVERIFY(QDir(fixture.path()).mkdir("another folder"));
        ControlledBackend backend;
        QVERIFY(backend.prepareArchiveFixture(archive));
        MainWindow window(&backend);
        window.resize(1100, 620);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        const auto listArchive = [&] {
            backend.completeList({ArchiveEntry{"another/first.txt", 1, 1},
                                  ArchiveEntry{QStringLiteral("папка 空/keep.txt"), 1, 1}});
        };
        backend.queueListAction(listArchive);
        window.openArchive(archive);
        auto *table = visibleTable(window);
        QVERIFY(table);
        QTRY_VERIFY(table->isEnabled());
        const QString child = QStringLiteral("папка 空");
        const auto rows =
            table->model()->match(table->model()->index(0, 0), Qt::DisplayRole, child);
        QCOMPARE(rows.size(), 1);
        table->setCurrentIndex(rows.front());
        table->setFocus();
        QTest::keyClick(table, Qt::Key_Return);
        auto *bar = window.findChild<QWidget *>("panelNavigation0");
        QVERIFY(bar);
        const auto clickCrumb = [&](const QString &location) {
            for (auto *button : bar->findChildren<QToolButton *>())
                if (button->isVisible() && QDir::cleanPath(button->toolTip()) == location) {
                    QTest::mouseClick(button, Qt::LeftButton);
                    return true;
                }
            return false;
        };
        QTRY_COMPARE(QDir::cleanPath(bar->toolTip()), archive + '/' + child);
        QVERIFY(WindowActivation::waitForActive(window));
        QVERIFY(clickCrumb(archive));
        QTRY_COMPARE(QDir::cleanPath(bar->toolTip()), archive);
        QTRY_VERIFY(table->isEnabled());
        QTRY_VERIFY(table->hasFocus());
        QCOMPARE(table->currentIndex().siblingAtColumn(0).data().toString(), child);
        QTest::keyClick(table, Qt::Key_Return);
        QTRY_COMPARE(QDir::cleanPath(bar->toolTip()), archive + '/' + child);
        QCOMPARE(backend.listCalls, 1);
        QVERIFY(clickCrumb(archive));
        QTRY_COMPARE(QDir::cleanPath(bar->toolTip()), archive);
        QVERIFY(clickCrumb(fixture.path()));
        QTRY_COMPARE(QDir::cleanPath(bar->toolTip()), fixture.path());
        QTRY_VERIFY(table->isEnabled() && table->hasFocus());
        QCOMPARE(table->currentIndex().siblingAtColumn(0).data().toString(),
                 QString("folders.zip"));
        backend.queueListAction(listArchive);
        QTest::keyClick(table, Qt::Key_Return);
        QTRY_COMPARE(QDir::cleanPath(bar->toolTip()), archive);
    }

    void backKeepsTheSelectedFileVisibleAfterSwitchingViews() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString initial = fixture.filePath("initial");
        const QString other = fixture.filePath("other");
        QVERIFY(QDir().mkpath(initial) && QDir().mkpath(other));
        writeFile(initial + "/a.txt", "selected");
        for (int i = 0; i < 150; ++i)
            writeFile(initial + QString("/file-%1.txt").arg(i), {});
        ControlledBackend backend;
        MainWindow window(&backend);
        window.resize(640, 400);
        window.show();
        window.openPath(initial);
        auto *table = visibleTable(window);
        QVERIFY(table);
        QTRY_COMPARE(table->rowCount(), 151);
        table->setCurrentCell(0, 0);
        table->selectRow(0);
        auto *scroll = table->verticalScrollBar();
        QTRY_VERIFY(scroll->maximum() > 0);
        scroll->setValue(scroll->maximum());
        const int savedScroll = scroll->value();
        window.openPath(other);
        auto *back = window.findChild<QAction *>("backAction");
        QVERIFY(back);
        QTRY_VERIFY(back->isEnabled());
        back->trigger();
        QTRY_COMPARE(table->rowCount(), 151);
        QTRY_COMPARE(scroll->value(), savedScroll);
        window.openPath(other);
        window.findChild<QAction *>("viewMode0Action")->trigger();
        auto *icons = window.findChild<QListView *>("iconList");
        QTRY_VERIFY(icons && icons->isVisible());
        back->trigger();
        QTRY_COMPARE(icons->currentIndex().data(QtPanelList::NameRole).toString(),
                     QString("a.txt"));
        QVERIFY(icons->selectionModel()->isSelected(icons->currentIndex()));
        QTRY_VERIFY(icons->viewport()->rect().intersects(icons->visualRect(icons->currentIndex())));
    }

    void hidingLeftPanelKeepsBothLocationsAndRightSelection() {
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
        view->setFocus();
        toggle->trigger();
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
        QCOMPARE(QDir::cleanPath(ToQString(reopened->sourcePanel->GetCurrentFolderPrefix())), left);
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

    void fileClipboardCopiesRealBytesAcrossLocations() {
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

    void refusedCutPasteCanBeRetried_data() {
        QTest::addColumn<bool>("sameFolder");
        QTest::newRow("cancel-overwrite") << false;
        QTest::newRow("same-folder") << true;
    }
    void refusedCutPasteCanBeRetried() {
        QFETCH(bool, sameFolder);
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
        if (!sameFolder)
            window.openPath(destination);
        auto *paste = window.findChild<QAction *>("pasteFilesAction");
        QTRY_VERIFY(paste->isEnabled());
        bool canceled = false;
        QString failure;
        QTimer responder;
        dialogs_.connectResponder(responder, &window, [&] {
            if (auto *result = TestProgressDialog::ResultWindow(dialogs_.activeDialog())) {
                failure = TestProgressDialog::ReportText(*result);
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
        QTRY_VERIFY_WITH_TIMEOUT(
            (sameFolder ? window.statusBar()->currentMessage().contains(name) : canceled) &&
                table->isEnabled(),
            10000);
        if (sameFolder)
            QVERIFY(failure.isEmpty());
        responder.stop();
        QFile old(destination + '/' + name);
        QVERIFY(old.open(QIODevice::ReadOnly));
        QCOMPARE(old.readAll(), "existing bytes");
        old.close();
        QFile original(source + '/' + name);
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), "original bytes");
        original.close();
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

    void mountManagersRemainIndependentAcrossWindows() {
        ControlledBackend firstBackend, secondBackend;
        auto first = std::make_unique<MainWindow>(&firstBackend);
        MainWindow second(&secondBackend);
        first->show();
        second.show();
        first->findChild<QAction *>("manageMountsAction")->trigger();
        const QPointer<QDialog> firstMounts = first->findChild<QDialog *>("archiveMountManager");
        QVERIFY(firstMounts && firstMounts->isVisible());
        second.findChild<QAction *>("manageMountsAction")->trigger();
        const QPointer<QDialog> secondMounts = second.findChild<QDialog *>("archiveMountManager");
        QVERIFY(secondMounts && secondMounts->isVisible());
        first.reset();
        QVERIFY(!firstMounts);
        QVERIFY(secondMounts && secondMounts->isVisible());
        QCOMPARE(secondMounts->parentWidget(), &second);
    }
};

QTEST_MAIN(WorkspaceNavigationTests)
#include "workspace_navigation_tests.moc"
