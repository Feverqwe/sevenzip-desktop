#include "archive/support/schema_folder.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "filemanager/window/qtnavigationbar.h"
#include "localization/LangUtils.h"
#include "ui/support/layout_inspection.h"
#include <QPainter>
#include <QStyleOptionViewItem>

class PanelColumnChangesTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    static void checkPrimarySelection(FileTable &view) {
        const unsigned count = view.sourcePanel->GetVisibleColumns().Size();
        if (!count)
            return;
        auto paint = [&](int column, bool selected) {
            QImage image(240, 32, QImage::Format_ARGB32_Premultiplied);
            image.fill(view.palette().base().color());
            QPainter painter(&image);
            QStyleOptionViewItem option;
            option.initFrom(&view);
            option.widget = &view;
            option.rect = image.rect();
            // Compare resting selection independently of the physical cursor:
            // the Windows 11 style also highlights hovered items.
            option.state &=
                ~(QStyle::State_Selected | QStyle::State_HasFocus | QStyle::State_MouseOver);
            if (selected)
                option.state |= QStyle::State_Selected;
            view.itemDelegate()->paint(&painter, option, view.model()->index(0, column));
            return image;
        };
        const int primary = view.columnForVisibleProperty(0);
        QVERIFY(primary >= 0);
        QVERIFY2(paint(primary, true) != paint(primary, false),
                 "The source main subitem must visibly retain selection");
        if (count > 1) {
            const int secondary = view.columnForVisibleProperty(1);
            QCOMPARE(paint(secondary, true) != paint(secondary, false), view.fullRowHighlight());
        }
    }
    static void inspectSchema(MainWindow &window, FileTable &view, FileTable &peer,
                              const QString &phase) {
        const auto &columns = view.sourcePanel->GetColumns();
        unsigned visibleCount = 0;
        for (int column = 0; column < view.columnCount(); ++column)
            if (!view.isColumnHidden(column)) {
                ++visibleCount;
                const CPropColumn *prop = view.sourceColumn(column);
                QVERIFY(prop);
                QCOMPARE(view.model()->headerData(column, Qt::Horizontal).toString(),
                         ToQString(prop->Name));
                for (int row = 0; row < view.rowCount(); ++row) {
                    QCOMPARE(view.model()
                                 ->index(row, column)
                                 .data(Qt::TextAlignmentRole)
                                 .value<Qt::Alignment>(),
                             GetColumnAlign(prop->ID, prop->Type));
                    QCOMPARE(
                        view.model()->index(row, column).data().toString(),
                        view.sourcePanel->GetItemText(
                            view.model()->index(row, 0).data(Qt::UserRole + 6).toUInt(), *prop));
                }
            }
        QCOMPARE(visibleCount, view.sourcePanel->GetVisibleColumns().Size());
        checkPrimarySelection(view);
        auto *icons = view.parentWidget()->findChild<QListView *>("iconList");
        QVERIFY(icons);
        if (visibleCount) {
            QCOMPARE(icons->modelColumn(), view.columnForVisibleProperty(0));
            for (int row = 0; row < view.rowCount(); ++row) {
                QCOMPARE(icons->model()->index(row, icons->modelColumn()).data().toString(),
                         view.sourcePanel->GetItemText(
                             view.model()->index(row, 0).data(Qt::UserRole + 6).toUInt(),
                             view.sourcePanel->GetVisibleColumns()[0]));
                QVERIFY(
                    !view.model()->index(row, 0).data(Qt::DecorationRole).value<QIcon>().isNull());
                QCOMPARE(
                    qvariant_cast<QIcon>(
                        icons->model()->index(row, icons->modelColumn()).data(Qt::DecorationRole))
                        .cacheKey(),
                    view.model()->index(row, 0).data(Qt::DecorationRole).value<QIcon>().cacheKey());
            }
        }
        if (!columns.IsEmpty())
            QTimer::singleShot(0, [&] {
                auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
                QVERIFY(menu);
                menu->close();
                QCOMPARE(menu->actions().size(), int(columns.Size()));
                for (unsigned index = 0; index < columns.Size(); ++index) {
                    QCOMPARE(menu->actions()[int(index)]->text(), ToQString(columns[index].Name));
                    QCOMPARE(menu->actions()[int(index)]->isChecked(), columns[index].IsVisible);
                    QCOMPARE(menu->actions()[int(index)]->isEnabled(), index != 0);
                }
            });
        QVERIFY(QMetaObject::invokeMethod(view.horizontalHeader(), "customContextMenuRequested",
                                          Qt::DirectConnection, Q_ARG(QPoint, QPoint(10, 5))));
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(size);
            QApplication::processEvents();
            QWidget &visibleView = icons->isVisible() ? static_cast<QWidget &>(*icons) : view;
            QVERIFY(UiInspection::fullyVisible(visibleView, window));
            auto *peerStack = qobject_cast<QStackedWidget *>(peer.parentWidget());
            QVERIFY(peerStack);
            QVERIFY(UiInspection::fullyVisible(*peerStack->currentWidget(), window));
            if (view.isVisible()) {
                if (columns.IsEmpty())
                    QVERIFY(!view.horizontalHeader()->isVisible());
                else
                    QVERIFY(UiInspection::fullyVisible(*view.horizontalHeader(), window));
            }
            QWidget &reportRoot = icons->isVisible() ? visibleView : window;
            QVERIFY(UiInspection::writeReport(reportRoot, QString("column-changes-%1-%2-%3")
                                                              .arg(QTest::currentDataTag(), phase)
                                                              .arg(size.width())));
        }
    }
    static void dataRows(bool counts) {
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<bool>("russian");
        if (counts)
            QTest::addColumn<bool>("failure");
        else
            QTest::addColumn<int>("mode");
        for (unsigned pane : {0u, 1u})
            for (bool russian : {false, true}) {
                if (counts) {
                    for (bool failure : {false, true})
                        QTest::newRow(qPrintable(
                            QString("count-%1-%2-%3").arg(pane).arg(russian).arg(failure)))
                            << pane << russian << failure;
                } else
                    for (int mode = 0; mode < 4; ++mode)
                        QTest::newRow(
                            qPrintable(QString("schema-%1-%2-%3").arg(pane).arg(russian).arg(mode)))
                            << pane << russian << mode;
            }
    }
    void runScenario(unsigned pane, bool russian, bool counts, bool failure, int mode = 3) {
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
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        action(window, "twoPanelsAction")->trigger();
        auto *ui = window.findChild<QWidget *>(QString("filePanel%1").arg(pane));
        auto *view = static_cast<FileTable *>(ui->findChild<FileTable *>("archiveTable"));
        static_cast<QtNavigationBar *>(
            ui->findChild<QWidget *>(QString("panelNavigation%1").arg(pane)))
            ->Edit();
        action(window, "openRootAction")->trigger();
        action(window, qPrintable(QString("viewMode%1Action").arg(mode)))->trigger();
        auto *spec = new SchemaFolder;
        CMyComPtr<IFolderFolder> provider = spec;
        auto &panel = *view->sourcePanel;
        panel.Set_AutoRefresh_Mode(false);
        panel.SetNewFolder(provider);
        panel.LoadFullPath();
        panel.OnReload();
        QCOMPARE(view->rowCount(), 2);
        panel.SelectAll(true);
        auto *peerUi = window.findChild<QWidget *>(QString("filePanel%1").arg(1 - pane));
        auto *peer = static_cast<FileTable *>(peerUi->findChild<FileTable *>("archiveTable"));
        peer->sourcePanel->Set_AutoRefresh_Mode(false);
        CMyComPtr<IFolderFolder> peerProvider = peer->sourcePanel->GetFolder();
        const int peerRows = peer->rowCount();
        static_cast<QtNavigationBar *>(
            peerUi->findChild<QWidget *>(QString("panelNavigation%1").arg(1 - pane)))
            ->Edit();
        QWidget *const focused = window.focusWidget();
        auto reload = [&](const QString &phase, unsigned columns) {
            panel.OnReload();
            QCOMPARE(panel.GetFolder(), (IFolderFolder *)provider);
            QCOMPARE(panel.GetColumns().Size(), columns);
            QCOMPARE(panel.GetVisibleColumns().Size(), columns);
            QCOMPARE(view->rowCount(), 2);
            QCOMPARE(view->selectionModel()->selectedRows().size(), 2);
            inspectSchema(window, *view, *peer, phase);
            QCOMPARE(window.focusWidget(), focused);
        };
        if (counts) {
            spec->propertyCountResult = spec->rawCountResult = failure ? E_FAIL : S_FALSE;
            reload("written", 4);
        } else {
            view->sortItems(view->columnForProperty(kpidSize), Qt::DescendingOrder);
            QCOMPARE(panel.GetSortID(), PROPID(kpidSize));
            spec->properties = {
                {kpidCRC, VT_UI4}, {kpidIsDir, VT_BOOL}, {kpidName, VT_BSTR, E_FAIL}};
            spec->rawId = kpidName;
            reload("raw-name", 2);
            QCOMPARE(panel.GetSortID(), PROPID(kpidName));
            QVERIFY(view->isColumnHidden(0));
            QVERIFY(panel.GetVisibleColumns()[0].IsRawProp);
            QCOMPARE(panel.GetVisibleColumns()[0].ID, PROPID(kpidName));
            for (const QString phase : {"raw-name-hidden", "raw-name-appended"}) {
                QTimer::singleShot(0, [&] {
                    auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
                    QVERIFY(menu);
                    menu->actions()[1]->trigger();
                    menu->close();
                });
                QVERIFY(QMetaObject::invokeMethod(
                    view->horizontalHeader(), "customContextMenuRequested", Qt::DirectConnection,
                    Q_ARG(QPoint, QPoint(10, 5))));
                QCOMPARE(panel.GetVisibleColumns()[0].ID, PROPID(kpidCRC));
                QCOMPARE(view->selectionModel()->selectedRows().size(), 2);
                inspectSchema(window, *view, *peer, phase);
                QCOMPARE(window.focusWidget(), focused);
            }
            spec->rawInfoResult = E_ABORT;
            reload("no-name", 1);
            QVERIFY(view->isColumnHidden(0));
            spec->properties.clear();
            spec->rawEnabled = false;
            reload("empty", 0);
            spec->properties = {{kpidName, VT_BSTR}, {kpidSize, VT_UI8}, {kpidCRC, VT_UI4}};
            spec->writePropertyCount = spec->writeRawCount = false;
            spec->propertyCountResult = spec->rawCountResult = E_ABORT;
            const unsigned queries = spec->propertyInfoQueries;
            reload("unwritten", 0);
            QCOMPARE(spec->propertyInfoQueries, queries);
        }
        spec->writePropertyCount = spec->writeRawCount = true;
        spec->propertyCountResult = spec->rawCountResult = spec->rawInfoResult = S_OK;
        spec->rawEnabled = true;
        spec->rawId = kpidCRC;
        reload("recovery", 4);
        QVERIFY(!view->isColumnHidden(0));
        QCOMPARE(peer->sourcePanel->GetFolder(), (IFolderFolder *)peerProvider);
        QCOMPARE(peer->rowCount(), peerRows);
        QVERIFY(control.open(QIODevice::ReadOnly));
        QCOMPARE(control.readAll(), QByteArray("untouched"));
        QCOMPARE(backend.listCalls, 0);
    }
  private slots:
    void writtenCountRetainsNativeEnumeration_data() { dataRows(true); }
    void writtenCountRetainsNativeEnumeration() {
        QFETCH(unsigned, pane);
        QFETCH(bool, russian);
        QFETCH(bool, failure);
        runScenario(pane, russian, true, failure);
    }
    void sameProviderSchemaChanges_data() { dataRows(false); }
    void sameProviderSchemaChanges() {
        QFETCH(unsigned, pane);
        QFETCH(bool, russian);
        QFETCH(int, mode);
        runScenario(pane, russian, false, false, mode);
    }
};
QTEST_MAIN(PanelColumnChangesTests)
#include "panel_column_changes_tests.moc"
