#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "ui/support/layout_inspection.h"

class PanelColumnSortTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    static int propertyColumn(FileTable &view, PROPID id, bool raw) {
        for (int column = 0; column < view.columnCount(); ++column)
            if (view.propertyId(column) == id && view.isRawProperty(column) == raw)
                return column;
        return -1;
    }
    static void clickColumn(FileTable &view, int column) {
        view.doItemsLayout();
        view.scrollTo(view.model()->index(0, column));
        auto *header = view.horizontalHeader();
        const auto exposed = [&] {
            return QRect(header->sectionViewportPosition(column), 0, header->sectionSize(column),
                         header->viewport()->height())
                .intersected(header->viewport()->rect());
        };
        QTRY_VERIFY(!exposed().isEmpty());
        const QPoint point = exposed().center();
        QCOMPARE(header->logicalIndexAt(point), column);
        auto *window = header->window();
        QTest::mouseClick(window->windowHandle(), Qt::LeftButton, Qt::NoModifier,
                          header->viewport()->mapTo(window, point));
    }
    static void checkSort(FileTable &view, PROPID property, bool ascending) {
        QCOMPARE(view.propertyId(view.horizontalHeader()->sortIndicatorSection()), property);
        QCOMPARE(view.horizontalHeader()->sortIndicatorOrder(),
                 ascending ? Qt::AscendingOrder : Qt::DescendingOrder);
        QCOMPARE(view.model()->index(0, 0).data(Qt::UserRole + 6).toUInt(),
                 ascending ? UInt32(1) : UInt32(0));
        QCOMPARE(view.selectionModel()->selectedRows().size(), 2);
    }
    static void toggleCatalog(FileTable &view, unsigned index) {
        QTimer::singleShot(0, [&] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            QVERIFY(menu);
            menu->actions()[int(index)]->trigger();
            menu->close();
        });
        QVERIFY(QMetaObject::invokeMethod(view.horizontalHeader(), "customContextMenuRequested",
                                          Qt::DirectConnection, Q_ARG(QPoint, QPoint(10, 5))));
    }
    static void inspectViews(MainWindow &window, FileTable &view, FileTable &peer,
                             const QString &state) {
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(size);
            QApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(view, window));
            QVERIFY(UiInspection::fullyVisible(peer, window));
            QVERIFY(UiInspection::writeReport(window, QString("column-sort-%1-%2-%3")
                                                          .arg(QTest::currentDataTag(), state)
                                                          .arg(size.width())));
        }
    }
  private slots:
    void duplicateHeadersUseSourceSort_data() {
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<bool>("russian");
        QTest::addColumn<bool>("sizeProperty");
        QTest::addColumn<bool>("genericFirst");
        for (unsigned pane : {0u, 1u})
            for (bool russian : {false, true})
                for (bool sizeProperty : {false, true})
                    for (bool genericFirst : {false, true})
                        QTest::newRow(qPrintable(QString("%1-%2-%3-%4")
                                                     .arg(pane)
                                                     .arg(russian)
                                                     .arg(sizeProperty)
                                                     .arg(genericFirst)))
                            << pane << russian << sizeProperty << genericFirst;
    }
    void duplicateHeadersUseSourceSort() {
        QFETCH(unsigned, pane);
        QFETCH(bool, russian);
        QFETCH(bool, sizeProperty);
        QFETCH(bool, genericFirst);
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settings);
        SaveRegLang(UString(russian ? L"ru" : L"-"), settings);
        ReloadLang(settings);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString controlPath = fixture.filePath(QStringLiteral("control 文件.txt"));
        QFile control(controlPath);
        QVERIFY(control.open(QIODevice::WriteOnly));
        QCOMPARE(control.write("untouched"), qint64(9));
        control.close();
        const PROPID property = sizeProperty ? kpidSize : kpidCRC;
        QList<ArchiveEntry> entries;
        for (int index = 0; index < 2; ++index) {
            ArchiveEntry entry{index ? QStringLiteral("b δ.txt") : QStringLiteral("a 文件.txt"),
                               2 - index, 1};
            entry.crc = UInt32(2 - index);
            entry.rawProperties[property] = index ? QByteArray::fromHex("ff") : QByteArray();
            entries.append(entry);
        }
        ControlledBackend backend;
        MainWindow window(&backend);
        window.resize(980, 620);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        action(window, "twoPanelsAction")->trigger();
        auto *ui = window.findChild<QWidget *>(QString("filePanel%1").arg(pane));
        auto *view = static_cast<FileTable *>(ui->findChild<FileTable *>("archiveTable"));
        view->setFocus();
        QTRY_VERIFY(view->hasFocus());
        openListedArchive(window, backend, fixture.filePath(QStringLiteral("sort 文件.zip")),
                          entries);
        QCOMPARE(view->rowCount(), 2);
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto &panel = *view->sourcePanel;
        panel.Set_AutoRefresh_Mode(false);
        CMyComPtr<IFolderFolder> provider = panel.GetFolder();
        auto *peerUi = window.findChild<QWidget *>(QString("filePanel%1").arg(1 - pane));
        auto *peer = static_cast<FileTable *>(peerUi->findChild<FileTable *>("archiveTable"));
        peer->sourcePanel->Set_AutoRefresh_Mode(false);
        CMyComPtr<IFolderFolder> peerProvider = peer->sourcePanel->GetFolder();
        const int peerRows = peer->rowCount();
        const PROPID peerSort = peer->sourcePanel->_sortID;
        const bool peerAscending = peer->sourcePanel->_ascending;
        peer->setFocus();
        QTRY_VERIFY(peer->hasFocus());
        QWidget *const focused = QApplication::focusWidget();
        QVERIFY(focused);
        const int generic = propertyColumn(*view, property, false);
        const int raw = propertyColumn(*view, property, true);
        QVERIFY(generic >= 0);
        QVERIFY(raw >= 0);
        for (const int column : {generic, raw}) {
            if (view->isColumnHidden(column)) {
                const auto &columns = panel.GetColumns();
                unsigned index = 0;
                while (index < columns.Size() && view->columnForCatalogProperty(index) != column)
                    ++index;
                QVERIFY(index > 0 && index < columns.Size());
                toggleCatalog(*view, index);
                QVERIFY(!view->isColumnHidden(column));
            }
        }
        panel.SelectAll(true);
        const int first = genericFirst ? generic : raw;
        const int second = genericFirst ? raw : generic;
        bool ascending = !sizeProperty;
        for (int column : {first, second, first, first}) {
            clickColumn(*view, column);
            QVERIFY(!QTest::currentTestFailed());
            checkSort(*view, property, ascending);
            QCOMPARE(view->horizontalHeader()->sortIndicatorSection(), column);
            QCOMPARE(window.focusWidget(), static_cast<QWidget *>(view));
            if (sizeProperty)
                QVERIFY(action(window, "sort3Action")->isChecked());
            ascending = !ascending;
        }
        const bool explicitAscending = panel._ascending;
        view->horizontalHeader()->resizeSection(raw, 180);
        view->sortItems(second, explicitAscending ? Qt::AscendingOrder : Qt::DescendingOrder);
        checkSort(*view, property, explicitAscending);
        unsigned rawCatalog = 0;
        for (unsigned index = 0; index < panel.GetColumns().Size(); ++index)
            if (panel.GetColumns()[index].ID == property && panel.GetColumns()[index].IsRawProp)
                rawCatalog = index;
        QVERIFY(rawCatalog > 0);
        toggleCatalog(*view, rawCatalog);
        QVERIFY(view->isColumnHidden(generic));
        toggleCatalog(*view, rawCatalog);
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        const int appended = view->columnForVisibleProperty(panel.GetVisibleColumns().Size() - 1);
        QVERIFY(appended >= 0 && appended != raw);
        clickColumn(*view, appended);
        checkSort(*view, property, !sizeProperty);
        clickColumn(*view, raw);
        checkSort(*view, property, sizeProperty);
        action(window, sizeProperty ? "sortDescendingAction" : "sortAscendingAction")->trigger();
        checkSort(*view, property, !sizeProperty);
        QCOMPARE(view->horizontalHeader()->sortIndicatorSection(), raw);
        inspectViews(window, *view, *peer, "copies");
        panel.OnReload();
        QCOMPARE(view->rowCount(), 2);
        checkSort(*view, property, !sizeProperty);
        inspectViews(window, *view, *peer, "refresh");
        QCOMPARE(panel.GetFolder(), (IFolderFolder *)provider);
        QCOMPARE(peer->sourcePanel->GetFolder(), (IFolderFolder *)peerProvider);
        QCOMPARE(peer->rowCount(), peerRows);
        QCOMPARE(peer->sourcePanel->_sortID, peerSort);
        QCOMPARE(peer->sourcePanel->_ascending, peerAscending);
        QVERIFY(control.open(QIODevice::ReadOnly));
        QCOMPARE(control.readAll(), QByteArray("untouched"));
    }
};
QTEST_MAIN(PanelColumnSortTests)
#include "panel_column_sort_tests.moc"
