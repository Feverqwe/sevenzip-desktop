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

    void explicitParentNameRestoresFocus() {
        CPanel panel(QStringLiteral("/tmp/"), items());
        QTableWidget view;
        populate(view);
        panel.AttachSelectionView(&view);
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

    void refreshKeepsParentFocusSeparateFromSelection() {
        CPanel panel(QStringLiteral("/tmp/"), items());
        QTableWidget view;
        populate(view);
        panel.AttachSelectionView(&view);
        panel._showDots = true;
        view.selectRow(1);
        view.setCurrentCell(0, 0, QItemSelectionModel::NoUpdate);
        QVERIFY(!selected(view, 0));

        CSelectedState state;
        panel.SaveSelectedState(state);
        QCOMPARE(panel.RefreshListCtrl(state), S_OK);
        QCOMPARE(view.currentRow(), 0);
        QVERIFY(!selected(view, 0));
        QCOMPARE(view.selectionModel()->selectedRows().size(), 1);
        QCOMPARE(view.selectionModel()->selectedRows().first().data().toString(),
                 QString("a.tar.gz"));
    }
};
QTEST_MAIN(PanelSelectionTests)
#include "panel_selection_tests.moc"
