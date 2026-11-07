#include "filemanager/support/panel_load_folder.h"
#include <QTableWidget>
#include <QtTest>

class PanelRefreshNamesTests final : public QObject {
    Q_OBJECT
  private slots:
    void unselectedFocusDoesNotBecomeOperated() {
        auto *spec = new LoadFolder;
        CMyComPtr<IFolderFolder> folder = spec;
        CPanel panel(folder);
        QCOMPARE(panel.LoadListItems(), S_OK);
        QTableWidget view(2, 1);
        view.setSelectionBehavior(QAbstractItemView::SelectRows);
        for (int row = 0; row < 2; ++row) {
            auto *item = new QTableWidgetItem;
            item->setData(Qt::UserRole + 6, row);
            view.setItem(row, 0, item);
        }
        panel.AttachSelectionView(&view);
        view.setCurrentCell(1, 0, QItemSelectionModel::NoUpdate);
        view.clearSelection();
        CSelectedState state;
        panel.SaveSelectedState(state);
        QVERIFY(!state.SelectFocused);
        QVERIFY(state.SelectedNames.IsEmpty());
        QCOMPARE(panel.RefreshListCtrl(state), S_OK);
        QCOMPARE(view.currentRow(), 1);
        QCOMPARE(view.selectionModel()->selectedRows().size(), 0);
        CRecordVector<UInt32> operated;
        panel.Get_ItemIndices_Operated(operated);
        QVERIFY(operated.IsEmpty());
    }
    void restoredNames_data() {
        QTest::addColumn<bool>("fast");
        QTest::addColumn<bool>("flat");
        QTest::addColumn<bool>("multiple");
        for (bool fast : {false, true})
            for (bool flat : {false, true})
                for (bool multiple : {false, true})
                    QTest::newRow(qPrintable(QString("%1-%2-%3").arg(fast).arg(flat).arg(multiple)))
                        << fast << flat << multiple;
    }
    void restoredNames() {
        QFETCH(bool, fast);
        QFETCH(bool, flat);
        QFETCH(bool, multiple);
        auto *spec = new LoadFolder;
        CMyComPtr<IFolderFolder> folder = spec;
        spec->fastNames = fast;
        spec->prefix = L"nested 文件/";
        CPanel panel(folder);
        if (flat)
            panel.ChangeFlatMode();
        QCOMPARE(panel.LoadListItems(), S_OK);
        QTableWidget view(2, 1);
        view.setSelectionBehavior(QAbstractItemView::SelectRows);
        view.setSelectionMode(QAbstractItemView::ExtendedSelection);
        for (int row = 0; row < 2; ++row) {
            auto *item = new QTableWidgetItem("formatted display");
            item->setData(Qt::UserRole + 6, 1 - row);
            view.setItem(row, 0, item);
        }
        panel.AttachSelectionView(&view);
        spec->nameResult = fast ? E_ABORT : S_OK;
        spec->prefixResult = fast || !flat ? E_ABORT : S_OK;
        CSelectedState state;
        state.FocusedName = (flat ? spec->prefix : UString()) + spec->firstName;
        state.FocusedName_Defined = true;
        state.FocusedItem = 0;
        state.SelectFocused = true;
        state.SelectedNames.Add(state.FocusedName);
        if (multiple)
            state.SelectedNames.Add((flat ? spec->prefix : UString()) + spec->secondName);
        state.SelectedNames.Sort();
        HRESULT result = E_FAIL;
        int exception = 0;
        try {
            result = panel.RefreshListCtrl(state);
        } catch (int value) {
            exception = value;
        }
        QCOMPARE(exception, 0);
        QCOMPARE(result, S_OK);
        QCOMPARE(view.currentRow(), 1);
        QVERIFY(panel.IsItemMarked(0));
        QCOMPARE(panel.IsItemMarked(1), multiple);
        QCOMPARE(view.selectionModel()->selectedRows().size(), multiple ? 2 : 1);
        if (fast)
            QCOMPARE(spec->namePropertyCalls, 0u);
        if (fast || !flat)
            QCOMPARE(spec->prefixPropertyCalls, 0u);
    }
    void nullFastPointersUseProperties_data() {
        QTest::addColumn<bool>("nameDefined");
        QTest::addColumn<bool>("prefixDefined");
        for (bool nameDefined : {false, true})
            for (bool prefixDefined : {false, true})
                QTest::newRow(qPrintable(QString("%1-%2").arg(nameDefined).arg(prefixDefined)))
                    << nameDefined << prefixDefined;
    }
    void nullFastPointersUseProperties() {
        QFETCH(bool, nameDefined);
        QFETCH(bool, prefixDefined);
        auto *spec = new LoadFolder;
        CMyComPtr<IFolderFolder> folder = spec;
        spec->fastNames = true;
        spec->fastNameDefined = nameDefined;
        spec->fastPrefixDefined = prefixDefined;
        spec->prefix = L"nested 文件/";
        CPanel panel(folder);
        panel.ChangeFlatMode();
        QCOMPARE(panel.LoadListItems(), S_OK);
        QTableWidget view(2, 1);
        view.setSelectionBehavior(QAbstractItemView::SelectRows);
        for (int row = 0; row < 2; ++row) {
            auto *item = new QTableWidgetItem("display");
            item->setData(Qt::UserRole + 6, row);
            view.setItem(row, 0, item);
        }
        panel.AttachSelectionView(&view);
        spec->nameResult = nameDefined ? E_ABORT : S_OK;
        spec->prefixResult = prefixDefined ? E_ABORT : S_OK;
        CSelectedState state;
        state.FocusedName = spec->prefix + spec->secondName;
        state.FocusedName_Defined = true;
        state.FocusedItem = 0;
        state.SelectFocused = true;
        state.SelectedNames.Add(state.FocusedName);
        HRESULT result = E_FAIL;
        int exception = 0;
        try {
            result = panel.RefreshListCtrl(state);
        } catch (int value) {
            exception = value;
        }
        QCOMPARE(exception, 0);
        QCOMPARE(result, S_OK);
        QCOMPARE(view.currentRow(), 1);
        QCOMPARE(view.selectionModel()->selectedRows().size(), 1);
        if (nameDefined)
            QCOMPARE(spec->namePropertyCalls, 0u);
        if (prefixDefined)
            QCOMPARE(spec->prefixPropertyCalls, 0u);
    }
};

QTEST_MAIN(PanelRefreshNamesTests)
#include "panel_refresh_names_tests.moc"
