#include "filemanager/support/panel_load_folder.h"
#include "filemanager/support/panel_model_support.h"
#include "filemanager/views/filetable.h"
#include <QTableWidget>
#include <QtTest>

class PanelRefreshNotificationsTests final : public QObject {
    Q_OBJECT
  private slots:
    void focusRequestsSourceName_data() {
        QTest::addColumn<bool>("fast");
        QTest::addColumn<bool>("fallback");
        for (bool fast : {false, true})
            for (bool fallback : {false, true})
                QTest::newRow(qPrintable(QString("%1-%2").arg(fast).arg(fallback)))
                    << fast << fallback;
    }
    void focusRequestsSourceName() {
        QFETCH(bool, fast);
        QFETCH(bool, fallback);
        auto *spec = new LoadFolder;
        CMyComPtr<IFolderFolder> folder = spec;
        spec->fastNames = fast;
        spec->firstName = L"first     文件";
        spec->secondName = L"second δ.txt";
        CPanel panel(folder);
        QCOMPARE(panel.LoadListItems(), S_OK);
        FileTable view(1);
        view.sourcePanel = &panel;
        view.textFolder = folder;
        view.setColumnProperties({kpidName});
        view.setSelectionBehavior(QAbstractItemView::SelectRows);
        for (int row = 0; row < 2; ++row) {
            FilePanelModel::Row item;
            item.sourceIndex = 1 - row;
            view.panelModel()->append(std::move(item));
        }
        panel.AttachSelectionView(&view);
        const int target = fallback ? 1 : 0;
        view.setCurrentCell(1 - target, 0, QItemSelectionModel::NoUpdate);
        // Leave the target uncached, as on the first callback after publication.

        CSelectedState state;
        state.FocusedName = fallback ? UString(L"missing.txt") : spec->secondName;
        state.FocusedName_Defined = true;
        state.FocusedItem = target;
        state.SelectFocused = true;
        state.SelectedNames.Add(spec->secondName);
        int requests = 0;
        bool callbacksEnabled = false;
        QString displayed;
        connect(view.selectionModel(), &QItemSelectionModel::currentChanged, &view,
                [&](const QModelIndex &index) {
                    const int row = index.row();
                    ++requests;
                    callbacksEnabled = panel.ShouldProcessNotifications();
                    displayed = view.model()->index(row, 0).data().toString();
                });
        QCOMPARE(panel.RefreshListCtrl(state), S_OK);
        QCOMPARE(requests, 1);
        QVERIFY(callbacksEnabled);
        QCOMPARE(displayed, fallback ? (fast ? QStringLiteral("first ... 文件")
                                             : QStringLiteral("first     文件"))
                                     : QStringLiteral("second δ.txt"));
        QCOMPARE(view.currentRow(), target);
        QVERIFY(panel.IsItemMarked(1));
        QVERIFY(panel.ShouldProcessNotifications());
    }
    void failedLoadKeepsOuterGuard_data() {
        QTest::addColumn<HRESULT>("result");
        QTest::newRow("failure") << E_FAIL;
        QTest::newRow("cancel") << E_ABORT;
    }
    void failedLoadKeepsOuterGuard() {
        QFETCH(HRESULT, result);
        auto *spec = new LoadFolder;
        CMyComPtr<IFolderFolder> folder = spec;
        CPanel panel(folder);
        QTableWidget view(1, 1);
        view.setItem(0, 0, new QTableWidgetItem("existing row"));
        panel.AttachSelectionView(&view);
        QSignalSpy focus(&view, &QTableWidget::currentCellChanged);
        {
            CPanel::CDisableNotify outer(panel);
            QVERIFY(!panel.ShouldProcessNotifications());
            QCOMPARE(panel.LoadListItems(true, result), result);
            QCOMPARE(panel.RefreshListCtrl(), result);
            QCOMPARE(focus.count(), 0);
            QVERIFY(!panel.ShouldProcessNotifications());
        }
        QVERIFY(panel.ShouldProcessNotifications());
    }
};

QTEST_MAIN(PanelRefreshNotificationsTests)
#include "panel_refresh_notifications_tests.moc"
