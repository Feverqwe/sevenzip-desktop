#include "filemanager/panels/Panel.h"
#include "fileops/patterns/Wildcard.h"
#include <QListView>
#include <QScrollBar>
#include <QTableWidget>
#include <QtTest>

extern bool g_CaseSensitive;

class PanelSelectionTests final : public QObject {
    Q_OBJECT

    static CObjectVector<CPanelItem> items() {
        CObjectVector<CPanelItem> result;
        for (const auto &name : {QStringLiteral("a.tar.gz"), QStringLiteral("b.GZ"),
                                 QStringLiteral("README"), QStringLiteral("folder.gz")}) {
            CPanelItem item;
            item.RelPath = name;
            item.IsFolder = name == QStringLiteral("folder.gz");
            result.Add(item);
        }
        return result;
    }

    static void populate(QTableWidget &view) {
        view.setColumnCount(1);
        view.setRowCount(5);
        view.setSelectionBehavior(QAbstractItemView::SelectRows);
        view.setSelectionMode(QAbstractItemView::ExtendedSelection);
        for (int row = 0; row < 5; ++row) {
            auto *item = new QTableWidgetItem(row == 0 ? QStringLiteral("..")
                                                       : items()[unsigned(row - 1)].RelPath);
            item->setData(Qt::UserRole + 6, row == 0 ? quint64(UInt32(-1)) : quint64(row - 1));
            view.setItem(row, 0, item);
        }
    }

    static bool selected(const QTableWidget &view, int row) {
        return view.selectionModel()->isRowSelected(row, QModelIndex());
    }

  private slots:
    void insertScrollsTheVisibleList() {
        CObjectVector<CPanelItem> entries;
        QTableWidget table(100, 1);
        for (int row = 0; row < 100; ++row) {
            CPanelItem item;
            item.RelPath = QString("file %1.txt").arg(row);
            entries.Add(item);
            auto *cell = new QTableWidgetItem(item.RelPath);
            cell->setData(Qt::UserRole + 6, row);
            table.setItem(row, 0, cell);
        }
        table.setSelectionBehavior(QAbstractItemView::SelectRows);
        CPanel panel(QStringLiteral("/tmp/"), entries);
        panel.AttachSelectionView(&table);
        QListView list;
        list.setModel(table.model());
        list.setSelectionModel(table.selectionModel());
        // Explicit EnsureVisible must work independently of Qt auto-scrolling
        // currentIndex changes, including after focus was restored offscreen.
        list.setAutoScroll(false);
        list.resize(240, 120);
        panel.AttachPresentation(&list, nullptr);
        list.show();
        QVERIFY(QTest::qWaitForWindowExposed(&list));
        table.setCurrentCell(80, 0);
        list.verticalScrollBar()->setValue(0);
        const QModelIndex next = list.model()->index(81, 0);
        QVERIFY(!list.viewport()->rect().intersects(list.visualRect(next)));
        panel.OnInsert();
        QCOMPARE(table.currentRow(), 81);
        QVERIFY(list.viewport()->rect().contains(list.visualRect(next)));
    }

    void upstreamWildcardRules() {
        QVERIFY(DoesWildcardMatchName(u"", u""));
        QVERIFY(!DoesWildcardMatchName(u"", u"a"));
        QVERIFY(DoesWildcardMatchName(u"***", u""));
        QVERIFY(!DoesWildcardMatchName(u"?", u""));
        QVERIFY(DoesWildcardMatchName(u"?", u"я"));
        QVERIFY(!DoesWildcardMatchName(u"?", u"ab"));
        QVERIFY(DoesWildcardMatchName(u"a*b?c", u"a one b two bxc"));
        QVERIFY(!DoesWildcardMatchName(u"*.txt", u"file.txt.bak"));
        QVERIFY(!DoesWildcardMatchName(u"*.*", u"README"));
        QVERIFY(DoesWildcardMatchName(u"АЛЬФА *.TXT", u"альфа файл.txt"));
        QVERIFY(!DoesWildcardMatchName(u"?", u"\U0001F600"));
        QVERIFY(DoesWildcardMatchName(u"??", u"\U0001F600"));
        QCOMPARE(DoesWildcardMatchName(UString(L"*.gz"), UString(L"b.GZ")), !g_CaseSensitive);
    }

    void sourcePanelCommandsKeepIndexAndFocus() {
        CPanel panel(QStringLiteral("/tmp/"), items());
        QTableWidget view;
        populate(view);
        panel.AttachSelectionView(&view);
        view.selectionModel()->setCurrentIndex(view.model()->index(1, 0),
                                               QItemSelectionModel::NoUpdate);
        panel.SelectAll(true);
        QCOMPARE(view.selectionModel()->selectedRows().size(), 4);
        QVERIFY(!selected(view, 0));
        QCOMPARE(view.currentRow(), 1);
        panel.KillSelection();
        QCOMPARE(view.selectionModel()->selectedRows().size(), 0);
    }

    void explicitParentNameRestoresFocus_data() {
        QTest::addColumn<bool>("alternative");
        QTest::newRow("standard") << false;
        QTest::newRow("alternative") << true;
    }
    void explicitParentNameRestoresFocus() {
        QFETCH(bool, alternative);
        CPanel panel(QStringLiteral("/tmp/"), items());
        QTableWidget view;
        populate(view);
        panel.AttachSelectionView(&view);
        panel.SetAlternativeSelection(alternative);
        panel._showDots = true;
        CSelectedState state;
        state.FocusedName = UString("..");
        state.FocusedName_Defined = true;
        state.FocusedItem = 3;
        state.SelectFocused = true;
        QCOMPARE(panel.RefreshListCtrl(state), S_OK);
        QCOMPARE(view.currentRow(), 0);
        QVERIFY(selected(view, 0));
        CRecordVector<UInt32> indices;
        panel.Get_ItemIndices_Selected(indices);
        QVERIFY(indices.IsEmpty());
        panel.Get_ItemIndices_Operated(indices);
        QVERIFY(indices.IsEmpty());
    }

    void alternativeMarkSourceTransitions() {
        CPanel panel(QStringLiteral("/tmp/"), items());
        QTableWidget view;
        populate(view);
        panel.AttachSelectionView(&view);
        CSelectedState snapshot;
        view.selectionModel()->setCurrentIndex(view.model()->index(1, 0),
                                               QItemSelectionModel::NoUpdate);
        view.selectRow(1);
        panel.SetAlternativeSelection(true);
        QCOMPARE(view.selectionMode(), QAbstractItemView::SingleSelection);
        QVERIFY(panel.IsItemMarked(0));
        QVERIFY(!panel.IsItemMarked(1));

        panel.OnLeftClick(2, Qt::ControlModifier);
        QVERIFY(panel.IsItemMarked(1));
        panel.OnLeftClick(4, Qt::ShiftModifier);
        QVERIFY(!panel.IsItemMarked(0));
        QVERIFY(panel.IsItemMarked(1));
        QVERIFY(panel.IsItemMarked(2));
        QVERIFY(panel.IsItemMarked(3));
        QVERIFY(!selected(view, 0));
        CRecordVector<UInt32> selectedIndices;
        panel.Get_ItemIndices_Selected(selectedIndices);
        QCOMPARE(selectedIndices.Size(), unsigned(3));
        view.selectionModel()->setCurrentIndex(view.model()->index(1, 0),
                                               QItemSelectionModel::NoUpdate);
        panel.SaveSelectedState(snapshot);
        panel.RefreshListCtrl(snapshot);
        QVERIFY(!panel.IsItemMarked(0));
        panel.Get_ItemIndices_Selected(selectedIndices);
        QCOMPARE(selectedIndices.Size(), unsigned(3));

        view.selectionModel()->setCurrentIndex(view.model()->index(3, 0),
                                               QItemSelectionModel::NoUpdate);
        panel.OnInsert();
        QVERIFY(!panel.IsItemMarked(2));
        QCOMPARE(view.currentRow(), 4);
        panel.SelectAll(false);
        panel.BeginShiftSelection();
        panel.OnArrowWithShift();
        view.selectionModel()->setCurrentIndex(view.model()->index(3, 0),
                                               QItemSelectionModel::NoUpdate);
        panel.OnShiftSelectMessage();
        QVERIFY(panel.IsItemMarked(2));

        panel.SetAlternativeSelection(false);
        QCOMPARE(view.selectionMode(), QAbstractItemView::ExtendedSelection);
        QCOMPARE(view.selectionModel()->selectedRows().size(), 2);
        QVERIFY(!selected(view, 0));
    }
};
QTEST_MAIN(PanelSelectionTests)
#include "panel_selection_tests.moc"
