#include "archive/support/native_archive_fixture.h"
#include "filemanager/support/panel_model_support.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "support/panel_keys.h"
#include "support/window_activation.h"
#include "ui/support/layout_inspection.h"

class PanelFolderSyncTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    static QAction *action(MainWindow &window, const char *name) {
        return window.findChild<QAction *>(name);
    }
  private slots:
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }
    void altArrowPublishesOtherFolderWithoutMovingFocus_data() {
        QTest::addColumn<int>("key");
        QTest::addColumn<bool>("same");
        QTest::addColumn<int>("items");
        QTest::addColumn<unsigned>("sourcePane");
        for (unsigned pane : {0u, 1u}) {
            QTest::newRow(qPrintable(QString("same-small-%1").arg(pane)))
                << int(Qt::Key_Up) << true << 3 << pane;
            QTest::newRow(qPrintable(QString("same-large-%1").arg(pane)))
                << int(Qt::Key_Up) << true << 512 << pane;
            QTest::newRow(qPrintable(QString("sub-left-small-%1").arg(pane)))
                << int(Qt::Key_Left) << false << 3 << pane;
            QTest::newRow(qPrintable(QString("sub-right-large-%1").arg(pane)))
                << int(Qt::Key_Right) << false << 512 << pane;
        }
    }
    void altArrowPublishesOtherFolderWithoutMovingFocus() {
        QFETCH(int, key);
        QFETCH(bool, same);
        QFETCH(int, items);
        QFETCH(unsigned, sourcePane);
        {
            QSettings settings(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"),
                               QSettings::IniFormat);
            settings.setValue("FM/ShowDots", true);
        }
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath(QStringLiteral("исходная 空 папка"));
        const QString child = QDir(source).filePath(QStringLiteral("вложенная 空 папка"));
        const QString originalDestination = fixture.filePath("destination");
        QVERIFY(QDir().mkpath(child));
        QVERIFY(QDir().mkpath(originalDestination));
        const QString expected = same ? source : child;
        for (int i = 0; i < items; ++i) {
            QFile file(QDir(expected).filePath(QStringLiteral("файл %1.txt").arg(i)));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write("bytes"), qint64(5));
        }
        SavePanelPath(1 - sourcePane, ToUString(originalDestination),
                      qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        action(window, "twoPanelsAction")->trigger();
        auto *left = window.findChild<QWidget *>(QString("filePanel%1").arg(sourcePane));
        auto *sourceAddress = left->findChild<QLineEdit *>("archivePath");
        auto *sourceTable = left->findChild<FileTable *>("archiveTable");
        PanelKeys::focusAddress(sourceAddress);
        QTRY_VERIFY(sourceAddress->hasFocus());
        sourceAddress->setText(source);
        QTest::keyClick(sourceAddress, Qt::Key_Return);
        QTRY_VERIFY(sourceTable->isEnabled());
        auto *right = window.findChild<QWidget *>(QString("filePanel%1").arg(1 - sourcePane));
        QVERIFY(right);
        auto *destinationAddress = right->findChild<QLineEdit *>("archivePath");
        auto *destinationTable = right->findChild<FileTable *>("archiveTable");
        int childRow = -1;
        for (int row = 0; row < sourceTable->rowCount(); ++row)
            if (sourceTable->model()->index(row, 0).data().toString() ==
                QFileInfo(child).fileName())
                childRow = row;
        QVERIFY(childRow >= 0);
        sourceTable->setCurrentCell(childRow, 0);
        sourceTable->setFocus();
        QTRY_VERIFY(sourceTable->hasFocus());
        QTest::keyClick(sourceTable, Qt::Key(key), Qt::AltModifier | Qt::ShiftModifier);
        QTRY_COMPARE(QDir::cleanPath(destinationAddress->text()), expected);
        QTRY_VERIFY(destinationTable->isEnabled());
        const int expectedRows = items + (same ? 2 : 1); // child and parent rows
        QTRY_COMPARE(destinationTable->rowCount(), expectedRows);
        QTRY_VERIFY(destinationTable->currentIndex().isValid());
        QVERIFY(
            destinationTable->selectionModel()->isRowSelected(destinationTable->currentRow(), {}));
        QCOMPARE(QDir::cleanPath(sourceAddress->text()), source);
        QVERIFY(sourceTable->hasFocus());
        QVERIFY(destinationTable->updatesEnabled());
        const auto *sourcePanel = static_cast<FileTable *>(sourceTable)->sourcePanel;
        const auto *destinationPanel = static_cast<FileTable *>(destinationTable)->sourcePanel;
        QCOMPARE(QDir::cleanPath(ToQString(sourcePanel->GetCurrentFolderPrefix())), source);
        QCOMPARE(QDir::cleanPath(ToQString(destinationPanel->GetCurrentFolderPrefix())), expected);
        for (int width : {980, 640}) {
            window.resize(width, width == 980 ? 620 : 400);
            QApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(*PanelKeys::navigationFor(sourceAddress), window));
            QVERIFY(
                UiInspection::fullyVisible(*PanelKeys::navigationFor(destinationAddress), window));
            QVERIFY(UiInspection::fullyVisible(*sourceTable, window));
            QVERIFY(UiInspection::fullyVisible(*destinationTable, window));
            QVERIFY(UiInspection::writeReport(
                window,
                QStringLiteral("alt-folder-%1-%2").arg(QTest::currentDataTag()).arg(width)));
        }
        destinationTable->setFocus();
        QTRY_VERIFY(destinationTable->hasFocus());
        const auto file = PanelMatches(*destinationTable, QStringLiteral("файл 0.txt"));
        QVERIFY(!file.isEmpty());
        destinationTable->selectRow(file.first().row());
        QTRY_VERIFY(action(window, "copyAction")->isEnabled());
        sourceTable->setFocus();
        QTRY_VERIFY(sourceTable->hasFocus());
    }
    void noOpConditions_data() {
        QTest::addColumn<bool>("twoPanels");
        QTest::addColumn<bool>("focusedFile");
        QTest::addColumn<int>("key");
        QTest::newRow("single-same") << false << true << int(Qt::Key_Up);
        QTest::newRow("single-sub") << false << true << int(Qt::Key_Right);
        QTest::newRow("no-focused-item") << true << false << int(Qt::Key_Left);
        QTest::newRow("file-left") << true << true << int(Qt::Key_Left);
        QTest::newRow("file-right") << true << true << int(Qt::Key_Right);
    }
    void noOpConditions() {
        QFETCH(bool, twoPanels);
        QFETCH(bool, focusedFile);
        QFETCH(int, key);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QFile file(fixture.filePath(QStringLiteral("обычный 空 файл.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("bytes"), qint64(5));
        file.close();
        const QString destination = fixture.filePath("destination");
        QVERIFY(QDir().mkdir(destination));
        SavePanelPath(1, ToUString(destination), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        auto *left = window.findChild<QWidget *>("filePanel0");
        auto *address = left->findChild<QLineEdit *>("archivePath");
        auto *table = left->findChild<FileTable *>("archiveTable");
        PanelKeys::focusAddress(address);
        address->setText(fixture.path());
        QTest::keyClick(address, Qt::Key_Return);
        if (twoPanels)
            action(window, "twoPanelsAction")->trigger();
        table->setFocus();
        QTRY_VERIFY(table->hasFocus());
        const auto matches = PanelMatches(*table, QFileInfo(file).fileName());
        QVERIFY(!matches.isEmpty());
        table->setCurrentCell(focusedFile ? matches.first().row() : -1, focusedFile ? 0 : -1);
        QCOMPARE(table->currentIndex().isValid(), focusedFile);
        QTest::keyClick(table, Qt::Key(key), Qt::AltModifier | Qt::ShiftModifier);
        QCOMPARE(QDir::cleanPath(address->text()), fixture.path());
        QVERIFY(table->hasFocus());
        if (twoPanels) {
            const auto *right = window.findChild<QWidget *>("filePanel1");
            QCOMPARE(QDir::cleanPath(right->findChild<QLineEdit *>("archivePath")->text()),
                     destination);
        }
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("bytes"));
    }
    void parentAndVirtualRoot_data() {
        QTest::addColumn<bool>("root");
        QTest::newRow("parent") << false;
        QTest::newRow("virtual-root") << true;
    }
    void parentAndVirtualRoot() {
        QFETCH(bool, root);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath("child");
        QVERIFY(QDir().mkdir(source));
        QSettings settings(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"), QSettings::IniFormat);
        settings.setValue("FM/ShowDots", true);
        settings.sync();
        SavePanelPath(1, ToUString(source), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        auto *left = window.findChild<QWidget *>("filePanel0");
        auto *address = left->findChild<QLineEdit *>("archivePath");
        auto *table = left->findChild<FileTable *>("archiveTable");
        PanelKeys::focusAddress(address);
        address->setText(source);
        QTest::keyClick(address, Qt::Key_Return);
        action(window, "twoPanelsAction")->trigger();
        table->setFocus();
        QTRY_VERIFY(table->hasFocus());
        if (root) {
            action(window, "openRootAction")->trigger();
            action(window, "parentFolderAction")->trigger();
        } else {
            const auto dots = PanelMatches(*table, "..");
            QVERIFY(!dots.isEmpty());
            table->setCurrentCell(dots.first().row(), 0);
        }
        table->setFocus();
        QTRY_VERIFY(table->hasFocus());
        QTest::keyClick(table, root ? Qt::Key_Up : Qt::Key_Right,
                        Qt::AltModifier | Qt::ShiftModifier);
        auto *right = window.findChild<QWidget *>("filePanel1");
        auto *destinationAddress = right->findChild<QLineEdit *>("archivePath");
        auto *destinationTable = right->findChild<FileTable *>("archiveTable");
        QTRY_COMPARE(destinationAddress->text(), root ? QString() : fixture.path());
        QVERIFY(table->hasFocus());
        QVERIFY(destinationTable->isEnabled());
        if (root) {
            QVERIFY(static_cast<FileTable *>(destinationTable)->sourcePanel->IsRootFolder());
            QCOMPARE(destinationTable->rowCount(), table->rowCount());
            for (int row = 0; row < table->rowCount(); ++row)
                QCOMPARE(destinationTable->model()->index(row, 0).data().toString(),
                         table->model()->index(row, 0).data().toString());
        }
    }
    void replacementCancelsOnlyDestinationPublication_data() {
        QTest::addColumn<bool>("closeInstead");
        QTest::newRow("replace") << false;
        QTest::newRow("close") << true;
    }
    void replacementCancelsOnlyDestinationPublication() {
        QFETCH(bool, closeInstead);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString first = fixture.filePath(QStringLiteral("первая 空 папка"));
        const QString second = fixture.filePath(QStringLiteral("вторая 空 папка"));
        QVERIFY(QDir().mkdir(first));
        QVERIFY(QDir().mkdir(second));
        for (int i = 0; i < 512; ++i) {
            QFile file(QDir(first).filePath(QStringLiteral("old %1.txt").arg(i)));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write("preserved"), qint64(9));
        }
        QFile file(QDir(second).filePath("new.txt"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        SavePanelPath(1, ToUString(second), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        auto *left = window.findChild<QWidget *>("filePanel0");
        auto *address = left->findChild<QLineEdit *>("archivePath");
        auto *table = left->findChild<FileTable *>("archiveTable");
        PanelKeys::focusAddress(address);
        address->setText(fixture.path());
        QTest::keyClick(address, Qt::Key_Return);
        action(window, "twoPanelsAction")->trigger();
        table->setFocus();
        QTRY_VERIFY(table->hasFocus());
        const auto oldFolder = PanelMatches(*table, QFileInfo(first).fileName());
        const auto newFolder = PanelMatches(*table, QFileInfo(second).fileName());
        QVERIFY(!oldFolder.isEmpty());
        QVERIFY(!newFolder.isEmpty());
        table->setCurrentCell(oldFolder.first().row(), 0);
        QTest::keyClick(table, Qt::Key_Right, Qt::AltModifier | Qt::ShiftModifier);
        auto *right = window.findChild<QWidget *>("filePanel1");
        auto *destinationAddress = right->findChild<QLineEdit *>("archivePath");
        auto *destinationTable = right->findChild<FileTable *>("archiveTable");
        QVERIFY(!destinationTable->isEnabled());
        QVERIFY(table->isEnabled());
        QVERIFY(table->hasFocus());
        if (!closeInstead) {
            table->setCurrentCell(newFolder.first().row(), 0);
            QTest::keyClick(table, Qt::Key_Left, Qt::AltModifier | Qt::ShiftModifier);
            QTRY_COMPARE(destinationAddress->text(), second);
            QTRY_VERIFY(destinationTable->isEnabled());
            QTRY_COMPARE(destinationTable->rowCount(), 1);
            QTest::qWait(100); // deliver any canceled worker's queued completion
            QCOMPARE(destinationTable->rowCount(), 1);
            QCOMPARE(destinationTable->model()->index(0, 0).data().toString(),
                     QStringLiteral("new.txt"));
            QCOMPARE(address->text(), fixture.path());
            QVERIFY(table->hasFocus());
        }
        QVERIFY(window.close());
        QTest::qWait(100);
        QFile preserved(QDir(first).filePath("old 0.txt"));
        QVERIFY(preserved.open(QIODevice::ReadOnly));
        QCOMPARE(preserved.readAll(), QByteArray("preserved"));
    }
    void refusedSubfolderKeepsDestination() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString missing = fixture.filePath(QStringLiteral("исчезнувшая 空 папка"));
        const QString destination = fixture.filePath("destination");
        QVERIFY(QDir().mkdir(missing));
        QVERIFY(QDir().mkdir(destination));
        SavePanelPath(1, ToUString(destination), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        auto *left = window.findChild<QWidget *>("filePanel0");
        auto *address = left->findChild<QLineEdit *>("archivePath");
        auto *table = left->findChild<FileTable *>("archiveTable");
        PanelKeys::focusAddress(address);
        address->setText(fixture.path());
        QTest::keyClick(address, Qt::Key_Return);
        action(window, "twoPanelsAction")->trigger();
        table->sourcePanel->Set_AutoRefresh_Mode(false);
        const auto matches = PanelMatches(*table, QFileInfo(missing).fileName());
        QVERIFY(!matches.isEmpty());
        table->setCurrentCell(matches.first().row(), 0);
        table->setFocus();
        QTRY_VERIFY(table->hasFocus());
        QVERIFY(QDir().rmdir(missing));
        QTest::keyClick(table, Qt::Key_Right, Qt::AltModifier | Qt::ShiftModifier);
        auto *right = window.findChild<QWidget *>("filePanel1");
        auto *destinationTable = right->findChild<FileTable *>("archiveTable");
        QCOMPARE(right->findChild<QLineEdit *>("archivePath")->text(), destination);
        QCOMPARE(
            QDir::cleanPath(ToQString(
                static_cast<FileTable *>(destinationTable)->sourcePanel->GetCurrentFolderPrefix())),
            destination);
        QVERIFY(destinationTable->isEnabled());
        QVERIFY(destinationTable->updatesEnabled());
        QVERIFY(table->hasFocus());
        QCOMPARE(address->text(), fixture.path());
    }
    void archiveSubfolderRetainsProviderAfterSourceLeaves_data() {
        QTest::addColumn<int>("items");
        QTest::newRow("small") << 3;
        QTest::newRow("large") << 512;
    }
    void archiveSubfolderRetainsProviderAfterSourceLeaves() {
        QFETCH(int, items);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        SavePanelPath(1, ToUString(fixture.path()), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        const QString archive = fixture.filePath(QStringLiteral("архив 空 with spaces.zip"));
        const QString folder = QStringLiteral("папка 空");
        QList<NativeArchiveFixture::Item> entries;
        for (int i = 0; i < items; ++i) {
            NativeArchiveFixture::Item item;
            item.Path = folder + QStringLiteral("/file %1.txt").arg(i);
            entries.append(item);
        }
        QVERIFY(NativeArchiveFixture::Write(archive, entries, "zip"));
        ArchiveBackend backend;
        QCOMPARE(NativeArchiveFixture::Attach(backend), S_OK);
        MainWindow window(&backend);
        window.show();
        window.openArchive(archive);
        action(window, "twoPanelsAction")->trigger();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *left = window.findChild<QWidget *>("filePanel0");
        auto *address = left->findChild<QLineEdit *>("archivePath");
        auto *table = left->findChild<FileTable *>("archiveTable");
        const auto matches = PanelMatches(*table, folder);
        QVERIFY(!matches.isEmpty());
        QVERIFY(table->isVisible());
        QVERIFY(table->isEnabled());
        table->setCurrentCell(matches.first().row(), 0);
        table->setFocus();
        QTRY_VERIFY(table->hasFocus());
        QTest::keyClick(table, Qt::Key_Right, Qt::AltModifier | Qt::ShiftModifier);
        auto *right = window.findChild<QWidget *>("filePanel1");
        auto *destinationTable = right->findChild<FileTable *>("archiveTable");
        QTRY_VERIFY(destinationTable->isEnabled());
        QTRY_COMPARE(destinationTable->rowCount(), items);
        QTRY_VERIFY(destinationTable->currentIndex().isValid());
        QVERIFY(destinationTable->updatesEnabled());
        QTRY_COMPARE(window.focusWidget(), table);
        QVERIFY(WindowActivation::waitForActive(window));
        QVERIFY(table->hasFocus());
        auto *destinationPanel = static_cast<FileTable *>(destinationTable)->sourcePanel;
        QVERIFY(destinationPanel->IsArcFolder());
        QCOMPARE(
            QDir::fromNativeSeparators(ToQString(GetFolderPath(destinationPanel->GetFolder()))),
            folder + '/');
        auto *destinationAddress = right->findChild<QLineEdit *>("archivePath");
        const QString destinationPath = destinationAddress->text();
        QVERIFY(destinationPath.contains(QFileInfo(archive).fileName()));
        QVERIFY(destinationPath.endsWith(folder));
        QCOMPARE(address->text(), archive);
        for (int width : {980, 640}) {
            window.resize(width, width == 980 ? 620 : 400);
            QApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(*PanelKeys::navigationFor(address), window));
            QVERIFY(
                UiInspection::fullyVisible(*PanelKeys::navigationFor(destinationAddress), window));
            QVERIFY(UiInspection::fullyVisible(*destinationTable, window));
            QVERIFY(UiInspection::writeReport(
                window,
                QStringLiteral("alt-archive-%1-%2").arg(QTest::currentDataTag()).arg(width)));
        }
        PanelKeys::focusAddress(address);
        address->setText(fixture.path());
        QTest::keyClick(address, Qt::Key_Return);
        QTRY_VERIFY(!static_cast<FileTable *>(table)->sourcePanel->IsArcFolder());
        UInt32 retainedCount = 0;
        QCOMPARE(destinationPanel->GetFolder()->GetNumberOfItems(&retainedCount), S_OK);
        QCOMPARE(retainedCount, UInt32(items));
        QCOMPARE(destinationTable->rowCount(), items);
        QCOMPARE(destinationAddress->text(), destinationPath);
        QVERIFY(window.close());
    }
};
QTEST_MAIN(PanelFolderSyncTests)
#include "panel_folder_sync_tests.moc"
