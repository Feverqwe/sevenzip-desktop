#include "7zip/UI/FileManager/resourceGui.h"
#include "7zip/UI/GUI/ExtractRes.h"
#include "archive/support/native_archive_fixture.h"
#include "dialogs/copy/alias.h"
#include "dialogs/overwrite/OverwriteDialog.h"
#include "dialogs/password/PasswordDialog.h"
#include "filemanager/properties/qtpropertieswindow.h"
#include "filemanager/support/window_state_support.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "settings/RegistryUtils.h"
#include "settings/ViewSettings.h"
#include "support/panel_keys.h"
#include "support/progress_dialog.h"
#include "support/window_activation.h"
#include "ui/progress/ProgressDialog2.h"
#include "ui/progress/qtprogresscontrols.h"
#include "ui/support/address_popup_support.h"
#include "ui/support/layout_inspection.h"
#include <QClipboard>
#include <QFileDialog>
#include <QLineEdit>
#include <QProgressBar>
#include <QScopeGuard>
#include <QTreeWidget>
#include <algorithm>
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
    void rejectedExtractionDestinationKeepsSelectedItems() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString archive = fixture.filePath("input.zip");
#ifdef Q_OS_WIN
        const QString blocked = fixture.filePath("blocked");
#else
        const QString blocked = fixture.filePath("blocked <b>folder");
#endif
        const QString output = fixture.filePath("output");
        QVERIFY(QDir().mkpath(QDir(blocked).filePath("selected.txt")));
        QVERIFY(QDir().mkpath(output));
        QVERIFY(NativeArchiveFixture::Write(
            archive, {{"selected.txt", "payload"}, {"other.txt", "other"}}, "zip"));
        ArchiveBackend backend;
        QCOMPARE(NativeArchiveFixture::Attach(backend), S_OK);
        MainWindow window(&backend);
        window.show();
        window.openArchive(archive);
        QTRY_VERIFY(rowWithPath(window, "selected.txt") >= 0);
        QVERIFY(WindowActivation::waitForActive(window));
        auto *view = table(window);
        view->setFocus();
        view->selectRow(rowWithPath(window, "selected.txt"));
        QString destination = blocked;
        QString diagnostic;
        Qt::TextFormat diagnosticFormat = Qt::AutoText;
        QTimer responder;
        connect(&responder, &QTimer::timeout, &window, [&] {
            auto *modal = QApplication::activeModalWidget();
            if (auto *copy = dynamic_cast<CopyDialog *>(modal)) {
                copy->findChild<QLineEdit *>("copyDestination")->setText(destination + '/');
                copy->accept();
            } else if (auto *warning = qobject_cast<QMessageBox *>(modal)) {
                diagnostic = warning->text();
                diagnosticFormat = warning->textFormat();
                warning->accept();
            }
        });
        responder.start(5);
        auto *copy = action(window, "copyAction");
        QVERIFY(copy->isEnabled());
        copy->trigger();
        QTRY_VERIFY(copy->isEnabled());
        QCOMPARE(diagnosticFormat, Qt::PlainText);
        QVERIFY(diagnostic.contains(QDir(blocked).filePath("selected.txt")));
        QVERIFY(QFileInfo(QDir(blocked).filePath("selected.txt")).isDir());
        QCOMPARE(view->selectionModel()->selectedRows().size(), 1);
        QCOMPARE(view->selectionModel()->selectedRows().front().data().toString(),
                 QString("selected.txt"));
        destination = output;
        copy->trigger();
        QTRY_VERIFY(copy->isEnabled());
        QFile extracted(QDir(output).filePath("selected.txt"));
        QVERIFY(extracted.open(QIODevice::ReadOnly));
        QCOMPARE(extracted.readAll(), QByteArray("payload"));
        QVERIFY(!QFileInfo::exists(QDir(output).filePath("other.txt")));
    }

    void failedExtractionKeepsSelectionForPasswordRetry_data() {
        QTest::addColumn<bool>("cancel");
        QTest::addColumn<bool>("hasExistingOutput");
        QTest::newRow("cancel-existing") << true << true;
        QTest::newRow("wrong-password-existing") << false << true;
        QTest::newRow("cancel-new") << true << false;
        QTest::newRow("wrong-password-new") << false << false;
    }
    void failedExtractionKeepsSelectionForPasswordRetry() {
        QFETCH(bool, cancel);
        QFETCH(bool, hasExistingOutput);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString archive = fixture.filePath("encrypted.zip");
        const QString outputPath = fixture.filePath("selected.txt");
        const QString password = QStringLiteral("correct password");
        QVERIFY(NativeArchiveFixture::Write(
            archive, {{"selected.txt", "selected"}, {"other.txt", "other"}}, "zip", password));
        QFile existing(outputPath);
        if (hasExistingOutput) {
            QVERIFY(existing.open(QIODevice::WriteOnly));
            QCOMPARE(existing.write("previous"), qint64(8));
            existing.close();
        }
        ArchiveBackend backend;
        QCOMPARE(NativeArchiveFixture::Attach(backend), S_OK);
        MainWindow window(&backend);
        window.show();
        window.openArchive(archive);
        QTRY_VERIFY(rowWithPath(window, "selected.txt") >= 0);
        QVERIFY(WindowActivation::waitForActive(window));
        auto *view = table(window);
        const int selected = rowWithPath(window, "selected.txt");
        view->setFocus();
        view->setCurrentCell(selected, 0);
        view->selectRow(selected);
        bool retry = false;
        int prompts = 0;
        QTimer responder;
        connect(&responder, &QTimer::timeout, &window, [&] {
            auto *modal = QApplication::activeModalWidget();
            if (auto *copy = dynamic_cast<CopyDialog *>(modal))
                copy->accept();
            else if (auto *overwrite = dynamic_cast<COverwriteDialog *>(modal))
                overwrite->findChild<QPushButton *>("overwriteYes")->click();
            else if (auto *dialog = dynamic_cast<CPasswordDialog *>(modal)) {
                ++prompts;
                if (cancel && !retry)
                    dialog->reject();
                else {
                    dialog->findChild<QLineEdit *>("passwordEdit")
                        ->setText(retry ? password : QStringLiteral("wrong password"));
                    dialog->accept();
                }
            } else if (auto *report = TestProgressDialog::ResultWindow(modal))
                TestProgressDialog::CloseResult(*report);
            else if (auto *box = qobject_cast<QMessageBox *>(modal))
                box->accept();
        });
        responder.start(5);
        auto *copy = action(window, "copyAction");
        QVERIFY(copy->isEnabled());
        copy->trigger();
        QTRY_VERIFY(copy->isEnabled());
        QCOMPARE(prompts, 1);
        if (hasExistingOutput) {
            QVERIFY(existing.open(QIODevice::ReadOnly));
            QCOMPARE(existing.readAll(), QByteArray("previous"));
            existing.close();
        } else {
            QVERIFY(!existing.exists());
        }
        QCOMPARE(view->selectionModel()->selectedRows().size(), 1);
        QVERIFY(view->selectionModel()->isRowSelected(selected, {}));
        retry = true;
        copy->trigger();
        QTRY_VERIFY(copy->isEnabled());
        QFile output(outputPath);
        QVERIFY(output.open(QIODevice::ReadOnly));
        QCOMPARE(output.readAll(), QByteArray("selected"));
        QVERIFY(!QFileInfo::exists(fixture.filePath("other.txt")));
    }
    void nestedArchiveUsesTheOuterArchivesFolder() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString inner = fixture.filePath("inner.zip");
        const QString outer = fixture.filePath("outer.7z");
        QVERIFY(NativeArchiveFixture::Write(inner, {{"payload.txt", "payload"}}, "zip"));
        QFile bytes(inner);
        QVERIFY(bytes.open(QIODevice::ReadOnly));
        QVERIFY(NativeArchiveFixture::Write(outer, {{"inner.zip", bytes.readAll()}}, "7z"));
        ArchiveBackend backend;
        QCOMPARE(NativeArchiveFixture::Attach(backend), S_OK);
        MainWindow window(&backend);
        window.show();
        window.openArchive(outer);
        QTRY_VERIFY(rowWithPath(window, "inner.zip") >= 0);
        openRow(window, rowWithPath(window, "inner.zip"));
        QTRY_VERIFY(rowWithPath(window, "payload.txt") >= 0);
        QVERIFY(WindowActivation::waitForActive(window));
        QString directory;
        QTimer responder;
        connect(&responder, &QTimer::timeout, &window, [&] {
            if (auto *picker = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) {
                directory = picker->directory().canonicalPath();
                picker->reject();
            } else if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
                dialog->reject();
            }
        });
        responder.start(5);
        auto *open = action(window, "openArchiveAction");
        QVERIFY(open->isEnabled());
        open->trigger();
        QCOMPARE(directory, QDir(fixture.path()).canonicalPath());
        QVERIFY(rowWithPath(window, "payload.txt") >= 0);
        responder.stop();

        QVERIFY(WindowActivation::waitForActive(window));
        auto *view = table(window);
        const int row = rowWithPath(window, "payload.txt");
        view->setFocus();
        view->setCurrentCell(row, 0);
        view->selectRow(row);
        QString destination;
        QTimer copyResponder;
        connect(&copyResponder, &QTimer::timeout, &window, [&] {
            auto *dialog = QApplication::activeModalWidget();
            if (auto *copy = dynamic_cast<CopyDialog *>(dialog)) {
                if (destination.isEmpty()) {
                    destination = copy->findChild<QLineEdit *>("copyDestination")->text();
                    copy->accept();
                } else
                    copy->reject();
            } else if (auto *report = TestProgressDialog::ResultWindow(dialog))
                TestProgressDialog::CloseResult(*report);
            else if (auto *box = qobject_cast<QMessageBox *>(dialog))
                box->accept();
        });
        copyResponder.start(5);
        auto *copy = action(window, "copyAction");
        QVERIFY(copy->isEnabled());
        copy->trigger();
        QCOMPARE(QDir::cleanPath(QDir::fromNativeSeparators(destination)), fixture.path());
        const QString extracted = fixture.filePath("payload.txt");
        QTRY_VERIFY(QFileInfo(extracted).isFile());
        QFile output(extracted);
        QVERIFY(output.open(QIODevice::ReadOnly));
        QCOMPARE(output.readAll(), QByteArray("payload"));
    }
    void batchArchiveCommandKeepsTheNewActiveWindow_data() {
        QTest::addColumn<QByteArray>("actionName");
        QTest::newRow("test") << QByteArray("testArchiveAction");
        QTest::newRow("extract") << QByteArray("extractArchiveAction");
        QTest::newRow("add") << QByteArray("toolbarAddAction");
    }
    void batchArchiveCommandKeepsTheNewActiveWindow() {
        QFETCH(QByteArray, actionName);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString archive = fixture.filePath("batch.zip");
        QVERIFY(NativeArchiveFixture::Write(archive, {{"file.txt", "payload"}}, "zip"));
        MainWindow peer;
        peer.show();
        peer.openPath(fixture.path());
        QTRY_VERIFY(filesystemRow(peer, "batch.zip") >= 0);
        auto *peerView = table(peer);
        struct SwitchingBackend : ControlledBackend {
            QWidget *peer = nullptr;
            QWidget *view = nullptr;
            void switchWindow() {
                QApplication::setActiveWindow(peer);
                view->setFocus();
            }
            QtOperationLauncher *startGuiTest(const QStringList &paths, QString &error,
                                              bool hashMode) override {
                switchWindow();
                return ControlledBackend::startGuiTest(paths, error, hashMode);
            }
            QtOperationLauncher *startGuiExtract(const QStringList &paths, const QString &folder,
                                                 bool showDialog, bool elimDup, quint32 zone,
                                                 QString &error) override {
                switchWindow();
                return ControlledBackend::startGuiExtract(paths, folder, showDialog, elimDup, zone,
                                                          error);
            }
            QtOperationLauncher *startGuiCompress(const QString &output, const QString &type,
                                                  bool addExtension, const QStringList &paths,
                                                  bool email, bool showDialog,
                                                  QString &error) override {
                switchWindow();
                return ControlledBackend::startGuiCompress(output, type, addExtension, paths, email,
                                                           showDialog, error);
            }
        } backend;
        backend.peer = &peer;
        backend.view = peerView;
        QCOMPARE(NativeArchiveFixture::Attach(backend), S_OK);
        MainWindow window(&backend);
        window.show();
        window.openPath(fixture.path());
        QVERIFY(WindowActivation::waitForActive(window));
        QTRY_VERIFY(filesystemRow(window, "batch.zip") >= 0);
        auto *view = table(window);
        view->setFocus();
        const int row = filesystemRow(window, "batch.zip");
        view->setCurrentCell(row, 0);
        view->selectRow(row);
        auto *command = action(window, actionName.constData());
        QVERIFY(command->isEnabled());
        command->trigger();
        QCOMPARE(backend.GuiRequests.size(), 1);
        QCoreApplication::processEvents();
        QCOMPARE(QApplication::activeWindow(), &peer);
        QCOMPARE(QApplication::focusWidget(), peerView);
        QCOMPARE(window.focusWidget(), view);
    }

    void testReportIdentifiesWhichItemsWereChecked_data() {
        QTest::addColumn<bool>("corrupt");
        QTest::newRow("healthy") << false;
        QTest::newRow("damaged-entry") << true;
    }
    void testReportIdentifiesWhichItemsWereChecked() {
        QFETCH(bool, corrupt);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString archive = fixture.filePath("test scope.zip");
        const QByteArray damagedPayload("unique stored payload to corrupt");
        QVERIFY(NativeArchiveFixture::Write(archive,
                                            {{"outside.txt", "outside"},
                                             {"folder/inside.txt", "inside"},
                                             {"damaged.txt", damagedPayload}},
                                            "zip", {}, true, true));
        if (corrupt) {
            QFile file(archive);
            QVERIFY(file.open(QIODevice::ReadWrite));
            const auto payloadOffset = file.readAll().indexOf(damagedPayload);
            QVERIFY(payloadOffset >= 0);
            QVERIFY(file.seek(payloadOffset));
            QCOMPARE(file.write("!", 1), qint64(1));
        }
        ArchiveBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openArchive(archive);
        QTRY_VERIFY(rowWithPath(window, "outside.txt") >= 0);
        auto *view = table(window);
        QVERIFY(WindowActivation::waitForActive(window));
        view->setFocus();
        QCOMPARE(window.focusWidget(), view);
        const auto *test = action(window, "testArchiveAction");
        QVERIFY(test->isEnabled());

        QStringList reports;
        QTimer responder;
        connect(&responder, &QTimer::timeout, &window, [&] {
            if (auto *result =
                    TestProgressDialog::ResultWindow(QApplication::activeModalWidget())) {
                reports.append(TestProgressDialog::ReportText(*result));
                TestProgressDialog::CloseResult(*result);
            } else if (auto *message =
                           qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                message->accept();
            }
        });
        responder.start(5);
        if (corrupt) {
            view->selectRow(rowWithPath(window, "damaged.txt"));
            window.testArchive();
            QCOMPARE(reports.size(), 1);
            QVERIFY(reports.last().contains("Scope: selected items"));
            QVERIFY(reports.last().contains("damaged.txt"));
            QVERIFY(reports.last().contains("CRC", Qt::CaseInsensitive));
            QVERIFY(!reports.last().contains("outside.txt"));
            QTRY_COMPARE(window.focusWidget(), view);
            QCOMPARE(view->selectionModel()->selectedRows().size(), 1);
            QVERIFY(view->selectionModel()->isRowSelected(rowWithPath(window, "damaged.txt"), {}));
            reports.clear();
        }
        view->selectRow(rowWithPath(window, "outside.txt"));
        window.testArchive();
        QCOMPARE(reports.size(), 1);
        QVERIFY(reports.last().contains("Scope: selected items"));
        QVERIFY(reports.last().contains("outside.txt"));
        QVERIFY(!reports.last().contains("damaged.txt"));
        QTRY_COMPARE(window.focusWidget(), view);

        view->clearSelection();
        window.testArchive();
        QCOMPARE(reports.size(), 2);
        QVERIFY(reports.last().contains("Scope: entire archive"));
        if (corrupt) {
            QVERIFY(reports.last().contains("damaged.txt"));
            QVERIFY(reports.last().contains("CRC", Qt::CaseInsensitive));
        }

        openRow(window, rowWithPath(window, "folder"));
        QTRY_VERIFY(rowWithPath(window, "folder/inside.txt") >= 0);
        view->clearSelection();
        window.testArchive();
        QCOMPARE(reports.size(), 3);
        QVERIFY(reports.last().contains("Scope: current folder"));
        QVERIFY(reports.last().contains("inside.txt"));
        QVERIFY(!reports.last().contains("damaged.txt"));
    }

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
            auto *destination = dialog->findChild<QLineEdit *>("copyDestination");
            QVERIFY(destination);
            destination->setText("/temporary/selected output");
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
            else
                TestProgressDialog::DismissVisibleResult(QApplication::activeModalWidget());
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
        const QString archiveTitle = window.windowTitle();
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
        auto *path = progress->findChild<QLabel *>("IDT_PROGRESS_FILE_NAME");
        QVERIFY(percent && path);
        const QString detail = QStringLiteral("folder with spaces/файл 空.txt");
        backend.report(37, detail, {});
        QTRY_COMPARE(percent->value(), 37);
        const QString extractionLabel = QtLangText(IDS_PROGRESS_EXTRACTING);
        QTRY_VERIFY(window.windowTitle().startsWith("37%"));
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
        QCOMPARE(window.windowTitle(), archiveTitle);
        QCOMPARE(table(window)->rowCount(), 1);
        QVERIFY(action(window, "extractArchiveAction")->isEnabled());
        QVERIFY(action(window, "parentFolderAction")->isEnabled());
        QVERIFY(action(window, "selectAllAction")->isEnabled());
        QCOMPARE(window.statusBar()->currentMessage(), QStringLiteral("Операция отменена."));
        backend.report(99, QStringLiteral("retired path"), {});
        QCOMPARE(window.statusBar()->currentMessage(), QStringLiteral("Операция отменена."));
        QVERIFY(TestProgressDialog::DismissVisibleResult(QApplication::activeModalWidget()));
        QCOMPARE(window.windowTitle(), archiveTitle);
        window.testArchive();
        QCOMPARE(backend.testCalls, 1);
        auto *next = dynamic_cast<CProgressDialog *>(window.findChild<QDialog *>("progressDialog"));
        QVERIFY(next);
        backend.report(64, detail, {});
        QTRY_COMPARE(next->findChild<QProgressBar *>("IDC_PROGRESS1")->value(), 64);
        QTRY_VERIFY(window.windowTitle().startsWith("64%"));
        QCOMPARE(window.statusBar()->currentMessage(),
                 QtLangText(IDS_PROGRESS_TESTING) + ": " + detail);
        backend.completeExtraction();
        QCOMPARE(window.windowTitle(), archiveTitle);
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
            const QString path = QStringLiteral("папка/имя с пробелом.txt");
            backend.report(42, path, QStringLiteral("42% + папка/имя с пробелом.txt"));
            auto *bar = progress->findChild<QProgressBar *>("IDC_PROGRESS1");
            auto *name = progress->findChild<QLabel *>("IDT_PROGRESS_FILE_NAME");
            QVERIFY(bar && name);
            QTRY_COMPARE(bar->value(), 42);
            QTRY_COMPARE(name->text(), QStringLiteral("имя с пробелом.txt"));
            QCOMPARE(name->toolTip(), path);
            QCOMPARE(name->accessibleDescription(), path);
        });
        window.openArchive("first.7z");
        const QString openedArchive = window.findChild<QLineEdit *>("archivePath")->text();

        QVERIFY(backend.prepareArchiveFixture("second.zip"));
        const QString diagnostic = QStringLiteral("Повреждены данные архива.");
        QStringList errors;
        QTimer warningResponder;
        connect(&warningResponder, &QTimer::timeout, &window, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                errors.append(box->text());
                box->accept();
            }
        });
        warningResponder.start(1);
        backend.queueListAction([&] { backend.fail(diagnostic); });
        window.openArchive("second.zip");
        warningResponder.stop();
        QCOMPARE(errors, QStringList({diagnostic}));
        QCOMPARE(backend.lastResult(), E_FAIL);
        QCOMPARE(window.statusBar()->currentMessage(), diagnostic);
        QCOMPARE(window.findChild<QLineEdit *>("archivePath")->text(), openedArchive);
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
        action(window, "selectedPropertiesAction")->trigger();
        const auto checkProperties = [&] {
            auto *dialog = window.findChild<QtPropertiesWindow *>();
            QVERIFY(dialog && dialog->isVisible());
            auto *copy = dialog->findChild<QPushButton *>("propertiesCopyInformation");
            QVERIFY(copy);
            auto *clipboard = QApplication::clipboard();
            clipboard->clear();
            copy->click();
            const QStringList properties = clipboard->text().split('\n');
            QVERIFY(properties.contains(name));
            QVERIFY(properties.contains("Location: " + QFileInfo(temp.path()).canonicalFilePath() +
                                        '/'));
            dialog->close();
            QTRY_VERIFY(!window.findChild<QtPropertiesWindow *>());
        };
        checkProperties();
        QTimer::singleShot(0, &window, [&] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            QVERIFY(menu);
            const auto items = menu->actions();
            const auto properties = std::find_if(items.begin(), items.end(), [&](QAction *item) {
                return item->property("nativeCommandID") ==
                       action(window, "selectedPropertiesAction")->property("nativeCommandID");
            });
            if (properties == items.end()) {
                menu->close();
                QFAIL("The context menu has no Properties command");
            }
            QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
                              menu->actionGeometry(*properties).center());
        });
        QVERIFY(QMetaObject::invokeMethod(
            view, "customContextMenuRequested", Qt::DirectConnection,
            Q_ARG(
                QPoint,
                view->visualRect(view->model()->index(filesystemRow(window, name), 0)).center())));
        checkProperties();
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
        UStringVector history;
        ReadFolderHistory(history, qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        bool recorded = false;
        FOR_VECTOR(i, history)
        recorded |= QDir::cleanPath(ToQString(history[i])) == QFileInfo(folder).canonicalFilePath();
        QVERIFY(recorded);
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
