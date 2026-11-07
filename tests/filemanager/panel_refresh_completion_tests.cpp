#include "filemanager/providers/FSFolder.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "filemanager/window/qtnavigationbar.h"
#include "localization/LangUtils.h"
#include "ui/formatting/qtregionalformat.h"
#include "ui/support/layout_inspection.h"
#include <QBrush>
#include <QHeaderView>
#include <QLabel>
#include <QToolButton>

class PanelRefreshCompletionTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    static void focusPane(MainWindow &window, unsigned pane) {
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *widget = window.findChild<QWidget *>(QString("filePanel%1").arg(pane));
        auto *navigation = static_cast<QtNavigationBar *>(
            widget->findChild<QWidget *>(QString("panelNavigation%1").arg(pane)));
        navigation->Edit();
        QApplication::processEvents();
        QTRY_VERIFY2(widget->isAncestorOf(QApplication::focusWidget()),
                     qPrintable(QString("pane=%1 visible=%2 twoPanels=%3 focus=%4")
                                    .arg(pane)
                                    .arg(widget->isVisible())
                                    .arg(action(window, "twoPanelsAction")->isChecked())
                                    .arg(QApplication::focusWidget()
                                             ? QApplication::focusWidget()->objectName()
                                             : QStringLiteral("none"))));
    }
    static FileTable *openPane(MainWindow &window, ControlledBackend &backend, unsigned pane,
                               const QString &path, const QList<ArchiveEntry> &entries) {
        if (!action(window, "twoPanelsAction")->isChecked())
            action(window, "twoPanelsAction")->trigger();
        auto *widget = window.findChild<QWidget *>(QString("filePanel%1").arg(pane));
        focusPane(window, pane);
        openListedArchive(window, backend, path, entries);
        return static_cast<FileTable *>(widget->findChild<FileTable *>("archiveTable"));
    }
    static void language(bool russian) {
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        SaveRegLang(UString(russian ? L"ru" : L"-"),
                    qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
    }
  private slots:
    void refreshResetsAlternativeGroupAnchor_data() {
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<bool>("russian");
        QTest::addColumn<bool>("icons");
        QTest::addColumn<bool>("showDots");
        QTest::addColumn<QString>("caller");
        for (unsigned pane : {0u, 1u})
            for (bool russian : {false, true})
                for (bool icons : {false, true})
                    for (bool showDots : {false, true})
                        for (const char *caller : {"restore", "refresh", "timer"})
                            QTest::newRow(qPrintable(QString("%1-%2-%3-%4-%5")
                                                         .arg(pane)
                                                         .arg(russian)
                                                         .arg(icons)
                                                         .arg(showDots)
                                                         .arg(caller)))
                                << pane << russian << icons << showDots
                                << QString::fromLatin1(caller);
    }
    void refreshResetsAlternativeGroupAnchor() {
        QFETCH(unsigned, pane);
        QFETCH(bool, russian);
        QFETCH(bool, icons);
        QFETCH(bool, showDots);
        QFETCH(QString, caller);
        language(russian);
        QSettings settings(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"), QSettings::IniFormat);
        settings.setValue("FM/AlternativeSelection", true);
        settings.setValue("FM/ShowDots", showDots);
        settings.sync();
        QTemporaryDir root;
        QVERIFY(root.isValid());
        for (int i = 0; i < 7; ++i) {
            QFile file(root.filePath(QString("file%1 δ.txt").arg(i)));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(QByteArray(i + 1, 'x')), qint64(i + 1));
        }
        ControlledBackend backend;
        MainWindow window(&backend);
        window.resize(980, 620);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        action(window, "twoPanelsAction")->trigger();
        auto *widget = window.findChild<QWidget *>(QString("filePanel%1").arg(pane));
        auto *peer = window.findChild<QWidget *>(QString("filePanel%1").arg(1 - pane));
        auto *peerTable = static_cast<FileTable *>(peer->findChild<FileTable *>("archiveTable"));
        CMyComPtr<IFolderFolder> peerProvider = peerTable->sourcePanel->GetFolder();
        const UString peerPrefix = peerTable->sourcePanel->GetCurrentFolderPrefix();
        auto *address = widget->findChild<QLineEdit *>("archivePath");
        focusPane(window, pane);
        QVERIFY(widget->findChild<QComboBox *>("pathCombo")->hasFocus() || address->hasFocus());
        address->setText(root.path());
        QTest::keyClick(address, Qt::Key_Return);
        auto *table = static_cast<FileTable *>(widget->findChild<FileTable *>("archiveTable"));
        QTRY_COMPARE(table->rowCount(), showDots ? 8 : 7);
        action(window, icons ? "viewMode0Action" : "viewMode3Action")->trigger();
        QAbstractItemView *view =
            icons ? static_cast<QAbstractItemView *>(widget->findChild<QListView *>("iconList"))
                  : table;
        QVERIFY(view && view->isVisible());
        QCOMPARE(view->selectionMode(), QAbstractItemView::SingleSelection);
        auto click = [&](int row, Qt::KeyboardModifiers modifiers) {
            const QModelIndex index = table->model()->index(row, 0);
            view->scrollTo(index);
            QTest::mouseClick(view->viewport(), Qt::LeftButton, modifiers,
                              view->visualRect(index).center());
        };
        click(3, Qt::ControlModifier);
        QCOMPARE(table->currentRow(), 3);
        auto &panel = *table->sourcePanel;
        QVERIFY(panel.IsItemMarked(table->model()->index(3, 0).data(Qt::UserRole + 6).toUInt()));
        if (caller == "restore") {
            CSelectedState state;
            panel.SaveSelectedState(state);
            QCOMPARE(panel.RefreshListCtrl(state), S_OK);
        } else if (caller == "timer") {
            panel.Set_AutoRefresh_Mode(true);
            panel.MarkPortableFolderChanged();
            panel.OnTimer();
        } else
            action(window, "refreshAction")->trigger();
        QCOMPARE(table->currentRow(), 3);
        click(4, Qt::ShiftModifier);
        CRecordVector<UInt32> indices;
        panel.Get_ItemIndices_Selected(indices);
        QCOMPARE(indices.Size(), showDots ? 4u : 5u);
        for (unsigned i = 0; i < unsigned(table->rowCount()); ++i) {
            const UInt32 source = table->model()->index(int(i), 0).data(Qt::UserRole + 6).toUInt();
            const bool marked = i <= 4 && source != UInt32(-1);
            QVERIFY(panel.IsItemMarked(source) == marked);
            QCOMPARE(table->model()->index(int(i), 0).data(Qt::BackgroundRole).isValid(), marked);
        }
        QCOMPARE(table->currentRow(), 4);
        QCOMPARE(table->selectionModel()->selectedRows().size(), 1);
        QCOMPARE(window.findChild<QLabel *>("selectedSizeStatus")->text(),
                 QtRegionalFormat::bytes(showDots ? 10 : 15));
        QCOMPARE(peerTable->sourcePanel->GetFolder(), peerProvider.Interface());
        QCOMPARE(peerTable->sourcePanel->GetCurrentFolderPrefix(), peerPrefix);
        for (int i = 0; i < 7; ++i) {
            QFile file(root.filePath(QString("file%1 δ.txt").arg(i)));
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), QByteArray(i + 1, 'x'));
        }
        for (const QSize geometry : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(geometry);
            QApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(*view, window));
            QVERIFY(UiInspection::writeReport(window, QString("source-refresh-group-%1-%2")
                                                          .arg(QTest::currentDataTag())
                                                          .arg(geometry.width())));
        }
    }
    void failedLoadDoesNotPublishCompletion_data() {
        QTest::addColumn<HRESULT>("result");
        QTest::newRow("failure") << E_FAIL;
        QTest::newRow("cancel") << E_ABORT;
    }
    void failedLoadDoesNotPublishCompletion() {
        QFETCH(HRESULT, result);
        QTemporaryDir fixture;
        CPanel panel(new NFsFolder::CFSFolder(fixture.path()));
        QCOMPARE(panel.LoadListItems(true, result), result);
        int publications = 0;
        panel.AttachStatusPresentation([&](const CPanelStatus &) { ++publications; });
        QCOMPARE(panel.RefreshListCtrl(), result);
        QCOMPARE(publications, 0);
    }
    void sourceSortPrecedesFallback_data() {
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<bool>("russian");
        QTest::addColumn<bool>("ascending");
        QTest::addColumn<bool>("matched");
        for (unsigned pane : {0u, 1u})
            for (bool russian : {false, true})
                for (bool ascending : {false, true})
                    for (bool matched : {false, true})
                        QTest::newRow(qPrintable(QString("%1-%2-%3-%4")
                                                     .arg(pane)
                                                     .arg(russian)
                                                     .arg(ascending)
                                                     .arg(matched)))
                            << pane << russian << ascending << matched;
    }
    void sourceSortPrecedesFallback() {
        QFETCH(unsigned, pane);
        QFETCH(bool, russian);
        QFETCH(bool, ascending);
        QFETCH(bool, matched);
        language(russian);
        QTemporaryDir fixture;
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        const QList<ArchiveEntry> entries = {
            {"file0 δ.txt", 3, 3}, {"file1 文件.txt", 1, 1}, {"file2 space.txt", 2, 2}};
        auto *view = openPane(window, backend, pane, fixture.filePath("sort space.zip"), entries);
        action(window, "sort3Action")->trigger();
        if (ascending)
            action(window, "sort3Action")->trigger();
        // Reproduce the unsorted, complete publication that precedes restoration.
        view->setSortingEnabled(false);
        view->panelModel()->sort(6, Qt::AscendingOrder);
        CSelectedState state;
        state.FocusedName_Defined = true;
        state.FocusedName = ToUString(matched ? entries[1].path : QStringLiteral("missing δ.txt"));
        state.FocusedItem = 1;
        state.SelectFocused = true;
        state.SelectedNames.Add(ToUString(entries[1].path));
        QCOMPARE(view->sourcePanel->RefreshListCtrl(state), S_OK);
        const QList<int> expected = ascending ? QList<int>{1, 2, 0} : QList<int>{0, 2, 1};
        for (int row = 0; row < 3; ++row)
            QCOMPARE(view->model()->index(row, 0).data(Qt::UserRole + 6).toInt(), expected[row]);
        QCOMPARE(view->currentRow(), matched ? (ascending ? 0 : 2) : 1);
        QCOMPARE(view->model()->index(view->currentRow(), 0).data(Qt::UserRole + 3).toString(),
                 entries[matched ? 1 : 2].path);
        CRecordVector<UInt32> selected;
        view->sourcePanel->Get_ItemIndices_Selected(selected);
        QCOMPARE(selected.Size(), 1u);
        QCOMPARE(selected[0], 1u);
        QVERIFY(view->sourcePanel->ShouldProcessNotifications());
        const QString focused =
            view->model()->index(view->currentRow(), 0).data(Qt::UserRole + 3).toString();
        backend.queueList(entries);
        action(window, "refreshAction")->trigger();
        for (int row = 0; row < 3; ++row)
            QCOMPARE(view->model()->index(row, 0).data(Qt::UserRole + 6).toInt(), expected[row]);
        QCOMPARE(view->model()->index(view->currentRow(), 0).data(Qt::UserRole + 3).toString(),
                 focused);
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(size);
            QApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(*view, window));
            QVERIFY(UiInspection::writeReport(window, QString("source-refresh-sort-%1-%2")
                                                          .arg(QTest::currentDataTag())
                                                          .arg(size.width())));
        }
    }
    void sourceCompletionPublishesStatus_data() {
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<bool>("russian");
        QTest::addColumn<bool>("inactive");
        QTest::addColumn<bool>("alternative");
        for (unsigned pane : {0u, 1u})
            for (bool russian : {false, true})
                for (bool inactive : {false, true})
                    for (bool alternative : {false, true})
                        QTest::newRow(qPrintable(QString("%1-%2-%3-%4")
                                                     .arg(pane)
                                                     .arg(russian)
                                                     .arg(inactive)
                                                     .arg(alternative)))
                            << pane << russian << inactive << alternative;
    }
    void sourceCompletionPublishesStatus() {
        QFETCH(unsigned, pane);
        QFETCH(bool, russian);
        QFETCH(bool, inactive);
        QFETCH(bool, alternative);
        language(russian);
        QTemporaryDir fixture;
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        const QList<ArchiveEntry> entries = {
            {"file0 δ.txt", 1, 1}, {"file1 文件.txt", 2, 2}, {"file2 space.txt", 3, 3}};
        auto *view = openPane(window, backend, pane, fixture.filePath("status space.zip"), entries);
        view->sourcePanel->SetAlternativeSelection(alternative);
        view->setCurrentCell(2, 0);
        action(window, "selectAllAction")->trigger();
        auto *size = window.findChild<QLabel *>("selectedSizeStatus");
        QCOMPARE(size->text(), QtRegionalFormat::bytes(6));
        if (inactive) {
            focusPane(window, 1 - pane);
            QApplication::processEvents();
        }
        QWidget *focused = QApplication::focusWidget();
        QStringList peerStatus;
        const char *labels[] = {"selectedItemsStatus", "selectedSizeStatus"};
        for (const char *label : labels)
            peerStatus << window.findChild<QLabel *>(label)->text();
        CSelectedState state;
        state.FocusedName_Defined = true;
        state.FocusedName = ToUString(entries[2].path);
        state.FocusedItem = 2;
        state.SelectFocused = false;
        state.SelectedNames.Add(ToUString(entries[1].path));
        QCOMPARE(view->sourcePanel->RefreshListCtrl(state), S_OK);
        QCOMPARE(view->currentRow(), 2);
        QVERIFY(!view->selectionModel()->isRowSelected(2, QModelIndex()));
        QCOMPARE(view->selectionModel()->selectedRows().size(), alternative ? 0 : 1);
        CSelectedState roundTrip;
        view->sourcePanel->SaveSelectedState(roundTrip);
        QVERIFY(roundTrip.FocusedName_Defined);
        QCOMPARE(roundTrip.FocusedName, state.FocusedName);
        QVERIFY(!roundTrip.SelectFocused);
        QCOMPARE(roundTrip.SelectedNames.Size(), 1u);
        QCOMPARE(roundTrip.SelectedNames[0], state.SelectedNames[0]);
        QCOMPARE(QApplication::focusWidget(), focused);
        if (inactive) {
            for (int part = 0; part < 2; ++part)
                QCOMPARE(window.findChild<QLabel *>(labels[part])->text(), peerStatus[part]);
            focusPane(window, pane);
        }
        QTRY_COMPARE(size->text(), QtRegionalFormat::bytes(2));
        QVERIFY(window.findChild<QLabel *>("selectedItemsStatus")->text().contains("1 / 3"));
        for (const QSize geometry : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(geometry);
            QApplication::processEvents();
            QVERIFY(UiInspection::writeReport(window, QString("source-refresh-status-%1-%2")
                                                          .arg(QTest::currentDataTag())
                                                          .arg(geometry.width())));
        }
    }
};

QTEST_MAIN(PanelRefreshCompletionTests)
#include "panel_refresh_completion_tests.moc"
