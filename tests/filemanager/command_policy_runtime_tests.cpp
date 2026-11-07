#include "filemanager/support/panel_model_support.h"
#include "filemanager/support/window_state_support.h"

class CommandPolicyRuntimeTests : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
  private slots:
    void filesystemCannotForgeMountCapability() {
        QTemporaryDir fixture;
        ControlledBackend backend;
        MainWindow window(&backend);
        auto *address = window.findChild<QLineEdit *>("archivePath");
        address->setText(fixture.path());
        QTest::keyClick(address, Qt::Key_Return);
        auto *mount = action(window, "mountReadOnlyAction");
        QVERIFY(!mount->isEnabled());
        mount->setEnabled(true);
        mount->trigger();
        QVERIFY(!window.findChild<QDialog *>("archiveMountSettings"));
        QVERIFY(!window.findChild<QDialog *>("archiveMountManager"));
        QCOMPARE(address->text(), QFileInfo(fixture.path()).canonicalFilePath());
        QCOMPARE(backend.extractCalls, 0);
    }
    void insideUsesFocusedFolderRatherThanAnotherSelectedFile() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString directory = temp.filePath(QStringLiteral("папка внутри"));
        QVERIFY(QDir().mkpath(directory));
        QFile file(temp.filePath("selected.txt"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        ControlledBackend backend;
        int opened = 0;
        MainWindow window(&backend, [&](const QUrl &) {
            ++opened;
            return true;
        });
        auto *address = window.findChild<QLineEdit *>("archivePath");
        address->setText(temp.path());
        QTest::keyClick(address, Qt::Key_Return);
        auto *view = table(window);
        view->selectRow(filesystemRow(window, "selected.txt"));
        view->setCurrentCell(filesystemRow(window, QStringLiteral("папка внутри")), 0,
                             QItemSelectionModel::NoUpdate);
        auto *inside = action(window, "openInsideAction");
        QVERIFY(inside->isEnabled());
        inside->trigger();
        QCOMPARE(address->text(), QFileInfo(directory).canonicalFilePath());
        QVERIFY(QDir::fromNativeSeparators(window.windowTitle())
                    .startsWith(QFileInfo(directory).canonicalFilePath()));
        QCOMPARE(opened, 0);
        QCOMPARE(backend.listCalls, 0);
    }
    void typedReadOnlyAndEncryptedMetadataCannotBeBypassed_data() {
        QTest::addColumn<QString>("format");
        QTest::addColumn<bool>("encrypted");
        QTest::newRow("reader-without-writer") << QStringLiteral("rar") << false;
        QTest::newRow("encrypted-source-mutations") << QStringLiteral("7z") << true;
    }
    void typedReadOnlyAndEncryptedMetadataCannotBeBypassed() {
        QFETCH(QString, format);
        QFETCH(bool, encrypted);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        ControlledBackend backend;
        MainWindow window(&backend);
        const QString archive = temp.filePath("fixture.7z");
        QVERIFY(backend.prepareArchiveFixture(archive));
        backend.queueListAction([&] {
            ArchiveInfo info;
            info.format = format;
            info.encrypted = encrypted;
            backend.infoReady(info);
            backend.completeList({ArchiveEntry{"file.txt", 1, 1}});
        });
        window.openArchive(archive);
        // A later backend result must not replace the bound folder's policy.
        ArchiveInfo unrelated;
        unrelated.format = format;
        unrelated.encrypted = !encrypted;
        backend.infoReady(unrelated);
        table(window)->selectRow(0);
        QVERIFY(action(window, "editFileAction")->isEnabled());
        for (const char *name : {"moveAction"}) {
            auto *command = action(window, name);
            QVERIFY(!command->isEnabled());
            command->setEnabled(true);
            command->trigger();
        }
        for (const char *name : {"toolbarAddAction"}) {
            auto *command = action(window, name);
            QCOMPARE(command->isEnabled(), encrypted);
            if (!encrypted) {
                command->setEnabled(true);
                command->trigger();
            }
        }
        for (const char *name : {"deleteAction", "renameAction", "createFolderAction"})
            QCOMPARE(action(window, name)->isEnabled(), encrypted);

        QCOMPARE(backend.extractCalls, 0);
        QVERIFY(action(window, "copyAction")->isEnabled());
        QVERIFY(action(window, "selectedPropertiesAction")->isEnabled());
        QVERIFY(action(window, "extractArchiveAction")->isEnabled());
        QVERIFY(action(window, "testArchiveAction")->isEnabled());
    }
    void selectedNativeIndicesDriveEveryExternalOpenAndKeepSelection() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QStringList paths;
        for (const QString &name : {QStringLiteral("Я файл.txt"), QStringLiteral("A file.txt")}) {
            const QString path = temp.filePath(name);
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            paths.append(
                QFileInfo(path).canonicalFilePath().normalized(QString::NormalizationForm_C));
        }
        ControlledBackend backend;
        QStringList opened;
        MainWindow window(&backend, [&](const QUrl &url) {
            opened.append(url.toLocalFile().normalized(QString::NormalizationForm_C));
            return true;
        });
        auto *address = window.findChild<QLineEdit *>("archivePath");
        address->setText(temp.path());
        QTest::keyClick(address, Qt::Key_Return);
        auto *view = table(window);
        QCOMPARE(view->rowCount(), 2);
        view->selectAll();
        for (int row = 0; row < 2; ++row) {
            SetPanelData(view->model()->index(row, 0), "forged name", Qt::EditRole);
            SetPanelData(view->model()->index(row, 1), "/missing/forged path", Qt::EditRole);
        }
        auto *outside = action(window, "openOutsideAction");
        outside->trigger();
        opened.sort();
        paths.sort();
        QCOMPARE(opened, paths);
        QCOMPARE(view->selectionModel()->selectedRows().size(), 2);
    }
    void staleEnabledActionsCannotBypassBusyOwnerOrClosedLifecycle() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        ControlledBackend backend;
        int opened = 0;
        MainWindow window(&backend, [&](const QUrl &) {
            ++opened;
            return true;
        });
        openListedArchive(window, backend, temp.filePath("busy архив.7z"),
                          {ArchiveEntry{"file.txt", 1, 1}});
        table(window)->selectRow(0);
        action(window, "testArchiveAction")->trigger();
        QCOMPARE(backend.testCalls, 1);
        const bool twoPanels = action(window, "twoPanelsAction")->isChecked();
        for (const char *name :
             {"twoPanelsAction", "testArchiveAction", "openOutsideAction", "createFolderAction",
              "refreshAction", "copyAction", "selectAllAction"}) {
            auto *command = action(window, name);
            QVERIFY(!command->isEnabled());
            command->setEnabled(true); // A stale queued trigger must recheck CApp.
            command->trigger();
        }
        QCOMPARE(backend.testCalls, 1);
        QCOMPARE(backend.listCalls, 1);
        QCOMPARE(opened, 0);
        QCOMPARE(action(window, "twoPanelsAction")->isChecked(), twoPanels);
        QVERIFY(!window.findChild<QDialog *>("comboDialog"));
        backend.completeExtraction();
        auto *outside = action(window, "openOutsideAction");
        QVERIFY(outside->isEnabled());
        auto *panels = action(window, "twoPanelsAction");
        QVERIFY(panels->isEnabled());
        panels->trigger();
        QCOMPARE(panels->isChecked(), !twoPanels);
        QVERIFY(window.close());
        outside->setEnabled(true);
        outside->trigger();
        QCOMPARE(opened, 0);
    }
};
QTEST_MAIN(CommandPolicyRuntimeTests)
#include "command_policy_runtime_tests.moc"
