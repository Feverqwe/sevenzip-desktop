#include "filemanager/panels/qtpanellist.h"
#include "filemanager/support/window_state_support.h"
#include "support/window_activation.h"
#include "ui/support/layout_inspection.h"
#include <QClipboard>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QScopeGuard>
#include <QScrollBar>
#include <QSignalSpy>

class WorkspaceSelectionGestureTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    static QSet<QString> selected(QAbstractItemView &view) {
        QSet<QString> result;
        for (const auto &index : view.selectionModel()->selectedRows())
            result.insert(index.data().toString());
        return result;
    }
    static void move(QWidget &viewport, QPoint point, Qt::KeyboardModifiers modifiers = {}) {
        QMouseEvent event(QEvent::MouseMove, point, viewport.mapToGlobal(point), Qt::NoButton,
                          Qt::LeftButton, modifiers);
        QCoreApplication::sendEvent(&viewport, &event);
        QApplication::processEvents();
    }
  private slots:
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }
    void cleanup() { QApplication::clipboard()->clear(); }
    void doubleClickUsesThePaneAndViewThatReceivedIt() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QVERIFY(QDir().mkpath(fixture.filePath("child/grandchild")));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openPath(fixture.path());
        auto *first = table(window);
        QTRY_VERIFY(first->isEnabled() && first->rowCount() == 1);
        action(window, "viewMode3Action")->trigger();
        action(window, "twoPanelsAction")->trigger();
        auto *secondPane = window.findChild<QWidget *>("filePanel1");
        QVERIFY(secondPane);
        auto *second = secondPane->findChild<FileTable *>("archiveTable");
        QVERIFY(second);
        auto *address = secondPane->findChild<QLineEdit *>("archivePath");
        QVERIFY(address);
        address->setFocus();
        address->setText(fixture.path());
        QTest::keyClick(address, Qt::Key_Return);
        QTRY_VERIFY(second->isEnabled() && second->rowCount() == 1);
        action(window, "viewMode3Action")->trigger();
        QVERIFY(WindowActivation::waitForActive(window));
        first->setFocus();
        QTRY_VERIFY(first->hasFocus());

        const auto child = second->model()->index(0, 0);
        auto *hiddenIcons = first->parentWidget()->findChild<QAbstractItemView *>("iconList");
        QVERIFY(hiddenIcons && !hiddenIcons->isVisible());
        emit hiddenIcons->doubleClicked(hiddenIcons->model()->index(0, 0));
        QCOMPARE(first->model()->index(0, 0).data().toString(), QString("child"));
        emit second->doubleClicked(child);
        QCOMPARE(first->model()->index(0, 0).data().toString(), QString("child"));

        const QPoint point = second->visualRect(child).center();
        QCOMPARE(second->indexAt(point), child);
        QTest::mouseClick(second->viewport(), Qt::LeftButton, Qt::NoModifier, point);
        QTest::mouseDClick(second->viewport(), Qt::LeftButton, Qt::NoModifier, point);
        QTRY_COMPARE(second->model()->index(0, 0).data().toString(), QString("grandchild"));
        QCOMPARE(first->model()->index(0, 0).data().toString(), QString("child"));
    }
    void doubleClickOpensFoldersOnlyWithTheLeftButton_data() {
        QTest::addColumn<bool>("icons");
        QTest::newRow("table") << false;
        QTest::newRow("icons") << true;
    }
    void doubleClickOpensFoldersOnlyWithTheLeftButton() {
        QFETCH(bool, icons);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString child = fixture.filePath("child");
        QVERIFY(QDir().mkpath(child));
        QFile control(child + "/inside.txt");
        QVERIFY(control.open(QIODevice::WriteOnly));
        control.close();
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openPath(fixture.path());
        auto *files = table(window);
        QTRY_VERIFY(files->isEnabled() && filesystemRow(window, "child") >= 0);
        action(window, icons ? "viewMode0Action" : "viewMode3Action")->trigger();
        auto *view = icons ? window.findChild<QAbstractItemView *>("iconList") : files;
        QVERIFY(view && view->isVisible());
        const auto index = view->model()->index(filesystemRow(window, "child"), 0);
        const QPoint point = view->visualRect(index).center();
        QCOMPARE(view->indexAt(point), index);
        QTest::mouseClick(view->viewport(), Qt::RightButton, Qt::NoModifier, point);
        QTest::mouseDClick(view->viewport(), Qt::RightButton, Qt::NoModifier, point);
        QCOMPARE(filesystemRow(window, "inside.txt"), -1);
        QVERIFY(filesystemRow(window, "child") >= 0);
        QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, point);
        if (icons) {
            const QPoint padding = view->visualRect(index).topRight() + QPoint(-2, 2);
            QCOMPARE(view->indexAt(padding), index);
            QTest::mouseDClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, padding);
            QCOMPARE(filesystemRow(window, "inside.txt"), -1);
        }
        QTest::mouseDClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, point);
        QTRY_VERIFY(filesystemRow(window, "inside.txt") >= 0);
    }
    void blankAreaSelectsIntersectedEntries_data() {
        QTest::addColumn<bool>("icons");
        QTest::addColumn<int>("modifier");
        QTest::addColumn<bool>("cancel");
        QTest::addColumn<bool>("padding");
        QTest::addColumn<bool>("inactive");
        for (bool icons : {false, true})
            for (int modifier :
                 {int(Qt::NoModifier), int(Qt::ControlModifier), int(Qt::ShiftModifier)})
                for (bool cancel : {false, true})
                    QTest::newRow(qPrintable(QString("%1-%2-%3")
                                                 .arg(icons ? "icons" : "table")
                                                 .arg(modifier)
                                                 .arg(cancel)))
                        << icons << modifier << cancel << false << false;
        QTest::newRow("table-cell-padding")
            << false << int(Qt::NoModifier) << false << true << false;
        QTest::newRow("icons-cell-padding")
            << true << int(Qt::NoModifier) << false << true << false;
        QTest::newRow("inactive-table-pane")
            << false << int(Qt::NoModifier) << false << false << true;
        QTest::newRow("inactive-icons-pane")
            << true << int(Qt::NoModifier) << false << false << true;
    }
    void blankAreaSelectsIntersectedEntries() {
        QFETCH(bool, icons);
        QFETCH(int, modifier);
        QFETCH(bool, cancel);
        QFETCH(bool, padding);
        QFETCH(bool, inactive);
        const Qt::KeyboardModifiers modifiers(modifier);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        for (const QString name : {"Alpha.txt", "Beta.txt", "Gamma.txt", "Delta.txt", "Echo.txt"}) {
            QFile file(fixture.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write("gesture bytes"), qint64(13));
        }
        ControlledBackend backend;
        MainWindow window(&backend);
        window.resize(980, 620);
        window.show();
        window.openPath(fixture.path());
        auto *files = table(window);
        QTRY_VERIFY(files->isEnabled() && files->rowCount() == 5);
        if (icons)
            action(window, "viewMode0Action")->trigger();
        auto *grid = dynamic_cast<FileIconList *>(window.findChild<QListView *>("iconList"));
        QVERIFY(grid);
        QAbstractItemView &view = icons ? static_cast<QAbstractItemView &>(*grid) : *files;
        QVERIFY(WindowActivation::waitForActive(window));
        view.setFocus();
        QTRY_VERIFY(view.hasFocus());
        FileTable *other = nullptr;
        QSet<QString> otherSelection;
        if (inactive) {
            action(window, "twoPanelsAction")->trigger();
            auto *pane = window.findChild<QWidget *>("filePanel1");
            QVERIFY(pane);
            other = pane->findChild<FileTable *>("archiveTable");
            QVERIFY(other);
            other->setFocus();
            QTRY_VERIFY(other->hasFocus());
            otherSelection = selected(*other);
        }
        const auto item = [&](const QString &name) {
            for (int row = 0; row < view.model()->rowCount(); ++row) {
                const auto index = view.model()->index(row, 0);
                if (index.data(Qt::EditRole).toString() == name)
                    return index;
            }
            return QModelIndex();
        };
        const auto target = item("Beta.txt");
        QVERIFY(target.isValid());
        const auto alpha = item("Alpha.txt");
        const auto echo = item("Echo.txt");
        const QRect cell = view.visualRect(icons ? alpha : echo);
        const QPoint start =
            padding ? cell.topRight() + QPoint(-2, 2)
                    : QPoint(view.viewport()->width() - 5, view.viewport()->height() - 5);
        QCOMPARE(view.indexAt(start).isValid(), padding);
        if (padding && !icons) {
            QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, start);
            QCOMPARE(selected(view), QSet<QString>{"Echo.txt"});
            QCOMPARE(view.currentIndex(), echo);
        }
        view.selectionModel()->setCurrentIndex(alpha, QItemSelectionModel::NoUpdate);
        view.selectionModel()->select(alpha, QItemSelectionModel::ClearAndSelect |
                                                 QItemSelectionModel::Rows);
        view.selectionModel()->select(target,
                                      QItemSelectionModel::Select | QItemSelectionModel::Rows);
        const auto baseline = selected(view);
        const QPoint end = view.visualRect(target).center();
        const QRect area = QRect(start, end).normalized();
        QSet<QString> expected;
        for (int row = 0; row < view.model()->rowCount(); ++row) {
            const auto index = view.model()->index(row, 0);
            if (area.intersects(view.visualRect(index)))
                expected.insert(index.data().toString());
        }
        QVERIFY(expected.size() >= 2);
        if (modifiers.testFlag(Qt::ControlModifier))
            expected = (expected - baseline) | (baseline - expected);
        else if (modifiers.testFlag(Qt::ShiftModifier))
            expected |= baseline;
        int exports = 0;
        files->dragStarter = [&] { ++exports; };
        grid->dragStarter = [&] { ++exports; };
        QTest::mousePress(view.viewport(), Qt::LeftButton, modifiers, start);
        auto release = qScopeGuard(
            [&] { QTest::mouseRelease(view.viewport(), Qt::LeftButton, modifiers, end); });
        for (int step = 1; step <= 5; ++step)
            move(*view.viewport(), start + (end - start) * step / 5, modifiers);
        QCOMPARE(selected(view), expected);
        // Re-entering the same rectangle must not toggle it a second time.
        move(*view.viewport(), start + (end - start) / 2, modifiers);
        move(*view.viewport(), end, modifiers);
        QCOMPARE(selected(view), expected);
        QCOMPARE(exports, 0);
        if (!modifier && !cancel && !padding && !inactive)
            QVERIFY(UiInspection::writeReport(view, icons ? "blank-selection-icons"
                                                          : "blank-selection-table"));
        if (cancel) {
            QTest::keyClick(&view, Qt::Key_Escape);
            expected = baseline;
            QCOMPARE(selected(view), expected);
            QCOMPARE(view.currentIndex(), alpha);
            move(*view.viewport(), end + QPoint(0, 20), modifiers);
        } else
            move(*view.viewport(), start + (end - start) / 2, modifiers);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, modifiers, end);
        release.dismiss();
        QCOMPARE(selected(view), expected);
        action(window, "copyFilesAction")->trigger();
        QSet<QString> copied;
        for (const auto &url : QApplication::clipboard()->mimeData()->urls())
            copied.insert(QFileInfo(url.toLocalFile()).fileName());
        QCOMPARE(copied, expected);
        if (other)
            QCOMPARE(selected(*other), otherSelection);
    }
    void itemDragAndEdgeScroll_data() {
        QTest::addColumn<bool>("icons");
        QTest::newRow("table") << false;
        QTest::newRow("icons") << true;
    }
    void itemDragAndEdgeScroll() {
        QFETCH(bool, icons);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        for (int i = 0; i < 120; ++i) {
            QFile file(fixture.filePath(QString("Entry %1.txt").arg(i, 3, 10, QChar('0'))));
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        ControlledBackend backend;
        MainWindow window(&backend);
        window.resize(980, 620);
        window.show();
        window.openPath(fixture.path());
        auto *files = table(window);
        QTRY_VERIFY(files->isEnabled() && files->rowCount() >= 120);
        if (icons)
            action(window, "viewMode0Action")->trigger();
        auto *grid = dynamic_cast<FileIconList *>(window.findChild<QListView *>("iconList"));
        QVERIFY(grid);
        QAbstractItemView &view = icons ? static_cast<QAbstractItemView &>(*grid) : *files;
        QVERIFY(WindowActivation::waitForActive(window));
        view.setFocus();
        QTRY_VERIFY(view.hasFocus());
        QModelIndex first;
        for (int row = 0; row < view.model()->rowCount(); ++row) {
            const auto index = view.model()->index(row, 0);
            if (index.data(Qt::EditRole).toString() == "Entry 000.txt")
                first = index;
        }
        QVERIFY(first.isValid());
        view.scrollTo(first, QAbstractItemView::PositionAtTop);
        QApplication::processEvents();
        const QRect cell = view.visualRect(first);
        const QPoint content =
            icons ? QPoint(cell.center().x(), cell.top() + view.iconSize().height() / 2)
                  : QPoint(cell.left() + 20, cell.center().y());
        int exports = 0;
        files->dragStarter = [&] { ++exports; };
        grid->dragStarter = [&] { ++exports; };
        QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, content);
        move(*view.viewport(), content + QPoint(50, 30));
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                            content + QPoint(50, 30));
        QCOMPARE(exports, 1);
        const QPoint blank = cell.topRight() + QPoint(-2, 2);
        QVERIFY(view.viewport()->rect().contains(blank));
        const int before = view.verticalScrollBar()->value();
        const QPoint end(20, view.viewport()->height() + 20);
        QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, blank);
        auto release = qScopeGuard(
            [&] { QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, end); });
        move(*view.viewport(), end);
        const auto initiallySelected = selected(view);
        QElapsedTimer moving;
        moving.start();
        while (moving.elapsed() < 150) {
            move(*view.viewport(), end + QPoint(int(moving.elapsed() % 3), 0));
            QTest::qWait(5);
        }
        QVERIFY(view.verticalScrollBar()->value() > before);
        QTRY_VERIFY(selected(view).size() > initiallySelected.size());
        QCOMPARE(exports, 1);
        QTest::keyClick(&view, Qt::Key_Escape);
        const auto restored = selected(view);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, end);
        release.dismiss();
        QCOMPARE(selected(view), restored);
    }
};

#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(WorkspaceSelectionGestureTests)
#include "workspace_selection_gesture_tests.moc"
