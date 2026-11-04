#include "dialogs/copy/alias.h"
#include "filemanager/support/window_state_support.h"
#include "localization/LangUtils.h"
#include "settings/RegistryUtils.h"
#include "support/panel_keys.h"
#include "ui/progress/ProgressDialog2.h"
#include "ui/progress/qtprogresscontrols.h"
#include "ui/support/address_popup_support.h"
#include "ui/support/layout_inspection.h"
#include <QProgressBar>
#include <QScopeGuard>
#include <QTreeWidget>
#ifdef Q_OS_WIN
#include "platform/support/native_properties_fixture.h"
#endif

class MainWindowArchiveStatesTests : public QObject, protected MainWindowTestSupport {
    Q_OBJECT

    static QString messageText(const QTreeWidget *list) {
        QString text;
        for (int i = 0; i < list->topLevelItemCount(); ++i)
            text += list->topLevelItem(i)->text(1) + '\n';
        return text;
    }

  private slots:
    void init() {
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        SaveRegLang(UString("-"), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
    }
    void sourceToolbarCancelAndExtractionAdapterCancellation() {
        ControlledBackend backend;
        MainWindow window(&backend);
        openListedArchive(window, backend, "archive.zip",
                          {ArchiveEntry{QStringLiteral("папка/δ space.txt"), 3, 3}});
        QVERIFY(table(window)->selectionModel()->selectedRows().isEmpty());
        QVERIFY(action(window, "copyAction")->isEnabled());
        QCOMPARE(backend.extractCalls, 0);
        table(window)->setCurrentCell(0, 0);
        bool dialogSeen = false;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = dynamic_cast<CopyDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            dialogSeen = true;
            auto *destination = dialog->findChild<QComboBox *>("copyDestination");
            QVERIFY(destination);
            destination->setCurrentText("/temporary/selected output");
            dialog->reject();
        });
        action(window, "extractArchiveAction")->trigger();
        QVERIFY(dialogSeen);
        QCOMPARE(backend.extractCalls, 0);
        QVERIFY(action(window, "extractArchiveAction")->isEnabled());
        ExtractionOptions options;
        options.pathMode = ExtractionOptions::PathMode::NoPaths;
        options.overwriteMode = ExtractionOptions::OverwriteMode::SkipExisting;
        window.extractTo("/temporary/selected output", false, "temporary secret", options);
        QCOMPARE(backend.extractCalls, 1);
        QCOMPARE(backend.lastExtractPassword, QString("temporary secret"));
        QCOMPARE(backend.lastExtractionOptions.pathMode, ExtractionOptions::PathMode::NoPaths);
        QCOMPARE(backend.lastExtractionOptions.overwriteMode,
                 ExtractionOptions::OverwriteMode::SkipExisting);
        QVERIFY(!action(window, "extractArchiveAction")->isEnabled());
        backend.cancel();
        QVERIFY(action(window, "extractArchiveAction")->isEnabled());
    }

    void launchFailure() {
        ArchiveBackend backend(nullptr, "/definitely/missing/7z");
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QFile archive(temporary.filePath("missing.7z"));
        QVERIFY(archive.open(QIODevice::WriteOnly));
        archive.close();
        MainWindow window(&backend);
        QTimer timer;
        connect(&timer, &QTimer::timeout, &window, [] { dismissWarnings(); });
        timer.start(10);
        window.openArchive(archive.fileName());
        QTRY_VERIFY_WITH_TIMEOUT(window.statusBar()->currentMessage().contains("7z"), 3000);
        QCOMPARE(window.findChild<QLineEdit *>("archivePath")->text(),
                 QFileInfo(QDir::currentPath()).canonicalFilePath());
        QVERIFY(!action(window, "backAction")->isEnabled());
        QVERIFY(!action(window, "openItemAction")->isEnabled());
        QCOMPARE(action(window, "selectAllAction")->isEnabled(), table(window)->rowCount() > 0);
        QVERIFY(action(window, "toolbarAddAction")->isEnabled());
        QVERIFY(!action(window, "extractArchiveAction")->isEnabled());
        QVERIFY(action(window, "parentFolderAction")->isEnabled());
        QVERIFY(!action(window, "testArchiveAction")->isEnabled());
        QVERIFY(!action(window, "archiveInfoAction")->isEnabled());
        QVERIFY(!backend.busy());
    }

    void filesystemNavigationAndArchiveReturn() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QVERIFY(QDir(temp.path()).mkdir(QStringLiteral("папка с пробелом")));
        QFile archive(temp.filePath(QStringLiteral("тест архив.zip")));
        QVERIFY(archive.open(QIODevice::WriteOnly));
        archive.close();
        ControlledBackend backend;
        MainWindow window(&backend);
        auto *address = window.findChild<QLineEdit *>("archivePath");
        QVERIFY(address);
        address->setText(temp.path());
        QTest::keyClick(address, Qt::Key_Return);
        QCOMPARE(address->text(), QFileInfo(temp.path()).canonicalFilePath());
        QCOMPARE(table(window)->rowCount(), 2);
        QVERIFY(action(window, "backAction")->isEnabled());
        int folderRow = -1;
        int archiveRow = -1;
        for (int row = 0; row < table(window)->rowCount(); ++row) {
            const QString name = table(window)->model()->index(row, 0).data().toString();
            if (name == QStringLiteral("папка с пробелом"))
                folderRow = row;
            if (name == QStringLiteral("тест архив.zip"))
                archiveRow = row;
        }
        QVERIFY(folderRow >= 0);
        QVERIFY(archiveRow >= 0);
        QVERIFY(QMetaObject::invokeMethod(
            table(window), "doubleClicked", Qt::DirectConnection,
            Q_ARG(QModelIndex, table(window)->model()->index(folderRow, 0))));
        QCOMPARE(address->text(),
                 QFileInfo(temp.filePath(QStringLiteral("папка с пробелом"))).canonicalFilePath());
        QCOMPARE(table(window)->rowCount(), 0);
        action(window, "backAction")->trigger();
        QCOMPARE(address->text(), QFileInfo(temp.path()).canonicalFilePath());
        backend.queueListAction([&] {
            const auto finish = qScopeGuard([&] { backend.completeList({}); });
            QVERIFY(!action(window, "backAction")->isEnabled());
        });
        QVERIFY(QMetaObject::invokeMethod(
            table(window), "doubleClicked", Qt::DirectConnection,
            Q_ARG(QModelIndex, table(window)->model()->index(archiveRow, 0))));
        QCOMPARE(backend.listCalls, 1);
        QCOMPARE(QDir::fromNativeSeparators(backend.lastArchive),
                 QFileInfo(archive.fileName()).canonicalFilePath());
        QCOMPARE(table(window)->rowCount(), 0);
        QVERIFY(action(window, "backAction")->isEnabled());
        action(window, "backAction")->trigger();
        QCOMPARE(address->text(), QFileInfo(temp.path()).canonicalFilePath());
        QCOMPARE(table(window)->rowCount(), 2);
        QVERIFY(!action(window, "extractArchiveAction")->isEnabled());
    }

    void cancellation() {
        ControlledBackend backend;
        MainWindow window(&backend);
        QVERIFY(backend.prepareArchiveFixture("cancel.7z"));
        QTimer responder;
        connect(&responder, &QTimer::timeout, &window, [] {
            if (auto *question = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                question && question->objectName() == "progressCancelQuestion")
                question->button(QMessageBox::Yes)->click();
        });
        responder.start(1);
        backend.queueListAction([&] {
            auto *progress = dynamic_cast<CQtProgressDialog *>(QApplication::activeModalWidget());
            if (!progress) {
                backend.cancel();
                QFAIL("Native archive open progress dialog is missing");
            }
            auto *button = progress->findChild<QPushButton *>("IDCANCEL");
            if (!button) {
                backend.cancel();
                QFAIL("Native archive open cancel control is missing");
            }
            button->click();
        });
        window.openArchive("cancel.7z");
        responder.stop();
        QCOMPARE(backend.cancelCalls, 1);
        QVERIFY(action(window, "parentFolderAction")->isEnabled());
        QVERIFY(!action(window, "openItemAction")->isEnabled());
        QCOMPARE(window.findChild<QLineEdit *>("archivePath")->text(),
                 QFileInfo(QDir::currentPath()).canonicalFilePath());
        QVERIFY(action(window, "toolbarAddAction")->isEnabled());
        QVERIFY(!action(window, "extractArchiveAction")->isEnabled());
        QCOMPARE(action(window, "selectAllAction")->isEnabled(), table(window)->rowCount() > 0);
        QCOMPARE(window.statusBar()->currentMessage(), QString("Operation canceled."));
        openListedArchive(window, backend, "after-cancel.zip", {});
        QCOMPARE(backend.listCalls, 2);
    }

    void operationCancellationKeepsArchive() {
        ControlledBackend backend;
        MainWindow window(&backend);
        openListedArchive(window, backend, "opened.7z",
                          {ArchiveEntry{QStringLiteral("файл.txt"), 4, 4}});
        QCoreApplication::processEvents();
        window.extractTo("/temporary/destination");
        QCOMPARE(backend.extractCalls, 1);
        QVERIFY(!action(window, "toolbarAddAction")->isEnabled());
        QVERIFY(!action(window, "extractArchiveAction")->isEnabled());
        QVERIFY(!action(window, "parentFolderAction")->isEnabled());
        QVERIFY(!action(window, "selectAllAction")->isEnabled());
        window.openArchive("ignored.zip");
        window.extractTo("/temporary/destination");
        QCOMPARE(backend.listCalls, 1);
        QCOMPARE(backend.extractCalls, 1);
        auto *progress =
            dynamic_cast<CProgressDialog *>(window.findChild<QDialog *>("progressDialog"));
        QVERIFY(progress);
        auto *percent = progress->findChild<QProgressBar *>("IDC_PROGRESS1");
        auto *label = progress->findChild<QLabel *>("IDT_PROGRESS_STATUS");
        auto *path = progress->findChild<QLabel *>("IDT_PROGRESS_FILE_NAME");
        QVERIFY(percent && label && path);
        const QString detail = QStringLiteral("folder with spaces/файл 空.txt");
        backend.report(37, detail, {});
        QTRY_COMPARE(percent->value(), 37);
        const QString extractionLabel = label->text();
        QVERIFY(!extractionLabel.isEmpty());
        QCOMPARE(window.statusBar()->currentMessage(), extractionLabel + ": " + detail);
        QVERIFY(path->text().contains(QStringLiteral("файл 空.txt")));
        backend.report(38, {}, {});
        QTRY_COMPARE(percent->value(), 38);
        QCOMPARE(window.statusBar()->currentMessage(), extractionLabel + ": " + detail);
        QVERIFY(path->text().contains(QStringLiteral("файл 空.txt")));
        auto *button = progress->findChild<QPushButton *>("IDCANCEL");
        QVERIFY(button);
        QTimer::singleShot(0, &window, [] {
            auto *question = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            QVERIFY(question);
            QCOMPARE(question->objectName(), QStringLiteral("progressCancelQuestion"));
            question->button(QMessageBox::Yes)->click();
        });
        button->click();
        QCOMPARE(backend.cancelCalls, 1);
        QCOMPARE(table(window)->rowCount(), 1);
        QVERIFY(action(window, "extractArchiveAction")->isEnabled());
        QVERIFY(action(window, "parentFolderAction")->isEnabled());
        QVERIFY(action(window, "selectAllAction")->isEnabled());
        QCOMPARE(window.statusBar()->currentMessage(), QStringLiteral("Операция отменена."));
        backend.report(99, QStringLiteral("retired path"), {});
        QCOMPARE(window.statusBar()->currentMessage(), QStringLiteral("Операция отменена."));
        window.testArchive();
        QCOMPARE(backend.testCalls, 1);
        auto *next = dynamic_cast<CProgressDialog *>(window.findChild<QDialog *>("progressDialog"));
        QVERIFY(next);
        backend.report(64, detail, {});
        QTRY_COMPARE(next->findChild<QProgressBar *>("IDC_PROGRESS1")->value(), 64);
        const QString testLabel = next->findChild<QLabel *>("IDT_PROGRESS_STATUS")->text();
        QVERIFY(testLabel != extractionLabel);
        QCOMPARE(window.statusBar()->currentMessage(), testLabel + ": " + detail);
        backend.cancel();
    }

    void createDialogEncryptionChoices() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString source = temp.filePath(QStringLiteral("δ file.txt"));
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        CreateArchiveDialog dialog(temp.path(), {source});
        auto *format = dialog.findChild<QComboBox *>("archiveFormat");
        auto *encryption = dialog.findChild<QComboBox *>("archiveEncryption");
        auto *password = dialog.findChild<QLineEdit *>("archivePassword");
        QVERIFY(format);
        QVERIFY(encryption);
        QVERIFY(password);
        QCOMPARE(password->echoMode(), QLineEdit::Password);
        dialog.findChild<QCheckBox *>("archiveEncryptHeaders")->setChecked(true);
        password->setText(QStringLiteral("disposable-test-password"));
        QCOMPARE(dialog.password(), QStringLiteral("disposable-test-password"));
        QVERIFY(dialog.encryptHeaders());
        format->setCurrentText("zip");
        encryption->setCurrentIndex(1);
        QCOMPARE(dialog.password(), QStringLiteral("disposable-test-password"));
        QVERIFY(!dialog.encryptHeaders());
    }

    void sourceProgressAndFailure() {
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(backend.prepareArchiveFixture("first.7z"));
        backend.queueListAction([&] {
            const auto finish = qScopeGuard([&] { backend.completeList({}); });
            auto *progress = dynamic_cast<CQtProgressDialog *>(QApplication::activeModalWidget());
            QVERIFY(progress);
            QCOMPARE(progress->windowModality(), Qt::ApplicationModal);
            backend.report(42, QStringLiteral("папка/имя с пробелом.txt"),
                           QStringLiteral("42% + папка/имя с пробелом.txt"));
            auto *bar = progress->findChild<QProgressBar *>("IDC_PROGRESS1");
            auto *name = progress->findChild<QLabel *>("IDT_PROGRESS_FILE_NAME");
            QVERIFY(bar && name);
            QTRY_COMPARE(bar->value(), 42);
            QTRY_VERIFY(name->text().contains(QStringLiteral("имя с пробелом.txt")));
            QVERIFY(name->text().contains(QStringLiteral("папка")));
        });
        window.openArchive("first.7z");

        QVERIFY(backend.prepareArchiveFixture("second.zip"));
        QStringList errors;
        QTimer warningResponder;
        connect(&warningResponder, &QTimer::timeout, &window, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                errors.append(box->text());
                box->accept();
            }
        });
        warningResponder.start(1);
        backend.queueListAction([&] {
            backend.report(-1, {}, QStringLiteral("ERROR: повреждённый архив"));
            backend.fail(QStringLiteral("7z завершился с кодом 2."));
        });
        window.openArchive("second.zip");
        warningResponder.stop();
        QCOMPARE(errors, QStringList({QStringLiteral("7z завершился с кодом 2.")}));
        QCOMPARE(backend.lastResult(), E_FAIL);
        QCOMPARE(window.statusBar()->currentMessage(), QStringLiteral("7z завершился с кодом 2."));
    }

    void contextMenuFollowsSelectionAndLocation() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString name = QFileInfo(temp.path()).fileName() + QStringLiteral(" имя с пробелом");
        QFile file(temp.filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.findChild<QLineEdit *>("archivePath")->setText(temp.path());
        QTest::keyClick(window.findChild<QLineEdit *>("archivePath"), Qt::Key_Return);
        auto *view = table(window);
        QTRY_VERIFY(filesystemRow(window, name) >= 0);
        QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
        QStringList commands;
        QHash<QString, bool> enabled;
        auto capture = [&] {
            QTimer::singleShot(0, &window, [&] {
                auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
                QVERIFY(menu);
                const auto collect = [&](auto &&self, QMenu &current) -> void {
                    for (QAction *item : current.actions()) {
                        commands << item->text();
                        enabled.insert(item->text(), item->isEnabled());
                        if (item->menu())
                            self(self, *item->menu());
                    }
                };
                collect(collect, *menu);
                menu->close();
            });
        };
        capture();
        QVERIFY(QMetaObject::invokeMethod(
            view, "customContextMenuRequested", Qt::DirectConnection,
            Q_ARG(
                QPoint,
                view->visualRect(view->model()->index(filesystemRow(window, name), 0)).center())));
        QVERIFY(commands.contains("Rena&me"));
        QVERIFY(commands.contains("&Copy To..."));
        QVERIFY(commands.contains("Create Folder"));
        QVERIFY(enabled.value(QStringLiteral("Rena&me")));
        QVERIFY(!commands.contains("Refresh"));
#ifdef Q_OS_WIN
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        std::atomic<bool> stop{false};
        auto observer = std::async(std::launch::async, NativePropertiesFixture::ObserveAndClose,
                                   name, reinterpret_cast<HWND>(window.winId()), std::cref(stop));
        const auto finish = qScopeGuard([&] { stop = true; });
        action(window, "selectedPropertiesAction")->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(
            observer.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready, 15000);
        const auto sheet = observer.get();
        QVERIFY2(sheet.window && sheet.tabs && sheet.visible, qPrintable(sheet.diagnostic));
        QTRY_VERIFY(!IsWindow(sheet.window));
#else
        QStringList properties;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<QDialog *>("selectedPropertiesDialog");
            QVERIFY(dialog);
            auto *list = dialog->findChild<QTreeWidget *>("propertiesList");
            QVERIFY(list);
            for (int row = 0; row < list->topLevelItemCount(); ++row)
                properties << list->topLevelItem(row)->text(1);
            dialog->reject();
        });
        action(window, "selectedPropertiesAction")->trigger();
        QVERIFY(properties.contains(name));
        QVERIFY(properties.contains(
            QDir::fromNativeSeparators(QFileInfo(temp.path()).canonicalFilePath() + '/')));
#endif
        commands.clear();
        view->clearSelection();
        capture();
        QVERIFY(QMetaObject::invokeMethod(view, "customContextMenuRequested", Qt::DirectConnection,
                                          Q_ARG(QPoint, QPoint(5, view->height() - 5))));
        QVERIFY(commands.contains("Rena&me"));
        // Clearing selection retains focus; native Rename edits that row.
        QVERIFY(enabled.value(QStringLiteral("Rena&me")));
        QVERIFY(commands.contains("Create Folder"));
        QVERIFY(enabled.value(QStringLiteral("Create Folder")));

        openListedArchive(window, backend, "menu.7z",
                          {ArchiveEntry{QStringLiteral("папка/файл.txt"), 1, 1}});
        commands.clear();
        capture();
        QVERIFY(QMetaObject::invokeMethod(view, "customContextMenuRequested", Qt::DirectConnection,
                                          Q_ARG(QPoint, QPoint(5, view->rowHeight(0) / 2))));
        QVERIFY(commands.contains(action(window, "copyAction")->text()));
        QVERIFY(commands.contains("P&roperties"));
    }

    void addressDropdownKeepsSourceHierarchySeparateFromHistory() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString folder = temp.filePath(QStringLiteral("папка с пробелом"));
        QVERIFY(QDir().mkdir(folder));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *address = window.findChild<QLineEdit *>("archivePath");
        auto *combo = window.findChild<QComboBox *>("pathCombo");
        QVERIFY(address && combo);
        PanelKeys::focusAddress(address);
        QTRY_VERIFY(address->hasFocus());
        address->setText(folder);
        QTest::keyClick(address, Qt::Key_Return);
        QCOMPARE(address->text(), QFileInfo(folder).canonicalFilePath());
        QSettings settings(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"), QSettings::IniFormat);
        QVERIFY(settings.value("FolderHistory")
                    .toStringList()
                    .contains(QFileInfo(folder).canonicalFilePath()));
        const QString archive = temp.filePath("archive.7z");
        openListedArchive(window, backend, archive, {});
        AddressPopupSupport::show(*combo);
        QApplication::processEvents();
        QVERIFY(combo->findText(QStringLiteral("archive.7z")) >= 0);
        const int parent = combo->findText(QFileInfo(temp.path()).fileName());
        QVERIFY(parent >= 0);
        AddressPopupSupport::selectRow(*combo, parent);
        QVERIFY(!QTest::currentTestFailed());
        QTRY_COMPARE(QDir::cleanPath(address->text()), QFileInfo(temp.path()).canonicalFilePath());
    }
};

QTEST_MAIN(MainWindowArchiveStatesTests)
#include "ui_archive_states_tests.moc"
