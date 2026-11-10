#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "ui/support/layout_inspection.h"
#include <QTableWidget>

class PanelSelectedStateTests : public QObject, protected MainWindowTestSupport {
    Q_OBJECT

    static int fileRow(FileTable *view, const QString &path) {
        for (int row = 0; row < view->rowCount(); ++row)
            if (view->model()->index(row, 1).data().toString() == path)
                return row;
        return -1;
    }
    static void createFile(const QString &path) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("fixture"), 7);
    }
    static void selectRow(QAbstractItemView *view, int row) {
        QVERIFY(row >= 0);
        view->selectionModel()->select(view->model()->index(row, 0),
                                       QItemSelectionModel::Select | QItemSelectionModel::Rows);
    }
    static void openFolderFromVisibleList(MainWindow &window, const QString &folder) {
        const int row = rowWithPath(window, folder);
        QVERIFY(row >= 0);
        auto *stack = window.findChild<QStackedWidget *>("fileViewStack");
        QVERIFY(stack);
        auto *view = qobject_cast<QAbstractItemView *>(stack->currentWidget());
        QVERIFY(view);
        table(window)->selectRow(row);
        view->setFocus();
        QTest::keySequence(view, action(window, "openItemAction")->shortcut());
    }

  private slots:
#ifndef Q_OS_WIN
    void filesystemRuntimeUsesNativeLinkMetadata() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QDir root(QFileInfo(temp.path()).canonicalFilePath());
        QVERIFY(root.mkdir("nested"));
        const QString target = root.filePath("nested/target");
        createFile(target);
        const QString link = root.filePath("file-link");
        const QString cycle = root.filePath("cycle");
        QVERIFY(QFile::link(target, link));
        QVERIFY(QFile::link(root.path(), cycle));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();

        auto *address = window.findChild<QLineEdit *>("archivePath");
        address->setText(root.path());
        QTest::keyClick(address, Qt::Key_Return);
        auto *view = table(window);
        QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
        QTRY_COMPARE(view->rowCount(), 3);
        for (const QString &path : {link, cycle}) {
            const int row = fileRow(view, path);
            QVERIFY(row >= 0);
            QVERIFY(!view->model()->index(row, 0).data(Qt::UserRole + 1).toBool());
            const QString linkTarget = path == link ? target : root.path();
            const quint64 linkSize = QFile::encodeName(linkTarget).size();
            QCOMPARE(view->model()->index(row, 2).data(Qt::UserRole + 2).toULongLong(), linkSize);
        }
        const int directoryRow = fileRow(view, root.filePath("nested"));
        QVERIFY(directoryRow >= 0);
        QVERIFY(view->model()->index(directoryRow, 2).data().toString().isEmpty());
        QVERIFY(!view->model()->index(directoryRow, 2).data(Qt::UserRole + 2).isValid());
        for (const QSize &size : {QSize(980, 620), QSize(640, 480)}) {
            window.resize(size);
            QCoreApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(*window.centralWidget(), window));
            QVERIFY(UiInspection::writeReport(window,
                                              QString("filesystem-runtime-%1").arg(size.width())));
        }

        address->setText(cycle);
        QTest::keyClick(address, Qt::Key_Return);
        QCOMPARE(address->text(), root.path());
        QCOMPARE(view->rowCount(), 3);
    }
#endif
    void archiveRuntimeTypedSizes_data() { filesystemRefreshRetainsCurrentAndSelectedItems_data(); }
    void archiveRuntimeTypedSizes() {
        QFETCH(int, mode);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();

        const QString folder = QStringLiteral("папка с пробелом");
        const QString unknown = folder + QStringLiteral("/неизвестный.txt");
        const QString zero = folder + QStringLiteral("/нулевой.txt");
        QList<ArchiveEntry> entries = {{unknown, -1, -1}, {zero, 0, 0}};
        openListedArchive(window, backend, "typed-sizes.zip", entries);
        entries.clear(); // The runtime retains its own immutable metadata owner.
        action(window, qPrintable(QString("viewMode%1Action").arg(mode)))->trigger();
        openFolderFromVisibleList(window, folder);
        auto *view = table(window);
        const int unknownRow = rowWithPath(window, unknown);
        const int zeroRow = rowWithPath(window, zero);
        QVERIFY(unknownRow >= 0 && zeroRow >= 0);
        for (int column : {2, 3}) {
            QVERIFY(!view->model()->index(unknownRow, column).data(Qt::UserRole + 2).isValid());
            const auto size = view->model()->index(zeroRow, column).data(Qt::UserRole + 2);
            QVERIFY(size.isValid());
            QCOMPARE(size.toULongLong(), 0ull);
        }
        for (const QSize &size : {QSize(980, 620), QSize(640, 480)}) {
            window.resize(size);
            QCoreApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(*window.centralWidget(), window));
            const QWidget *activeView =
                mode == 3 ? static_cast<QWidget *>(view) : window.findChild<QWidget *>("iconList");
            QVERIFY(activeView && activeView->isVisible());
            QVERIFY(UiInspection::fullyVisible(*activeView, window));
            QVERIFY(UiInspection::writeReport(
                window, QString("archive-runtime-mode%1-%2").arg(mode).arg(size.width())));
        }
    }
    void archiveRefreshRetainsCurrentAndSelectedItems_data() {
        filesystemRefreshRetainsCurrentAndSelectedItems_data();
    }

    void archiveRefreshRetainsCurrentAndSelectedItems() {
        QFETCH(int, mode);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();

        const QString folder = QStringLiteral("папка с пробелом");
        const QString selected = folder + QStringLiteral("/имя.txt");
        const QString focused = folder + QStringLiteral("/текущий.txt");
        QList<ArchiveEntry> entries = {
            {selected, 1, 1}, {focused, 2, 2}, {QStringLiteral("другая/имя.txt"), 3, 3}};
        openListedArchive(window, backend, "selection-refresh.zip", entries);
        action(window, qPrintable(QString("viewMode%1Action").arg(mode)))->trigger();
        openFolderFromVisibleList(window, folder);
        auto *view = table(window);
        view->clearSelection();
        selectRow(view, rowWithPath(window, selected));
        view->setCurrentCell(rowWithPath(window, focused), 0, QItemSelectionModel::NoUpdate);
        QSignalSpy publication(view->model(), &QAbstractItemModel::rowsRemoved);
        action(window, "refreshAction")->trigger();
        QCOMPARE(backend.listCalls, 1);
        QVERIFY(!publication.isEmpty());
        QCOMPARE(view->currentRow(), rowWithPath(window, focused));
        QCOMPARE(view->selectionModel()->selectedRows().size(), 1);
        QVERIFY(view->selectionModel()->isRowSelected(rowWithPath(window, selected), {}));
        QVERIFY(!view->selectionModel()->isRowSelected(rowWithPath(window, focused), {}));
    }

    void failedArchiveOpenDiscardsPendingSnapshot_data() {
        QTest::addColumn<bool>("cancel");
        QTest::newRow("cancel") << true;
        QTest::newRow("failure") << false;
    }

    void failedArchiveOpenDiscardsPendingSnapshot() {
        QFETCH(bool, cancel);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        openListedArchive(window, backend, "first.zip", {{QStringLiteral("имя.txt"), 1, 1}});
        table(window)->selectRow(0);
        const QString failedArchive = QFileInfo("failed-open.zip").absoluteFilePath();
        QVERIFY(backend.prepareArchiveFixture(failedArchive));
        const int calls = backend.listCalls;
        backend.queueListAction([&] {
            if (cancel)
                backend.cancel();
            else
                backend.fail("fixture list failure");
        });
        QTimer responder;
        connect(&responder, &QTimer::timeout, &window, [] { dismissWarnings(); });
        responder.start(10);
        window.openArchive(failedArchive);
        responder.stop();
        QCOMPARE(backend.listCalls, calls + 1);
        QVERIFY(action(window, "createFolderAction")->isEnabled());
        openListedArchive(window, backend, "second.zip", {{QStringLiteral("имя.txt"), 2, 2}});
        QCOMPARE(table(window)->rowCount(), 1);
        QVERIFY(table(window)->selectionModel()->selectedRows().isEmpty());
    }

    void snapshotSeparatesFocusFromSelection() {
        QTableWidget view(3, 1);
        const QStringList names = {QStringLiteral("папка/z space.txt"), "a.txt", "focus.txt"};
        for (int i = 0; i < names.size(); ++i) {
            auto *item = new QTableWidgetItem(names[i]);
            item->setData(Qt::UserRole + 3, names[i].section('/', -1));
            view.setItem(i, 0, item);
        }
        CObjectVector<CPanelItem> items;
        for (const QString &name : names) {
            CPanelItem item;
            item.RelPath = name;
            items.Add(item);
        }
        CPanel panel("", items);
        panel.ChangeFlatMode();
        panel.AttachSelectionView(&view);
        view.setCurrentCell(2, 0, QItemSelectionModel::NoUpdate);
        selectRow(&view, 0);
        selectRow(&view, 1);
        CSelectedState state;
        panel.SaveSelectedState(state);
        QCOMPARE(state.FocusedItem, 2);
        QVERIFY(state.FocusedName_Defined);
        QCOMPARE(ToQString(state.FocusedName), names[2]);
        QVERIFY(!state.SelectFocused);
        QCOMPARE(state.SelectedNames.Size(), 2u);
        QCOMPARE(ToQString(state.SelectedNames[0]), names[1]);
        QCOMPARE(ToQString(state.SelectedNames[1]), names[0]);
        view.clearSelection();
        view.setCurrentCell(0, 0, QItemSelectionModel::NoUpdate);
        panel.RefreshListCtrl(state);
        QCOMPARE(view.currentRow(), 2);
        QVERIFY(!view.selectionModel()->isRowSelected(2, QModelIndex()));
        QVERIFY(view.selectionModel()->isRowSelected(0, QModelIndex()));
        QVERIFY(view.selectionModel()->isRowSelected(1, QModelIndex()));
        CPanel emptyPanel;
        emptyPanel.SaveSelectedState(state);
        QCOMPARE(state.FocusedItem, -1);
        QVERIFY(!state.FocusedName_Defined);
        QVERIFY(state.SelectFocused);
        QCOMPARE(state.SelectedNames.Size(), 0u);
    }

    void switchingFileViewsKeepsCurrentItemVisible_data() {
        QTest::addColumn<int>("column");
        QTest::newRow("name") << 0;
        QTest::newRow("size") << 2;
    }

    void switchingFileViewsKeepsCurrentItemVisible() {
        QFETCH(int, column);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QDir root(QFileInfo(temp.path()).canonicalFilePath());
        for (int i = 0; i < 120; ++i)
            createFile(root.filePath(QString("item-%1.txt").arg(i, 3, 10, QLatin1Char('0'))));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.resize(640, 480);
        window.show();

        auto *address = window.findChild<QLineEdit *>("archivePath");
        address->setText(root.path());
        QTest::keyClick(address, Qt::Key_Return);
        auto *details = table(window);
        auto *icons = window.findChild<QAbstractItemView *>("iconList");
        QVERIFY(icons);
        auto *detailsAction = action(window, "viewMode3Action");
        auto *iconsAction = action(window, "viewMode0Action");
        detailsAction->trigger();
        const QString lastPath = root.filePath("item-119.txt");
        const QString firstPath = root.filePath("item-000.txt");
        QTRY_VERIFY(fileRow(details, lastPath) >= 0);
        auto *selection = details->selectionModel();
        selection->clearSelection();
        selectRow(details, fileRow(details, lastPath));
        selectRow(details, fileRow(details, firstPath));
        const auto selectedRows = selection->selectedRows();

        details->setFocus();
        QTRY_VERIFY(details->hasFocus());
        const QModelIndex last = details->model()->index(fileRow(details, lastPath), 0);
        const QModelIndex lastCell = last.siblingAtColumn(column);
        selection->setCurrentIndex(lastCell, QItemSelectionModel::NoUpdate);
        details->scrollTo(lastCell);
        iconsAction->trigger();
        QCOMPARE(icons->currentIndex().row(), last.row());
        QCOMPARE(selection->selectedRows(), selectedRows);
        QTRY_VERIFY(icons->hasFocus());
        QTRY_VERIFY(icons->visualRect(last).intersects(icons->viewport()->rect()));

        const QModelIndex first = details->model()->index(fileRow(details, firstPath), 0);
        selection->setCurrentIndex(first, QItemSelectionModel::NoUpdate);
        icons->scrollTo(first);
        detailsAction->trigger();
        QCOMPARE(details->currentIndex().siblingAtColumn(0), first);
        QCOMPARE(selection->selectedRows(), selectedRows);
        QTRY_VERIFY(details->hasFocus());
        QTRY_VERIFY(details->visualRect(first).intersects(details->viewport()->rect()));
    }

    void filesystemRefreshRetainsCurrentAndSelectedItems_data() {
        QTest::addColumn<int>("mode");
        for (int mode : {0, 3})
            QTest::newRow(qPrintable(QString("mode%1").arg(mode))) << mode;
    }

    void filesystemRefreshRetainsCurrentAndSelectedItems() {
        QFETCH(int, mode);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QDir root(QFileInfo(temp.path()).canonicalFilePath());
        QVERIFY(root.mkdir(QStringLiteral("вложенная")));
        const QString prefix = QString();
        const QString selected = root.filePath(prefix + QStringLiteral("a пробел й.txt"));
        const QString focused = root.filePath(prefix + QStringLiteral("z текущий.txt"));
        createFile(selected);
        createFile(focused);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        auto *address = window.findChild<QLineEdit *>("archivePath");
        address->setFocus();
        address->setText(root.path());
        QTest::keyClick(address, Qt::Key_Return);
        action(window, "viewMode0Action")->trigger();
        action(window, qPrintable(QString("viewMode%1Action").arg(mode)))->trigger();
        auto *view = table(window);
        QTRY_VERIFY(fileRow(view, selected) >= 0);
        QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
        view->clearSelection();
        selectRow(view, fileRow(view, selected));
        view->setCurrentCell(fileRow(view, focused), 0, QItemSelectionModel::NoUpdate);
        createFile(root.filePath(prefix + "0 new.txt"));
        action(window, "refreshAction")->trigger();
        QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
        QTRY_VERIFY(view->currentRow() >= 0);
        QCOMPARE(view->model()->index(view->currentRow(), 1).data().toString(), focused);
        QCOMPARE(view->selectionModel()->selectedRows().size(), 1);
        QCOMPARE(view->model()
                     ->index(view->selectionModel()->selectedRows()[0].row(), 1)
                     .data()
                     .toString(),
                 selected);
        QVERIFY(!view->selectionModel()->isRowSelected(view->currentRow(), QModelIndex()));
        QVERIFY(action(window, "copyAction")->isEnabled());
        QVERIFY(action(window, "selectedPropertiesAction")->isEnabled());

        const int oldFocusedRow = view->currentRow();
        QVERIFY(QFile::remove(focused));
        action(window, "refreshAction")->trigger();
        QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
        QTRY_COMPARE(fileRow(view, focused), -1);
        QCOMPARE(view->currentRow(), qMin(oldFocusedRow, view->rowCount() - 1));
        QCOMPARE(view->selectionModel()->selectedRows().size(), 1);
        QCOMPARE(view->model()
                     ->index(view->selectionModel()->selectedRows()[0].row(), 1)
                     .data()
                     .toString(),
                 selected);
    }
};

QTEST_MAIN(PanelSelectedStateTests)
#include "panel_selected_state_tests.moc"
