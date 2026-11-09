#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "filemanager/window/qtnavigationbar.h"
#include "localization/LangUtils.h"
#include "support/window_activation.h"
#include "ui/support/layout_inspection.h"
#include <QHeaderView>
#include <QScrollBar>

class PanelSortCommandsTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
  private slots:
    void directionMenuUsesNativeSortAndRetainsSelection_data() {
        QTest::addColumn<bool>("russian");
        QTest::newRow("en") << false;
        QTest::newRow("ru") << true;
    }
    void directionMenuUsesNativeSortAndRetainsSelection() {
        QFETCH(bool, russian);
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        SaveRegLang(UString(russian ? L"ru" : L"-"),
                    qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir fixture;
        ControlledBackend backend;
        MainWindow window(&backend);
        openListedArchive(window, backend, fixture.filePath("menu sort.zip"),
                          {{"small.txt", 2, 2}, {"large.txt", 100, 100}, {"middle.txt", 10, 10}});
        window.show();
        auto *view = table(window);
        view->setCurrentCell(1, 0);
        view->selectRow(1);
        const QString selected = view->model()->index(1, 0).data(Qt::UserRole + 3).toString();
        auto *sort = window.findChild<QMenu *>("sortMenu");
        QVERIFY(sort);
        action(window, "sort3Action")->trigger();
        auto *ascending = action(window, "sortAscendingAction");
        auto *descending = action(window, "sortDescendingAction");
        QCOMPARE(ascending->text(), russian ? QString("По возрастанию") : QString("Ascending"));
        QCOMPARE(descending->text(), russian ? QString("По убыванию") : QString("Descending"));
        QVERIFY(descending->isChecked());
        for (auto *direction : {ascending, descending, descending}) {
            sort->popup(window.mapToGlobal(QPoint(20, 20)));
            QTRY_VERIFY(sort->isVisible());
            QVERIFY(UiInspection::writeReport(
                *sort, QString("workspace-sort-menu-%1-%2")
                           .arg(russian ? "ru" : "en",
                                ascending->isChecked() ? "ascending" : "descending")));
            QTest::mouseClick(sort, Qt::LeftButton, Qt::NoModifier,
                              sort->actionGeometry(direction).center());
            QTRY_VERIFY(!sort->isVisible());
            QVERIFY(direction->isChecked());
            const bool up = direction == ascending;
            QCOMPARE(view->horizontalHeader()->sortIndicatorOrder(),
                     up ? Qt::AscendingOrder : Qt::DescendingOrder);
            QCOMPARE(view->model()->index(0, 0).data(Qt::UserRole + 3).toString(),
                     up ? QString("small.txt") : QString("large.txt"));
            QCOMPARE(view->model()->index(view->currentRow(), 0).data(Qt::UserRole + 3).toString(),
                     selected);
            QVERIFY(view->selectionModel()->isRowSelected(view->currentRow(), {}));
        }
        auto *appearance = window.findChild<QMenu *>("appearanceMenu");
        QVERIFY(appearance);
        QList<QAction *> visible;
        for (auto *choice : appearance->actions())
            if (choice->isVisible() && !choice->isSeparator())
                visible.append(choice);
        QCOMPARE(visible.size(), 2);
        QVERIFY(visible.contains(action(window, "viewMode0Action")));
        QVERIFY(visible.contains(action(window, "viewMode3Action")));
        action(window, "viewMode0Action")->trigger();
        action(window, "viewMode3Action")->trigger();
        QCOMPARE(view->horizontalHeader()->sortIndicatorOrder(), Qt::DescendingOrder);
        QCOMPARE(view->model()->index(view->currentRow(), 0).data(Qt::UserRole + 3).toString(),
                 selected);
    }
    void headerSortWaitsForTheRefreshedList() {
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const auto write = [&](const QString &name, const QByteArray &bytes) {
            QFile file(fixture.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(bytes), bytes.size());
        };
        const QString selected = QStringLiteral("a selected.txt");
        write(selected, "a");
        write("z largest.txt", QByteArray(128, 'z'));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        window.openPath(fixture.path());
        auto *view = table(window);
        QTRY_COMPARE(view->rowCount(), 2);
        view->sourcePanel->Set_AutoRefresh_Mode(false);
        view->setFocus();
        QTRY_VERIFY(view->hasFocus());
        const int row = filesystemRow(window, selected);
        QVERIFY(row >= 0);
        view->setCurrentCell(row, 0);
        view->selectRow(row);
        for (int i = 0; i < 257; ++i)
            write(QString("item-%1.txt").arg(i), "item");

        auto *header = view->horizontalHeader();
        const int sizeColumn = view->columnForProperty(kpidSize);
        QVERIFY(sizeColumn >= 0);
        const auto clickSize = [&] {
            QTest::mouseClick(
                header->viewport(), Qt::LeftButton, Qt::NoModifier,
                QPoint(header->sectionViewportPosition(sizeColumn) + 10, header->height() / 2));
        };
        const int initialSort = header->sortIndicatorSection();
        auto *refresh = action(window, "refreshAction");
        QTest::keySequence(view, refresh->shortcut());
        QVERIFY(!refresh->isEnabled());
        QVERIFY(view->hasFocus());
        clickSize();
        QTRY_VERIFY(refresh->isEnabled() && view->rowCount() == 259);
        QCOMPARE(header->sortIndicatorSection(), initialSort);
        QVERIFY(view->hasFocus());
        clickSize();
        QTRY_COMPARE(view->model()->index(0, 0).data(QtPanelList::NameRole).toString(),
                     QString("z largest.txt"));
        QCOMPARE(view->currentIndex().data(QtPanelList::NameRole).toString(), selected);
        QVERIFY(view->selectionModel()->isRowSelected(view->currentRow(), {}));
    }
    void rawCommandUsesSourceColumnFlag() {
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir fixture;
        ControlledBackend backend;
        MainWindow window(&backend);
        QList<ArchiveEntry> entries;
        for (int i = 0; i < 3; ++i) {
            ArchiveEntry entry{QString("file%1 δ.txt").arg(i), 1, 1};
            entry.rawProperties[kpidSha1] = QByteArray::fromHex(i == 0   ? "ff"
                                                                : i == 1 ? "01"
                                                                         : "0100");
            entries.append(entry);
        }
        openListedArchive(window, backend, fixture.filePath("raw sort.zip"), entries);
        auto *view = static_cast<FileTable *>(table(window));
        const int column = view->columnForProperty(kpidSha1);
        QVERIFY(column >= 0);
        view->sourcePanel->SortItemsWithPropID(kpidSha1);
        QCOMPARE(view->horizontalHeader()->sortIndicatorSection(), column);
        QCOMPARE(view->horizontalHeader()->sortIndicatorOrder(), Qt::AscendingOrder);
        QCOMPARE(view->model()->index(0, 0).data(Qt::UserRole + 3).toString(), entries[1].path);
        QCOMPARE(view->model()->index(1, 0).data(Qt::UserRole + 3).toString(), entries[2].path);
        QCOMPARE(view->model()->index(2, 0).data(Qt::UserRole + 3).toString(), entries[0].path);
        view->sourcePanel->SortItemsWithPropID(kpidSha1);
        QCOMPARE(view->horizontalHeader()->sortIndicatorOrder(), Qt::DescendingOrder);
        QCOMPARE(view->model()->index(0, 0).data(Qt::UserRole + 3).toString(), entries[0].path);
    }
    void inactiveHeaderKeepsPeerSortState() {
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QFile file(fixture.filePath(QStringLiteral("control 文件.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("control"), qint64(7));
        file.close();
        QFile larger(fixture.filePath("z larger.txt"));
        QVERIFY(larger.open(QIODevice::WriteOnly));
        QCOMPARE(larger.write("larger payload"), qint64(14));
        larger.close();
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        action(window, "twoPanelsAction")->trigger();
        QList<FileTable *> views;
        QList<QLineEdit *> addresses;
        QList<QtNavigationBar *> navigation;
        for (unsigned index = 0; index < 2; ++index) {
            auto *pane = window.findChild<QWidget *>(QString("filePanel%1").arg(index));
            QVERIFY(pane);
            views.append(static_cast<FileTable *>(pane->findChild<FileTable *>("archiveTable")));
            addresses.append(pane->findChild<QLineEdit *>("archivePath"));
            navigation.append(static_cast<QtNavigationBar *>(
                pane->findChild<QWidget *>(QString("panelNavigation%1").arg(index))));
            QVERIFY(navigation.back());
            navigation.back()->Edit();
            addresses.back()->setFocus();
            addresses.back()->setText(fixture.path());
            QTest::keyClick(addresses.back(), Qt::Key_Return);
        }
        navigation[0]->Edit();
        const PROPID peerSort = views[0]->sourcePanel->_sortID;
        const bool peerAscending = views[0]->sourcePanel->_ascending;
        QVERIFY(action(window, "sort0Action")->isChecked());
        CMyComPtr<IFolderFolder> peer = views[0]->sourcePanel->GetFolder();
        const auto firstName = [](FileTable &view) {
            return view.model()->index(0, 0).data(QtPanelList::NameRole).toString();
        };
        const QString control = QStringLiteral("control 文件.txt");
        QCOMPARE(firstName(*views[0]), control);
        QCOMPARE(firstName(*views[1]), control);
        views[1]->selectRow(0);
        views[1]->sortItems(2, Qt::DescendingOrder);
        QCOMPARE(firstName(*views[1]), QString("z larger.txt"));
        QCOMPARE(firstName(*views[0]), control);
        const auto selected = views[1]->selectionModel()->selectedRows();
        QCOMPARE(selected.size(), 1);
        QCOMPARE(selected.front().data(QtPanelList::NameRole).toString(), control);
        QCOMPARE(views[1]->sourcePanel->_sortID, PROPID(kpidSize));
        QVERIFY(!views[1]->sourcePanel->_ascending);
        QCOMPARE(views[0]->sourcePanel->_sortID, peerSort);
        QCOMPARE(views[0]->sourcePanel->_ascending, peerAscending);
        QCOMPARE(views[0]->sourcePanel->GetFolder(), peer.Interface());
        QVERIFY(addresses[0]->hasFocus());
        QVERIFY(action(window, "sort0Action")->isChecked());
        action(window, "viewMode0Action")->trigger();
        navigation[1]->Edit();
        QTRY_VERIFY(addresses[1]->hasFocus());
        QVERIFY(action(window, "sort3Action")->isChecked());
        QVERIFY(action(window, "viewMode3Action")->isChecked());
        action(window, "viewMode0Action")->trigger();
        navigation[0]->Edit();
        QTRY_VERIFY(addresses[0]->hasFocus());
        QVERIFY(action(window, "sort0Action")->isChecked());
        QVERIFY(action(window, "viewMode0Action")->isChecked());
        QCOMPARE(views[0]->sourcePanel->GetFolder(), peer.Interface());
        auto *details = action(window, "viewMode3Action");
        QSignalSpy detailsChanges(details, &QAction::toggled);
        views[1]->sourcePanel->SetListViewMode(3);
        QVERIFY(views[1]->isVisible());
        QVERIFY(addresses[0]->hasFocus());
        QVERIFY(action(window, "viewMode0Action")->isChecked());
        QVERIFY(!details->isChecked());
        QVERIFY(detailsChanges.isEmpty());
    }
    void headerDefaults_data() {
        QTest::addColumn<quint32>("property");
        QTest::addColumn<bool>("russian");
        for (quint32 property :
             {quint32(kpidCTime), quint32(kpidATime), quint32(kpidMTime), quint32(kpidSize)})
            for (bool russian : {false, true})
                QTest::newRow(qPrintable(QString("%1-%2").arg(property).arg(russian)))
                    << property << russian;
    }
    void headerDefaults() {
        QFETCH(quint32, property);
        QFETCH(bool, russian);
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        SaveRegLang(UString(russian ? L"ru" : L"-"),
                    qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QFile file(fixture.filePath(QStringLiteral("имя space δ.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("control"), qint64(7));
        file.close();
        ControlledBackend backend;
        MainWindow window(&backend);
        auto *address = window.findChild<QLineEdit *>("archivePath");
        address->setText(fixture.path());
        QTest::keyClick(address, Qt::Key_Return);
        auto *view = static_cast<FileTable *>(table(window));
        const int column = view->columnForProperty(property);
        QVERIFY(column >= 0);
        view->setColumnHidden(column, false);
        window.resize(640, 400);
        window.show();
        QApplication::processEvents();
        view->scrollTo(view->model()->index(0, column));
        auto *header = view->horizontalHeader();
        QTest::mouseClick(
            header->viewport(), Qt::LeftButton, Qt::NoModifier,
            QPoint(header->sectionViewportPosition(column) + 10, header->height() / 2));
        QCOMPARE(header->sortIndicatorSection(), column);
        QCOMPARE(header->sortIndicatorOrder(), Qt::DescendingOrder);
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(size);
            QApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(*view, window));
            QVERIFY(UiInspection::writeReport(window, QString("source-sort-header-%1-%2")
                                                          .arg(QTest::currentDataTag())
                                                          .arg(size.width())));
        }
    }
    void menuSortShowsFocusedRow() {
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir fixture;
        ControlledBackend backend;
        MainWindow window(&backend);
        QList<ArchiveEntry> entries;
        for (int i = 0; i < 64; ++i)
            entries.append({QString("file%1 δ.txt").arg(i, 2, 10, QChar('0')), i + 1, i + 1});
        openListedArchive(window, backend, fixture.filePath("sort space.zip"), entries);
        window.resize(640, 400);
        window.show();
        QApplication::processEvents();
        auto *view = static_cast<FileTable *>(table(window));
        view->selectRow(0);
        view->verticalScrollBar()->setValue(0);
        action(window, "sort3Action")->trigger();
        QCOMPARE(view->sourcePanel->_sortID, PROPID(kpidSize));
        QVERIFY(!view->sourcePanel->_ascending);
        QCOMPARE(view->model()->index(view->currentRow(), 0).data(Qt::UserRole + 3).toString(),
                 entries[0].path);
        const QRect focused = view->visualRect(view->currentIndex());
        QVERIFY(view->viewport()->rect().contains(focused));
        CRecordVector<UInt32> selected;
        view->sourcePanel->Get_ItemIndices_Selected(selected);
        QCOMPARE(selected.Size(), 1u);
        QCOMPARE(selected[0], UInt32(0));
        action(window, "sort3Action")->trigger();
        QVERIFY(view->sourcePanel->_ascending);
        QVERIFY(view->viewport()->rect().contains(view->visualRect(view->currentIndex())));
        backend.queueList(entries);
        action(window, "refreshAction")->trigger();
        QCOMPARE(view->sourcePanel->_sortID, PROPID(kpidSize));
        QVERIFY(view->sourcePanel->_ascending);
        QCOMPARE(view->model()->index(view->currentRow(), 0).data(Qt::UserRole + 3).toString(),
                 entries[0].path);
        QVERIFY(view->viewport()->rect().contains(view->visualRect(view->currentIndex())));
    }
};

#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(PanelSortCommandsTests)
#include "panel_sort_commands_tests.moc"
