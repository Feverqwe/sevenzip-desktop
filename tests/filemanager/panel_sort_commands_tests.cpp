#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "filemanager/window/qtnavigationbar.h"
#include "localization/LangUtils.h"
#include "ui/support/layout_inspection.h"
#include <QHeaderView>
#include <QScrollBar>

class PanelSortCommandsTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
  private slots:
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
        views[1]->sortItems(2, Qt::DescendingOrder);
        QCOMPARE(views[1]->sourcePanel->_sortID, PROPID(kpidSize));
        QVERIFY(!views[1]->sourcePanel->_ascending);
        QCOMPARE(views[0]->sourcePanel->_sortID, peerSort);
        QCOMPARE(views[0]->sourcePanel->_ascending, peerAscending);
        QCOMPARE(views[0]->sourcePanel->GetFolder(), peer.Interface());
        QVERIFY(addresses[0]->hasFocus());
        QVERIFY(action(window, "sort0Action")->isChecked());
        action(window, "viewMode2Action")->trigger();
        navigation[1]->Edit();
        QTRY_VERIFY(addresses[1]->hasFocus());
        QVERIFY(action(window, "sort3Action")->isChecked());
        QVERIFY(action(window, "viewMode3Action")->isChecked());
        action(window, "viewMode0Action")->trigger();
        navigation[0]->Edit();
        QTRY_VERIFY(addresses[0]->hasFocus());
        QVERIFY(action(window, "sort0Action")->isChecked());
        QVERIFY(action(window, "viewMode2Action")->isChecked());
        QCOMPARE(views[0]->sourcePanel->GetFolder(), peer.Interface());
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

QTEST_MAIN(PanelSortCommandsTests)
#include "panel_sort_commands_tests.moc"
