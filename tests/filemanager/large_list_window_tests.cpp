#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "ui/support/layout_inspection.h"
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>

class LargeListWindowTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
  private slots:
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }
    void guiHeartbeatDuringRealRowProjection_data() {
        QTest::addColumn<bool>("archive");
        QTest::addColumn<QSize>("size");
        for (const bool archive : {false, true}) {
            QTest::newRow(archive ? "archive-normal" : "disk-normal") << archive << QSize(980, 620);
            QTest::newRow(archive ? "archive-compact" : "disk-compact")
                << archive << QSize(640, 480);
        }
    }
    void guiHeartbeatDuringRealRowProjection() {
        QFETCH(bool, archive);
        QFETCH(QSize, size);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        constexpr int count = 4096;
        QList<ArchiveEntry> entries;
        for (int i = 0; i < count; ++i) {
            const QString name = QStringLiteral("имя с пробелом %1.txt").arg(i, 5, 10, QChar('0'));
            if (archive)
                entries.append(ArchiveEntry{name, 9, 5});
            else {
                QFile file(fixture.filePath(name));
                QVERIFY(file.open(QIODevice::WriteOnly));
                QCOMPARE(file.write("123456789"), qint64(9));
            }
        }
        ControlledBackend backend;
        MainWindow window(&backend);
        window.resize(size);
        window.show();
        action(window, "viewMode3Action")->trigger();

        auto *list = table(window);
        bool scheduled = false;
        int heartbeatRows = -1;
        bool panelSwitchBlocked = false;
        bool panelShortcutIgnored = false;
        QElapsedTimer guiTime;
        QElapsedTimer elapsed;
        qint64 firstRowMs = -1;
        qint64 lastRowMs = -1;
        int publishedRows = 0;
        qint64 lastHeartbeat = 0;
        qint64 maxGuiGap = 0;
        int maxGuiGapRows = 0;
        QTimer heartbeat;
        heartbeat.setInterval(1);
        connect(&heartbeat, &QTimer::timeout, &window, [&] {
            const qint64 now = guiTime.elapsed();
            if (now - lastHeartbeat > maxGuiGap) {
                maxGuiGap = now - lastHeartbeat;
                maxGuiGapRows = list->rowCount();
            }
            lastHeartbeat = now;
        });
        connect(list->model(), &QAbstractItemModel::rowsInserted, &window,
                [&](const QModelIndex &, int first, int last) {
                    if (elapsed.isValid()) {
                        lastRowMs = elapsed.elapsed();
                        if (firstRowMs < 0)
                            firstRowMs = lastRowMs;
                        publishedRows += last - first + 1;
                    }
                    if (scheduled)
                        return;
                    scheduled = true;
                    guiTime.start();
                    heartbeat.start();
                    QTimer::singleShot(0, &window, [&] {
                        heartbeatRows = list->rowCount();
                        panelSwitchBlocked = !action(window, "twoPanelsAction")->isEnabled();
                        const bool twoPanels = action(window, "twoPanelsAction")->isChecked();
                        const auto *previousPeer = window.findChild<QWidget *>("filePanel1");
                        const bool peerVisible = previousPeer && previousPeer->isVisible();
                        auto *address = window.findChild<QLineEdit *>("archivePath");
                        address->setFocus();
                        QTest::keyClick(address, Qt::Key_F9);
                        const auto *peer = window.findChild<QWidget *>("filePanel1");
                        panelShortcutIgnored =
                            action(window, "twoPanelsAction")->isChecked() == twoPanels &&
                            bool(peer && peer->isVisible()) == peerVisible;
                    });
                });
        elapsed.start();
        if (archive)
            openListedArchive(window, backend, fixture.filePath("fixture.zip"), entries);
        else {
            auto *address = window.findChild<QLineEdit *>("archivePath");
            address->setText(fixture.path());
            QTest::keyClick(address, Qt::Key_Return);
        }
        QTRY_VERIFY_WITH_TIMEOUT(heartbeatRows >= 0, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(list->isEnabled() && list->rowCount() >= count, 15000);
        QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
        QVERIFY(panelSwitchBlocked);
        QVERIFY(panelShortcutIgnored);
        QVERIFY(action(window, "twoPanelsAction")->isEnabled());
        const qint64 readyMs = elapsed.elapsed();
        if (guiTime.elapsed() - lastHeartbeat > maxGuiGap) {
            maxGuiGap = guiTime.elapsed() - lastHeartbeat;
            maxGuiGapRows = list->rowCount();
        }
        heartbeat.stop();
        QVERIFY(list->updatesEnabled());
        QVERIFY(UiInspection::fullyVisible(*list, window));
        QVERIFY(UiInspection::writeReport(
            window, QString("large-%1-%2").arg(archive ? "archive" : "disk").arg(size.width())));
        const qint64 reportMs = elapsed.elapsed() - readyMs;
        qInfo("full %s projection: %d items in %lld ms, heartbeat at %d rows, max GUI gap %lld ms",
              archive ? "archive" : "disk", count, elapsed.elapsed(), heartbeatRows, maxGuiGap);
        qInfo("commands ready after %lld ms; widget report took %lld ms", readyMs, reportMs);
        const QString reports = qEnvironmentVariable("SEVENZIP_UI_REPORT_DIR");
        if (!reports.isEmpty()) {
            QFile report(QDir(reports).filePath(QString("large-%1-%2-benchmark.json")
                                                    .arg(archive ? "archive" : "disk")
                                                    .arg(size.width())));
            QVERIFY(report.open(QIODevice::WriteOnly));
            const QJsonObject values{{"items", count},
                                     {"elapsedMs", elapsed.elapsed()},
                                     {"readyMs", readyMs},
                                     {"reportMs", reportMs},
                                     {"firstRowMs", firstRowMs},
                                     {"lastRowMs", lastRowMs},
                                     {"publishedRows", publishedRows},
                                     {"heartbeatRows", heartbeatRows},
                                     {"maxGuiGapMs", maxGuiGap},
                                     {"maxGuiGapRows", maxGuiGapRows}};
            QVERIFY(report.write(QJsonDocument(values).toJson()) > 0);
        }
        QVERIFY(elapsed.elapsed() < 15000);
        QVERIFY2(maxGuiGap < 250,
                 qPrintable(QString("GUI gap %1 ms at %2 rows").arg(maxGuiGap).arg(maxGuiGapRows)));
        QVERIFY2(
            heartbeatRows > 0 && heartbeatRows < count,
            qPrintable(QString("GUI resumed only at %1 rows of %2").arg(heartbeatRows).arg(count)));
        const QString selectedName = QStringLiteral("имя с пробелом 04095.txt");
        int selectedRow = -1;
        for (int row = 0; row < list->rowCount(); ++row)
            if (list->model()->index(row, 0).data().toString() == selectedName)
                selectedRow = row;
        QVERIFY(selectedRow >= 0);
        list->setCurrentCell(selectedRow, 0);
        list->selectRow(selectedRow);
        const int calls = backend.listCalls;
        QSignalSpy publication(list->model(), &QAbstractItemModel::rowsRemoved);
        action(window, "refreshAction")->trigger();
        QCOMPARE(backend.listCalls, calls);
        QTRY_VERIFY(!publication.isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(list->isEnabled() && list->rowCount() >= count, 15000);
        QCOMPARE(list->selectionModel()->selectedRows().size(), 1);
        QCOMPARE(list->currentIndex().data().toString(), selectedName);
        QCOMPARE(list->model()
                     ->index(list->selectionModel()->selectedRows().first().row(), 0)
                     .data()
                     .toString(),
                 selectedName);
    }
    void enteringLargeArchiveFolderRestoresCommands() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QList<ArchiveEntry> entries;
        for (int i = 0; i < 512; ++i)
            entries.append(ArchiveEntry{QString("folder/item-%1.txt").arg(i), 9, 5});
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        action(window, "viewMode3Action")->trigger();

        openListedArchive(window, backend, fixture.filePath("fixture.zip"), entries);
        auto *list = table(window);
        int folderRow = -1;
        for (int row = 0; row < list->rowCount(); ++row)
            if (list->model()->index(row, 0).data().toString() == "folder")
                folderRow = row;
        QVERIFY(folderRow >= 0);
        list->setCurrentCell(folderRow, 0);
        list->selectRow(folderRow);
        QTRY_VERIFY(action(window, "openItemAction")->isEnabled());
        action(window, "openItemAction")->trigger();
        QTRY_VERIFY(list->isEnabled() && list->rowCount() >= 512);
        QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
        const int calls = backend.listCalls;
        QSignalSpy publication(list->model(), &QAbstractItemModel::rowsRemoved);
        action(window, "refreshAction")->trigger();
        QCOMPARE(backend.listCalls, calls);
        QTRY_VERIFY(!publication.isEmpty());
        QTRY_VERIFY(list->isEnabled() && list->rowCount() >= 512);
    }
    void createFolderSelectionWaitsForCompleteNativeFolder() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        for (int i = 0; i < 512; ++i) {
            QFile file(fixture.filePath(QString("old-%1.txt").arg(i)));
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();

        auto *address = window.findChild<QLineEdit *>("archivePath");
        address->setText(fixture.path());
        QTest::keyClick(address, Qt::Key_Return);
        auto *list = table(window);
        QTRY_VERIFY(list->isEnabled() && list->rowCount() >= 512);
        const QString name = QStringLiteral("новая папка с пробелом");
        action(window, "createFolderAction")->trigger();
        auto *folderName = window.findChild<QLineEdit *>("inlineFolderName");
        QVERIFY(folderName && folderName->isVisible());
        folderName->setText(name);
        QTest::keyClick(folderName, Qt::Key_Return);
        QVERIFY(QFileInfo::exists(fixture.filePath(name)));
        QTRY_VERIFY(list->isEnabled() && list->rowCount() >= 513);
        QCOMPARE(list->selectionModel()->selectedRows().size(), 1);
        QCOMPARE(list->model()
                     ->index(list->selectionModel()->selectedRows().first().row(), 0)
                     .data()
                     .toString(),
                 name);
    }
    void switchPanelDuringRefreshKeepsOriginalSelectedState() {
        QTemporaryDir fixture;
        QTemporaryDir other;
        QVERIFY(fixture.isValid() && other.isValid());
        for (int i = 0; i < 512; ++i) {
            QFile file(fixture.filePath(QString("old-%1.txt").arg(i)));
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        SavePanelPath(1, ToUString(other.path()), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        action(window, "viewMode3Action")->trigger();

        action(window, "twoPanelsAction")->trigger();
        auto *left = window.findChild<QWidget *>("filePanel0");
        auto *right = window.findChild<QWidget *>("filePanel1");
        QVERIFY(left && right);
        auto *address = left->findChild<QLineEdit *>("archivePath");
        auto *list = left->findChild<FileTable *>("archiveTable");
        address->setFocus();
        address->setText(fixture.path());
        QTest::keyClick(address, Qt::Key_Return);
        QTRY_VERIFY(list->isEnabled() && list->rowCount() >= 512);
        const QString name = "old-511.txt";
        int row = -1;
        for (int i = 0; i < list->rowCount(); ++i)
            if (list->model()->index(i, 0).data().toString() == name)
                row = i;
        QVERIFY(row >= 0);
        list->setCurrentCell(row, 0);
        list->selectRow(row);
        bool switched = false;
        QTimer timer;
        connect(&timer, &QTimer::timeout, &window, [&] {
            if (list->rowCount() < 2 || list->rowCount() >= 512)
                return;
            timer.stop();
            switched = true;
            right->findChild<QLineEdit *>("archivePath")->setFocus();
        });
        timer.start(0);
        action(window, "refreshAction")->trigger();
        QTRY_VERIFY(switched);
        QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
        QTest::qWait(50);
        address->setFocus();
        QTRY_VERIFY(list->isEnabled() && list->rowCount() >= 512);
        QCOMPARE(list->selectionModel()->selectedRows().size(), 1);
        QCOMPARE(list->currentIndex().data().toString(), name);
        QCOMPARE(list->model()
                     ->index(list->selectionModel()->selectedRows().first().row(), 0)
                     .data()
                     .toString(),
                 name);
    }
    void navigationAndCloseDuringProjection_data() {
        QTest::addColumn<bool>("archive");
        QTest::addColumn<bool>("close");
        QTest::newRow("disk-navigate") << false << false;
        QTest::newRow("archive-navigate") << true << false;
        QTest::newRow("disk-close") << false << true;
        QTest::newRow("archive-close") << true << true;
    }
    void navigationAndCloseDuringProjection() {
        QFETCH(bool, archive);
        QFETCH(bool, close);
        QTemporaryDir fixture;
        QTemporaryDir destination;
        QVERIFY(fixture.isValid() && destination.isValid());
        QList<ArchiveEntry> entries;
        for (int i = 0; i < 512; ++i) {
            const QString name = QString("old-%1.txt").arg(i);
            if (archive) {
                ArchiveEntry entry;
                entry.path = QStringLiteral("inside/") + name;
                entry.size = entry.packedSize = 1;
                entries.append(std::move(entry));
            } else {
                QFile file(fixture.filePath(name));
                QVERIFY(file.open(QIODevice::WriteOnly));
                file.close();
            }
        }
        QFile target(destination.filePath("only.txt"));
        QVERIFY(target.open(QIODevice::WriteOnly));
        target.close();
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        action(window, "viewMode3Action")->trigger();

        auto *list = table(window);
        int archiveFolderRow = -1;
        if (archive) {
            openListedArchive(window, backend, fixture.filePath("fixture.zip"), entries);
            QTRY_VERIFY(list->isEnabled());
            const auto matches = list->model()->match(list->model()->index(0, 0), Qt::DisplayRole,
                                                      "inside", 1, Qt::MatchExactly);
            QCOMPARE(matches.size(), 1);
            archiveFolderRow = matches.front().row();
        }
        bool handled = false;
        QTimer timer;
        connect(&timer, &QTimer::timeout, &window, [&] {
            if (list->isEnabled() || !list->rowCount())
                return;
            const QPersistentModelIndex last = list->model()->index(list->rowCount() - 1, 0);
            if (!last.isValid() || !last.data(Qt::UserRole + 3).toString().startsWith("old-"))
                return;
            QVERIFY(list->rowCount() < 512);
            timer.stop();
            handled = true;
            if (close)
                QVERIFY(window.close());
            else {
                auto *address = window.findChild<QLineEdit *>("archivePath");
                address->setText(destination.path());
                QTest::keyClick(address, Qt::Key_Return);
            }
        });
        timer.start(0);
        if (archive)
            openRow(window, archiveFolderRow);
        else {
            auto *address = window.findChild<QLineEdit *>("archivePath");
            address->setText(fixture.path());
            QTest::keyClick(address, Qt::Key_Return);
        }
        QTRY_VERIFY_WITH_TIMEOUT(handled, 15000);
        const int rows = list->rowCount();
        QTest::qWait(50);
        QCOMPARE(list->rowCount(), rows);
        if (close)
            QVERIFY(!window.isVisible());
        else {
            QVERIFY(list->isEnabled());
            QVERIFY(list->updatesEnabled());
            QVERIFY(rows <= 2);
            QVERIFY(filesystemRow(window, "only.txt") >= 0);
        }
    }
};
QTEST_MAIN(LargeListWindowTests)
#include "large_list_window_tests.moc"
