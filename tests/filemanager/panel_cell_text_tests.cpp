#include "archive/support/schema_folder.h"
#include "filemanager/support/panel_model_support.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "support/panel_keys.h"
#include "ui/support/layout_inspection.h"
#include <QPersistentModelIndex>

class PanelCellTextTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    static QString uncached(FileTable &view, int column) {
        SetPanelData(view.model()->index(0, column), QVariant(), Qt::DisplayRole);
        return view.model()->index(0, column).data().toString();
    }
    static void reportViews(MainWindow &window, QWidget &visibleView, FileTable &peer,
                            const QString &phase) {
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(size);
            QApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(visibleView, window));
            QVERIFY(UiInspection::fullyVisible(peer, window));
            QVERIFY(UiInspection::writeReport(window, QString("cell-text-%1-%2-%3")
                                                          .arg(QTest::currentDataTag(), phase)
                                                          .arg(size.width())));
        }
    }
  private slots:
    void selectionProjectionKeepsTextCallbacks_data() {
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<bool>("russian");
        QTest::addColumn<bool>("icons");
        for (unsigned pane : {0u, 1u})
            for (bool russian : {false, true})
                for (bool icons : {false, true})
                    QTest::newRow(qPrintable(QString("%1-%2-%3").arg(pane).arg(russian).arg(icons)))
                        << pane << russian << icons;
    }
    void selectionProjectionKeepsTextCallbacks() {
        QFETCH(unsigned, pane);
        QFETCH(bool, russian);
        QFETCH(bool, icons);
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settings);
        SaveRegLang(UString(russian ? L"ru" : L"-"), settings);
        ReloadLang(settings);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        action(window, "twoPanelsAction")->trigger();
        auto *ui = window.findChild<QWidget *>(QString("filePanel%1").arg(pane));
        auto *view = static_cast<FileTable *>(ui->findChild<FileTable *>("archiveTable"));
        PanelKeys::focusAddress(ui->findChild<QLineEdit *>("archivePath"));
        action(window, "openRootAction")->trigger();
        if (icons)
            action(window, "viewMode0Action")->trigger();
        auto *spec = new SchemaFolder;
        CMyComPtr<IFolderFolder> provider = spec;
        auto &panel = *view->sourcePanel;
        panel.Set_AutoRefresh_Mode(false);
        panel.SetNewFolder(provider);
        panel.LoadFullPath();
        panel.OnReload();
        QCOMPARE(view->rowCount(), 2);
        panel.SelectAll(false);
        view->setCurrentCell(0, 0, QItemSelectionModel::NoUpdate);
        auto *peerUi = window.findChild<QWidget *>(QString("filePanel%1").arg(1 - pane));
        auto *peer = static_cast<FileTable *>(peerUi->findChild<FileTable *>("archiveTable"));
        peer->sourcePanel->Set_AutoRefresh_Mode(false);
        CMyComPtr<IFolderFolder> peerProvider = peer->sourcePanel->GetFolder();
        const int peerRows = peer->rowCount();
        PanelKeys::focusAddress(peerUi->findChild<QLineEdit *>("archivePath"));
        QWidget *const focused = window.focusWidget();
        bool reading = false, outer = false, selected = false;
        unsigned callbacks = 0;
        bool valid = true;
        const auto observe = [&] {
            if (reading)
                return;
            reading = true;
            ++callbacks;
            valid &= panel.ShouldProcessNotifications() == !outer;
            for (int row = 0; row < view->rowCount(); ++row) {
                const UInt32 index = view->model()->index(row, 0).data(Qt::UserRole + 6).toUInt();
                const QString expected =
                    outer ? QString()
                          : ToQString(index ? spec->items->secondName : spec->items->firstName);
                SetPanelData(view->model()->index(row, 0), QVariant(), Qt::DisplayRole);
                valid &= view->model()->index(row, 0).data().toString() == expected;
                valid &= panel.IsItemMarked(index) == selected;
            }
            reading = false;
        };
        const auto selectionConnection = connect(
            view->selectionModel(), &QItemSelectionModel::selectionChanged, &window, observe);
        const auto paintConnection = connect(
            view->model(), &QAbstractItemModel::dataChanged, &window,
            [&](const QModelIndex &, const QModelIndex &, const QList<int> &) { observe(); });
        const auto disconnectObservers = qScopeGuard([&] {
            disconnect(selectionConnection);
            disconnect(paintConnection);
        });
        for (bool guarded : {false, true}) {
            outer = guarded;
            std::unique_ptr<CPanel::CDisableNotify> guard;
            if (outer)
                guard = std::make_unique<CPanel::CDisableNotify>(panel);
            for (bool state : {true, false}) {
                selected = state;
                const unsigned before = callbacks;
                panel.SelectAll(state);
                QVERIFY(callbacks > before);
                QVERIFY2(valid,
                         "Selection projection suppressed text callbacks or changed source marks");
                QCOMPARE(panel.ShouldProcessNotifications(), !outer);
                QCOMPARE(view->selectionModel()->selectedRows().size(), state ? 2 : 0);
            }
        }
        disconnect(selectionConnection);
        disconnect(paintConnection);
        QVERIFY(panel.ShouldProcessNotifications());
        for (int row = 0; row < view->rowCount(); ++row) {
            const UInt32 index = view->model()->index(row, 0).data(Qt::UserRole + 6).toUInt();
            QVERIFY(!panel.IsItemMarked(index));
            QVERIFY(!view->model()->index(row, 0).data(Qt::BackgroundRole).isValid());
            SetPanelData(view->model()->index(row, 0), QVariant(), Qt::DisplayRole);
            QCOMPARE(view->model()->index(row, 0).data().toString(),
                     ToQString(index ? spec->items->secondName : spec->items->firstName));
        }
        QCOMPARE(panel.GetFolder(), (IFolderFolder *)provider);
        QCOMPARE(peer->sourcePanel->GetFolder(), (IFolderFolder *)peerProvider);
        QCOMPARE(peer->rowCount(), peerRows);
        QCOMPARE(window.focusWidget(), focused);
        QWidget &visible =
            icons ? static_cast<QWidget &>(*ui->findChild<QListView *>("iconList")) : *view;
        reportViews(window, visible, *peer, "selection-notify");
    }
    void publishedIndicesUseSourceProperties_data() {
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<bool>("russian");
        QTest::addColumn<bool>("icons");
        for (unsigned pane : {0u, 1u})
            for (bool russian : {false, true})
                for (bool icons : {false, true})
                    QTest::newRow(qPrintable(QString("%1-%2-%3").arg(pane).arg(russian).arg(icons)))
                        << pane << russian << icons;
    }
    void publishedIndicesUseSourceProperties() {
        QFETCH(unsigned, pane);
        QFETCH(bool, russian);
        QFETCH(bool, icons);
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settings);
        SaveRegLang(UString(russian ? L"ru" : L"-"), settings);
        ReloadLang(settings);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QFile control(fixture.filePath(QStringLiteral("control 文件.txt")));
        QVERIFY(control.open(QIODevice::WriteOnly));
        QCOMPARE(control.write("untouched"), qint64(9));
        control.close();
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        action(window, "twoPanelsAction")->trigger();
        auto *ui = window.findChild<QWidget *>(QString("filePanel%1").arg(pane));
        auto *view = static_cast<FileTable *>(ui->findChild<FileTable *>("archiveTable"));
        PanelKeys::focusAddress(ui->findChild<QLineEdit *>("archivePath"));
        action(window, "openRootAction")->trigger();
        if (icons)
            action(window, "viewMode0Action")->trigger();
        auto *spec = new SchemaFolder;
        CMyComPtr<IFolderFolder> provider = spec;
        auto &panel = *view->sourcePanel;
        panel.Set_AutoRefresh_Mode(false);
        panel.SetNewFolder(provider);
        panel.LoadFullPath();
        panel.OnReload();
        QCOMPARE(view->rowCount(), 2);
        panel.SelectAll(true);
        view->setSortingEnabled(false);
        const int generic = view->columnForCatalogProperty(2);
        const int raw = view->columnForCatalogProperty(3);
        QVERIFY(generic >= 0 && raw >= 0 && generic != raw);
        auto *peerUi = window.findChild<QWidget *>(QString("filePanel%1").arg(1 - pane));
        auto *peer = static_cast<FileTable *>(peerUi->findChild<FileTable *>("archiveTable"));
        peer->sourcePanel->Set_AutoRefresh_Mode(false);
        CMyComPtr<IFolderFolder> peerProvider = peer->sourcePanel->GetFolder();
        const int peerRows = peer->rowCount();
        PanelKeys::focusAddress(peerUi->findChild<QLineEdit *>("archivePath"));
        QWidget *const focused = window.focusWidget();
        const UInt32 sourceIndex = view->model()->index(0, 0).data(Qt::UserRole + 6).toUInt();
        const QString sourceName =
            ToQString(sourceIndex ? spec->items->secondName : spec->items->firstName);
        for (HRESULT result : {S_FALSE, E_FAIL, E_ABORT}) {
            spec->items->countResult = result;
            const unsigned queries = spec->countQueries;
            QCOMPARE(uncached(*view, 0), sourceName);
            QCOMPARE(uncached(*view, generic), QString("12345678"));
            QCOMPARE(uncached(*view, raw), QString("DEADBEEF"));
            QCOMPARE(spec->countQueries, queries);
            spec->crcResult = result;
            spec->rawResult = result;
            QCOMPARE(uncached(*view, generic), QString::fromUtf8(russian ? "Ошибка: " : "Error: "));
            QCOMPARE(uncached(*view, raw), QString());
            QCOMPARE(spec->countQueries, queries);
            spec->crcResult = spec->rawResult = S_OK;
        }
        spec->items->countResult = S_OK;
        spec->crcResult = E_FAIL;
        const QPersistentModelIndex errorItem = view->model()->index(0, generic);
        const QString initialError = uncached(*view, generic);
        {
            CPanel::CDisableNotify guard(panel);
            SaveRegLang(UString(russian ? L"-" : L"ru"), settings);
            ReloadLang(settings);
            QtNotifyLanguageChanged();
            QCOMPARE(errorItem.data().toString(), initialError);
        }
        QCOMPARE(errorItem.data().toString(), QString::fromUtf8(russian ? "Error: " : "Ошибка: "));
        QCOMPARE(view->model()->index(0, generic), errorItem);
        QCOMPARE(view->selectionModel()->selectedRows().size(), 2);
        spec->crcResult = S_OK;
        SaveRegLang(UString(russian ? L"ru" : L"-"), settings);
        ReloadLang(settings);
        QtNotifyLanguageChanged();
        QCOMPARE(uncached(*view, generic), QString("12345678"));
        QCOMPARE(uncached(*view, raw), QString("DEADBEEF"));
        const unsigned rawQueries = spec->rawQueries;
        {
            CPanel::CDisableNotify guard(panel);
            QCOMPARE(view->model()->index(0, raw).data().toString(), QString("DEADBEEF"));
            QCOMPARE(uncached(*view, raw), QString());
            QCOMPARE(spec->rawQueries, rawQueries);
        }
        const auto savedFolder = view->textFolder;
        view->textFolder = peerProvider;
        QCOMPARE(uncached(*view, raw), QString());
        QCOMPARE(spec->rawQueries, rawQueries);
        view->textFolder = savedFolder;
        QCOMPARE(uncached(*view, raw), QString("DEADBEEF"));
        panel.OnReload();
        QCOMPARE(panel.GetFolder(), (IFolderFolder *)provider);
        QCOMPARE(view->rowCount(), 2);
        QCOMPARE(view->selectionModel()->selectedRows().size(), 2);
        QCOMPARE(window.focusWidget(), focused);
        QCOMPARE(peer->sourcePanel->GetFolder(), (IFolderFolder *)peerProvider);
        QCOMPARE(peer->rowCount(), peerRows);
        QWidget &visibleView =
            icons ? static_cast<QWidget &>(*ui->findChild<QListView *>("iconList")) : *view;
        reportViews(window, visibleView, *peer, "recovery");
        const QPersistentModelIndex previous = view->model()->index(0, raw);
        auto *replacement = new SchemaFolder;
        replacement->items->firstName = L"replacement δ";
        replacement->items->secondName = L"replacement 文件.txt";
        panel.SetNewFolder(replacement);
        const unsigned previousRawQueries = spec->rawQueries;
        panel.LoadFullPath();
        panel.OnReload();
        QVERIFY(!previous.isValid());
        QCOMPARE(view->rowCount(), 2);
        const UInt32 replacementIndex = view->model()->index(0, 0).data(Qt::UserRole + 6).toUInt();
        QCOMPARE(uncached(*view, 0), ToQString(replacementIndex ? replacement->items->secondName
                                                                : replacement->items->firstName));
        reportViews(window, visibleView, *peer, "rebind");
        QCOMPARE(spec->rawQueries, previousRawQueries);
        QCOMPARE(window.focusWidget(), focused);
        QCOMPARE(peer->sourcePanel->GetFolder(), (IFolderFolder *)peerProvider);
        QCOMPARE(peer->rowCount(), peerRows);
        QVERIFY(control.open(QIODevice::ReadOnly));
        QCOMPARE(control.readAll(), QByteArray("untouched"));
        QCOMPARE(backend.listCalls, 0);
    }
};
QTEST_MAIN(PanelCellTextTests)
#include "panel_cell_text_tests.moc"
