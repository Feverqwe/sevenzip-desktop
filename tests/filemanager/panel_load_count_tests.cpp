#include "filemanager/support/panel_load_folder.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "support/panel_keys.h"
#include "ui/support/layout_inspection.h"
#include <QTableWidget>

class PanelLoadCountTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    static FileTable &view(MainWindow &window, unsigned pane) {
        return *window.findChild<QWidget *>(QString("filePanel%1").arg(pane))
                    ->findChild<FileTable *>("archiveTable");
    }
    static QLineEdit &address(MainWindow &window, unsigned pane) {
        return *window.findChild<QWidget *>(QString("filePanel%1").arg(pane))
                    ->findChild<QLineEdit *>("archivePath");
    }
    static void report(MainWindow &window, const QString &phase) {
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(size);
            QApplication::processEvents();
            for (unsigned pane : {0u, 1u}) {
                QWidget *control = &view(window, pane);
                if (!control->isVisible())
                    control = window.findChild<QWidget *>(QString("filePanel%1").arg(pane))
                                  ->findChild<QListView *>("iconList");
                QVERIFY(control);
                QVERIFY(UiInspection::fullyVisible(*control, window));
                QVERIFY(UiInspection::fullyVisible(
                    *PanelKeys::navigationFor(&address(window, pane)), window));
            }
            QVERIFY(UiInspection::writeReport(window, QString("load-count-%1-%2-%3")
                                                          .arg(QTest::currentDataTag(), phase)
                                                          .arg(size.width())));
        }
    }
  private slots:
    void loadedCountLifetime() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        for (HRESULT status : {S_FALSE, E_FAIL, E_ABORT})
            for (int count : {-1, 0, 2}) {
                auto *spec = new LoadFolder;
                CMyComPtr<IFolderFolder> provider = spec;
                CPanel panel(provider);
                QTableWidget table;
                table.setColumnCount(1);
                table.setSelectionBehavior(QAbstractItemView::SelectRows);
                panel.AttachSelectionView(&table);
                panel.AttachFolderManager(nullptr, fixture.filePath("settings.ini"));
                spec->countResult = status;
                spec->itemCount = count < 0 ? std::nullopt : std::optional<UInt32>(UInt32(count));
                const unsigned calls = spec->countCalls;
                QCOMPARE(panel.LoadListItems(true), S_OK);
                const UInt32 expected = count < 0 ? 0u : UInt32(count);
                QCOMPARE(panel.GetLoadedItemCount(), expected);
                QCOMPARE(spec->countCalls, calls + 1);
                table.setRowCount(int(expected));
                for (UInt32 row = 0; row < expected; ++row) {
                    auto *item = new QTableWidgetItem;
                    item->setData(Qt::UserRole + 6, row);
                    table.setItem(int(row), 0, item);
                }
                if (expected)
                    table.selectRow(0);
                CSelectedState selection;
                panel.SaveSelectedState(selection);
                spec->itemCount.reset();
                QCOMPARE(panel.RefreshListCtrl(selection), S_OK);
                QCOMPARE(spec->countCalls, calls + 1);
                QCOMPARE(table.rowCount(), int(expected));
                panel.ReleaseFolder();
                QCOMPARE(panel.GetLoadedItemCount(), 0u);
                QCOMPARE(table.rowCount(), 0);
                panel.SetNewFolder(provider);
                QCOMPARE(panel.GetLoadedItemCount(), 0u);
                spec->countResult = S_OK;
                spec->itemCount = 2;
                QCOMPARE(panel.LoadListItems(true), S_OK);
                QCOMPARE(panel.GetLoadedItemCount(), 2u);
            }
    }
    void failedLoadLeavesEmptyMarks_data() {
        QTest::addColumn<qint32>("result");
        QTest::newRow("failure") << qint32(E_FAIL);
        QTest::newRow("cancel") << qint32(E_ABORT);
    }
    void failedLoadLeavesEmptyMarks() {
        QFETCH(qint32, result);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        auto *spec = new LoadFolder;
        CMyComPtr<IFolderFolder> provider = spec;
        CPanel panel(provider);
        QTableWidget table;
        table.setColumnCount(1);
        panel.AttachSelectionView(&table);
        panel.AttachFolderManager(nullptr, fixture.filePath("settings.ini"));
        QCOMPARE(panel.LoadListItems(), S_OK);
        table.setRowCount(2);
        for (int row = 0; row < 2; ++row) {
            auto *item = new QTableWidgetItem;
            item->setData(Qt::UserRole + 6, UInt32(row));
            table.setItem(row, 0, item);
        }
        spec->loadResult = result;
        spec->loadedCountResult = E_FAIL;
        const unsigned calls = spec->countCalls;
        QCOMPARE(panel.LoadListItems(), HRESULT(result));
        QCOMPARE(table.rowCount(), 0);
        QCOMPARE(panel.GetLoadedItemCount(), 0u);
        try {
            panel.SelectAll(true);
        } catch (...) {
            QFAIL("Selecting an empty list queried metadata after the source load early return");
        }
        QCOMPARE(spec->countCalls, calls);
        CRecordVector<UInt32> marks;
        panel.Get_ItemIndices_Selected(marks);
        QVERIFY(marks.IsEmpty());
        CSelectedState state;
        QCOMPARE(panel.RefreshListCtrl(state), HRESULT(result));
        spec->loadResult = S_OK;
        QCOMPARE(panel.LoadListItems(), S_OK);
        QCOMPARE(panel.GetLoadedItemCount(), 2u);
        QCOMPARE(spec->countCalls, calls + 1);
    }
    void writtenCountSurvivesReload_data() {
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<bool>("russian");
        QTest::addColumn<int>("mode");
        QTest::addColumn<qint32>("status");
        QTest::addColumn<int>("count");
        for (unsigned pane : {0u, 1u})
            for (bool ru : {false, true})
                for (int mode : {0, 3})
                    for (HRESULT status : {S_FALSE, E_FAIL, E_ABORT})
                        for (int count : {-1, 0, 2})
                            QTest::newRow(qPrintable(QString("%1-%2-%3-%4-%5")
                                                         .arg(pane)
                                                         .arg(ru)
                                                         .arg(mode)
                                                         .arg(quint32(status))
                                                         .arg(count)))
                                << pane << ru << mode << qint32(status) << count;
    }
    void writtenCountSurvivesReload() {
        QFETCH(unsigned, pane);
        QFETCH(bool, russian);
        QFETCH(int, mode);
        QFETCH(qint32, status);
        QFETCH(int, count);
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settings);
        SaveRegLang(UString(russian ? L"ru" : L"-"), settings);
        ReloadLang(settings);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString payload = fixture.filePath(QStringLiteral("control 文件 δ.txt"));
        QFile file(payload);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("untouched"), qint64(9));
        file.close();
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        action(window, "twoPanelsAction")->trigger();
        PanelKeys::focusAddress(&address(window, 1 - pane));
        address(window, 1 - pane).setText(fixture.path());
        QTest::keyClick(&address(window, 1 - pane), Qt::Key_Return);
        QTRY_COMPARE(view(window, 1 - pane).rowCount(), 1);
        auto &peer = *static_cast<FileTable &>(view(window, 1 - pane)).sourcePanel;
        peer.Set_AutoRefresh_Mode(false);
        CMyComPtr<IFolderFolder> peerProvider = peer.GetFolder();
        view(window, 1 - pane).selectRow(0);
        const auto peerSelection = view(window, 1 - pane).selectionModel()->selection();
        PanelKeys::focusAddress(&address(window, pane));
        action(window, "openRootAction")->trigger();
        auto *spec = new LoadFolder;
        CMyComPtr<IFolderFolder> provider = spec;
        auto &panel = *static_cast<FileTable &>(view(window, pane)).sourcePanel;
        panel.Set_AutoRefresh_Mode(false);
        panel.SetNewFolder(provider);
        panel.OnReload();
        QCOMPARE(view(window, pane).rowCount(), 2);
        action(window, qPrintable(QString("viewMode%1Action").arg(mode)))->trigger();
        view(window, pane).selectRow(1);
        CSelectedState before;
        panel.SaveSelectedState(before);
        QVERIFY(!before.SelectedNames.IsEmpty());
        QStringList errors;
        QTimer responder;
        connect(&responder, &QTimer::timeout, &window, [&] {
            for (QWidget *widget : QApplication::topLevelWidgets())
                if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible()) {
                    errors << box->text();
                    box->accept();
                }
        });
        responder.start(0);
        spec->loadedCountResult = status;
        spec->itemCount = count < 0 ? std::nullopt : std::optional<UInt32>(UInt32(count));
        action(window, "refreshAction")->trigger();
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
        const int expected = count < 0 ? 0 : count;
        QCOMPARE(view(window, pane).rowCount(), expected);
        QCOMPARE(panel.GetFolder(), (IFolderFolder *)provider);
        QVERIFY(panel.ShouldProcessNotifications());
        QVERIFY(view(window, pane).isEnabled() && view(window, pane).updatesEnabled());
        CSelectedState after;
        panel.SaveSelectedState(after);
        QCOMPARE(after.SelectedNames.Size(), expected ? before.SelectedNames.Size() : 0u);
        report(window, "refresh");
        panel.Set_AutoRefresh_Mode(true);
        spec->changed = true;
        panel.OnTimer();
        panel.Set_AutoRefresh_Mode(false);
        QVERIFY(errors.isEmpty());
        QCOMPARE(view(window, pane).rowCount(), expected);
        report(window, "timer");
        spec->itemCount = 2;
        spec->loadedCountResult = S_OK;
        action(window, "refreshAction")->trigger();
        QCOMPARE(view(window, pane).rowCount(), 2);
        QVERIFY(errors.isEmpty());
        // The native numItems local survives insertion, restore and mark paint.
        // A later metadata result cannot resize the published source indices.
        spec->countResult = status;
        spec->itemCount.reset();
        HRESULT restored = E_FAIL;
        try {
            restored = panel.RestoreLoadedSelection(after);
        } catch (...) {
            QFAIL("Restoration/selection queried a later count instead of the loaded count");
        }
        QCOMPARE(restored, S_OK);
        QCOMPARE(view(window, pane).rowCount(), 2);
        report(window, "recovery");
        responder.stop();
        QCOMPARE(peer.GetFolder(), (IFolderFolder *)peerProvider);
        QCOMPARE(view(window, 1 - pane).rowCount(), 1);
        QCOMPARE(view(window, 1 - pane).selectionModel()->selection(), peerSelection);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("untouched"));
        QCOMPARE(backend.listCalls, 0);
    }
};
QTEST_MAIN(PanelLoadCountTests)
#include "panel_load_count_tests.moc"
