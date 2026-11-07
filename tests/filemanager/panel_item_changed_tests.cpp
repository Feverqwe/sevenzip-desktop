#include "filemanager/support/panel_load_folder.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "support/panel_keys.h"
#include "ui/support/layout_inspection.h"
#include <QTableWidget>

class PanelItemChangedTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    static void marks(CPanel &panel, unsigned count) {
        CRecordVector<UInt32> indices;
        panel.Get_ItemIndices_Selected(indices);
        QCOMPARE(indices.Size(), count);
    }
    static FileTable *view(MainWindow &window, unsigned pane) {
        return window.findChild<QWidget *>(QString("filePanel%1").arg(pane))
            ->findChild<FileTable *>("archiveTable");
    }
    static QLineEdit *path(MainWindow &window, unsigned pane) {
        return window.findChild<QWidget *>(QString("filePanel%1").arg(pane))
            ->findChild<QLineEdit *>("archivePath");
    }
  private slots:
    void sourceIndicesAndBindingLifetime() {
        auto *spec = new LoadFolder;
        CMyComPtr<IFolderFolder> provider = spec;
        CPanel panel(provider);
        QTableWidget oldView(3, 2), newView(3, 2);
        for (QTableWidget *list : {&oldView, &newView}) {
            list->setSelectionBehavior(QAbstractItemView::SelectRows);
            list->setSelectionMode(QAbstractItemView::ExtendedSelection);
            for (int row = 0; row < 3; ++row) {
                auto *item = new QTableWidgetItem(row ? "file" : "..");
                item->setData(Qt::UserRole + 6, row == 0 ? UInt32(-1) : UInt32(2 - row));
                list->setItem(row, 0, item);
                list->setItem(row, 1, new QTableWidgetItem);
            }
        }
        panel.AttachSelectionView(&oldView);
        oldView.selectRow(0);
        marks(panel, 0);
        oldView.selectRow(1);
        CRecordVector<UInt32> indices;
        panel.Get_ItemIndices_Selected(indices);
        QCOMPARE(indices.Size(), 1u);
        QCOMPARE(indices[0], UInt32(1));
        oldView.selectRow(1);
        marks(panel, 1);
        spec->countResult = E_FAIL;
        CSelectedState saved;
        panel.SaveSelectedState(saved);
        QCOMPARE(saved.SelectedNames.Size(), 1u);
        spec->countResult = S_OK;
        panel.AttachSelectionView(&newView);
        marks(panel, 0);
        oldView.clearSelection();
        oldView.selectAll();
        marks(panel, 0);
        newView.selectRow(2);
        panel.Get_ItemIndices_Selected(indices);
        QCOMPARE(indices.Size(), 1u);
        QCOMPARE(indices[0], UInt32(0));
        QSignalSpy deletion(newView.selectionModel(), &QItemSelectionModel::selectionChanged);
        const auto observe =
            connect(newView.selectionModel(), &QItemSelectionModel::selectionChanged, &panel,
                    [&] { marks(panel, 1); });
        panel.ReleaseFolder();
        disconnect(observe);
        QVERIFY(!deletion.isEmpty());
        QCOMPARE(newView.rowCount(), 0);
        marks(panel, 0);
        oldView.clearSelection();
        marks(panel, 0);
    }
    void owningPaneNotifications_data() {
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<bool>("russian");
        for (unsigned pane : {0u, 1u})
            for (bool ru : {false, true})
                QTest::newRow(qPrintable(QString("%1-%2").arg(pane).arg(ru))) << pane << ru;
    }
    void owningPaneNotifications() {
        QFETCH(unsigned, pane);
        QFETCH(bool, russian);
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settings);
        SaveRegLang(UString(russian ? L"ru" : L"-"), settings);
        ReloadLang(settings);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QStringList names{QStringLiteral("first 文件.txt"), QStringLiteral("second δ.txt")};
        for (const QString &name : names) {
            QFile file(fixture.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write("untouched"), qint64(9));
        }
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        action(window, "twoPanelsAction")->trigger();
        for (unsigned owner : {0u, 1u}) {
            PanelKeys::focusAddress(path(window, owner));
            path(window, owner)->setText(fixture.path());
            QTest::keyClick(path(window, owner), Qt::Key_Return);
            QTRY_COMPARE(view(window, owner)->rowCount(), 2);
            static_cast<FileTable *>(view(window, owner))->sourcePanel->Set_AutoRefresh_Mode(false);
        }
        auto *list = view(window, pane);
        auto &panel = *static_cast<FileTable *>(list)->sourcePanel;
        auto *peer = view(window, 1 - pane);
        PanelKeys::focusAddress(path(window, 1 - pane));
        QWidget *const focused = window.focusWidget();
        const auto peerSelection = peer->selectionModel()->selection();
        list->clearSelection();
        list->selectAll();
        marks(panel, 2);
        CSelectedState saved;
        {
            CPanel::CDisableNotify guard(panel);
            list->clearSelection();
            panel.SaveSelectedState(saved);
            marks(panel, 2);
            QCOMPARE(saved.SelectedNames.Size(), 2u);
        }
        panel.RefreshListCtrl(saved);
        QCOMPARE(list->selectionModel()->selectedRows().size(), 2);
        list->clearSelection();
        marks(panel, 0);
        list->selectRow(1);
        marks(panel, 1);
        list->clearSelection();
        marks(panel, 0);
        list->selectAll();
        panel.SaveSelectedState(saved);
        panel.OnReload();
        marks(panel, 2);
        QCOMPARE(window.focusWidget(), focused);
        QCOMPARE(peer->selectionModel()->selection(), peerSelection);
        PanelKeys::focusAddress(path(window, pane));
        list->clearSelection();
        list->setCurrentCell(0, 0);
        list->selectRow(0);
        QTRY_VERIFY(action(window, "renameAction")->isEnabled());
        list->selectAll();
        QVERIFY(action(window, "renameAction")->isEnabled());
        marks(panel, 2);
        PanelKeys::focusAddress(path(window, 1 - pane));
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(size);
            QApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(*list, window));
            QVERIFY(UiInspection::fullyVisible(*peer, window));
            QVERIFY(UiInspection::writeReport(
                window,
                QString("item-changed-%1-%2").arg(QTest::currentDataTag()).arg(size.width())));
        }
        for (const QString &name : names) {
            QFile file(fixture.filePath(name));
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), QByteArray("untouched"));
        }
        QCOMPARE(backend.listCalls, 0);
    }
};
QTEST_MAIN(PanelItemChangedTests)
#include "panel_item_changed_tests.moc"
