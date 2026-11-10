#include "filemanager/support/panel_model_support.h"
#include "filemanager/support/window_state_support.h"
#include <QScopeGuard>

class CommandPolicyRuntimeTests : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
  private slots:
    void filesystemRootParentOnlyOpensInside() {
        QSettings settings(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"), QSettings::IniFormat);
        const auto previous = settings.value("FM/ShowDots");
        const auto restore = qScopeGuard([&] {
            if (previous.isValid())
                settings.setValue("FM/ShowDots", previous);
            else
                settings.remove("FM/ShowDots");
        });
        settings.setValue("FM/ShowDots", true);
        settings.sync();
        ControlledBackend backend;
        int opened = 0;
        MainWindow window(&backend, [&](const QUrl &) {
            ++opened;
            return true;
        });
        window.show();
        window.openPath(QDir::rootPath());
        auto *view = table(window);
        QTRY_VERIFY(view->isEnabled() && view->rowCount() > 0);
        QCOMPARE(view->model()->index(0, 0).data().toString(), QString(".."));
        view->setCurrentCell(0, 0);
        view->selectRow(0);
        view->setFocus();
        QVERIFY(action(window, "openItemAction")->isEnabled());
        QVERIFY(!action(window, "openOutsideAction")->isEnabled());
        QTest::keyClick(view, Qt::Key_Return, Qt::ShiftModifier);
        QCOMPARE(opened, 0);
        QVERIFY(view->sourcePanel->IsFSFolder());
    }

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
        QVERIFY(!action(window, "deleteAction")->isEnabled());
        for (const char *name : {"renameAction", "createFolderAction"})
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
        const QString folder = temp.filePath(QStringLiteral("Папка 空"));
        QVERIFY(QDir().mkdir(folder));
        paths.append(QFileInfo(folder).canonicalFilePath().normalized(QString::NormalizationForm_C));
        ControlledBackend backend;
        QStringList opened;
        MainWindow window(&backend, [&](const QUrl &url) {
            opened.append(
                QDir::cleanPath(url.toLocalFile()).normalized(QString::NormalizationForm_C));
            return true;
        });
        auto *address = window.findChild<QLineEdit *>("archivePath");
        address->setText(temp.path());
        QTest::keyClick(address, Qt::Key_Return);
        auto *view = table(window);
        QCOMPARE(view->rowCount(), paths.size());
        view->selectAll();
        for (int row = 0; row < view->rowCount(); ++row) {
            SetPanelData(view->model()->index(row, 0), "forged name", Qt::EditRole);
            SetPanelData(view->model()->index(row, 1), "/missing/forged path", Qt::EditRole);
        }
        auto *outside = action(window, "openOutsideAction");
        outside->trigger();
        opened.sort();
        paths.sort();
        QCOMPARE(opened, paths);
        QCOMPARE(view->selectionModel()->selectedRows().size(), paths.size());
        QCOMPARE(QDir(address->text()).canonicalPath(), QDir(temp.path()).canonicalPath());
    }
    void archiveFolderDisablesExternalOpen() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        ControlledBackend backend;
        QStringList opened;
        MainWindow window(&backend, [&](const QUrl &url) {
            opened.append(url.toLocalFile());
            return true;
        });
        window.show();
        openListedArchive(
            window, backend, temp.filePath("folders.7z"),
            {ArchiveEntry{"folder/child.txt", 1, 1}, ArchiveEntry{"document.txt", 1, 1}});
        auto *view = table(window);
        QTRY_COMPARE(view->rowCount(), 2);
        const auto folder = view->model()->match(view->model()->index(0, 0), Qt::DisplayRole,
                                                 "folder", 1, Qt::MatchExactly);
        QCOMPARE(folder.size(), 1);
        const int folderRow = folder.front().row();
        auto *outside = action(window, "openOutsideAction");
        view->setFocus();
        view->selectRow(folderRow);
        QVERIFY(!outside->isEnabled());
        outside->setEnabled(true);
        outside->trigger();
        QTest::keyClick(view, Qt::Key_Return, Qt::ShiftModifier);
        QVERIFY(opened.isEmpty());
        view->selectAll();
        QVERIFY(!outside->isEnabled());
        QTest::keyClick(view, Qt::Key_Return, Qt::ShiftModifier);
        QVERIFY(opened.isEmpty());
        QCOMPARE(backend.extractCalls, 0);
        const auto file = view->model()->match(view->model()->index(0, 0), Qt::DisplayRole,
                                               "document.txt", 1, Qt::MatchExactly);
        QCOMPARE(file.size(), 1);
        view->selectionModel()->select(file.front(), QItemSelectionModel::ClearAndSelect |
                                                         QItemSelectionModel::Rows);
        QCOMPARE(view->selectionModel()->selectedRows(), QModelIndexList{file.front()});
        QTRY_VERIFY(outside->isEnabled());
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
        auto *view = table(window);
        const auto file = view->model()->match(view->model()->index(0, 0), Qt::DisplayRole,
                                               "file.txt", 1, Qt::MatchExactly);
        QCOMPARE(file.size(), 1);
        view->selectionModel()->select(file.front(), QItemSelectionModel::ClearAndSelect |
                                                         QItemSelectionModel::Rows);
        QVERIFY(action(window, "openOutsideAction")->isEnabled());
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
        QTRY_VERIFY(outside->isEnabled());
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
