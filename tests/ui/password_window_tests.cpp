#include "7zip/UI/FileManager/resourceGui.h"
#include "archive/support/native_archive_fixture.h"
#include "archive/support/source_extract_fixture.h"
#include "dialogs/overwrite/OverwriteDialog.h"
#include "dialogs/password/PasswordDialog.h"
#include "filemanager/support/window_state_support.h"
#include "localization/LangUtils.h"
#include "support/progress_dialog.h"
#include "support/widget_dialogs.h"
#include "ui/progress/ProgressDialog2.h"
#include "ui/support/layout_inspection.h"
#include <QScopeGuard>

class PasswordWindowTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
  private slots:
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }
    void cancelledExtractionKeepsEachPanelsNativePassword_data() {
        QTest::addColumn<QString>("leftPassword");
        QTest::newRow("unicode-spaces") << QStringLiteral(" left 密碼 ");
        QTest::newRow("defined-empty") << QStringLiteral("");
    }
    void cancelledExtractionKeepsEachPanelsNativePassword() {
        QFETCH(QString, leftPassword);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString name = QStringLiteral("данные 密碼 with spaces.txt");
        const QByteArray payload = "encrypted payload";
        const QString leftArchive = fixture.filePath("left.7z");
        const QString rightArchive = fixture.filePath("right.7z");
        const QString rightPassword = QStringLiteral("right δ password");
        NativeArchiveFixture::Item item;
        item.Path = name;
        item.Bytes = payload;
        QVERIFY(NativeArchiveFixture::Write(leftArchive, {item}, "7z", leftPassword));
        QVERIFY(NativeArchiveFixture::Write(rightArchive, {item}, "7z", rightPassword));
        ArchiveBackend backend;
        QCOMPARE(NativeArchiveFixture::Attach(backend), S_OK);
        WidgetDialogFixture dialogs;
        QString answer = leftPassword;
        int passwords = 0, overwrites = 0, testResults = 0;
        QStringList errors, promptedArchives;
        QTimer responder;
        MainWindow window(&backend);
        dialogs.connectResponder(responder, &window, [&] {
            auto *modal = dialogs.activeDialog();
            if (auto *dialog = dynamic_cast<CPasswordDialog *>(modal)) {
                ++passwords;
                const auto *source = dialog->findChild<QWidget *>("passwordArchiveSource");
                promptedArchives.append(source ? QFileInfo(source->toolTip()).fileName()
                                               : QString());
                dialog->findChild<QLineEdit *>("passwordEdit")->setText(answer);
                dialog->accept();
            } else if (auto *dialog = dynamic_cast<COverwriteDialog *>(modal)) {
                ++overwrites;
                dialog->reject();
            } else if (auto *progress = dynamic_cast<CProgressDialog *>(modal)) {
                if (auto *result = TestProgressDialog::ResultWindow(progress)) {
                    const QString report = TestProgressDialog::ReportText(*result);
                    if (report.contains(QString::fromWCharArray(LangString(IDS_MESSAGE_NO_ERRORS))))
                        ++testResults;
                    else if (!report.isEmpty())
                        errors.append(report);
                    TestProgressDialog::CloseResult(*result);
                }
            } else if (auto *box = qobject_cast<QMessageBox *>(modal)) {
                errors.append(box->text());
                if (auto *yes = box->button(QMessageBox::Yes))
                    yes->click();
                else
                    box->accept();
            }
        });
        responder.start(1);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QTRY_VERIFY(action(window, "twoPanelsAction")->isEnabled());
        if (!action(window, "twoPanelsAction")->isChecked())
            action(window, "twoPanelsAction")->trigger();
        QVERIFY(action(window, "twoPanelsAction")->isChecked());
        auto *left = table(window);
        auto *right =
            window.findChild<QWidget *>("filePanel1")->findChild<FileTable *>("archiveTable");
        QVERIFY(left && right);
        left->setFocus();
        QTRY_VERIFY(left->hasFocus());
        window.openArchive(leftArchive);
        QTRY_COMPARE(left->rowCount(), 1);
        QTRY_VERIFY(left->isEnabled());
        QCOMPARE(passwords, 1);
        left->selectRow(0);
        const QString cancelled = fixture.filePath("cancelled output");
        QVERIFY(SourceExtractFixture::writeFile(QDir(cancelled).filePath(name), "retained"));
        ExtractionOptions options;
        options.overwriteMode = ExtractionOptions::OverwriteMode::Ask;
        window.extractTo(cancelled, true, {}, options);
        QCOMPARE(overwrites, 1);
        QCOMPARE(passwords, 1);
        QCOMPARE(SourceExtractFixture::readFile(QDir(cancelled).filePath(name)),
                 QByteArray("retained"));
        window.testArchive();
        QCOMPARE(passwords, 1);
        answer = rightPassword;
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QTRY_VERIFY(right->isVisible());
        QTRY_VERIFY(right->isEnabled());
        right->setFocus();
        QTRY_VERIFY(right->hasFocus());
        window.openArchive(rightArchive);
        QTRY_COMPARE(right->rowCount(), 1);
        QTRY_VERIFY(right->isEnabled());
        QCOMPARE(passwords, 2);
        window.testArchive();
        QCOMPARE(passwords, 2);
        answer = leftPassword;
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QTRY_VERIFY(left->isEnabled());
        left->setFocus();
        QTRY_VERIFY(left->hasFocus());
        left->selectRow(0);
        const QString extracted = fixture.filePath("retried output");
        window.extractTo(extracted, true);
        QCOMPARE(SourceExtractFixture::readFile(QDir(extracted).filePath(name)), payload);
        QCOMPARE(passwords, 2);
        QCOMPARE(promptedArchives, (QStringList{"left.7z", "right.7z"}));
        QCOMPARE(testResults, 2);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
    }
    void encryptedArchiveOpenCanBeRetried_data() {
        QTest::addColumn<bool>("typedFolder");
        QTest::newRow("file-list") << false;
        QTest::newRow("address-to-inner-folder") << true;
    }
    void encryptedArchiveOpenCanBeRetried() {
        QFETCH(bool, typedFolder);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString name = QStringLiteral("payload 空.txt");
        const QString archive = fixture.filePath("protected archive.7z");
        const QString password = QStringLiteral("пароль 密碼");
        const QString innerPath = QStringLiteral("inner folder/") + name;
        QVERIFY(NativeArchiveFixture::Write(
            archive, {{name, "root payload"}, {innerPath, "nested payload"}}, "7z", password));
        ArchiveBackend backend;
        int externalCalls = 0;
        MainWindow window(&backend, [&](const QUrl &) {
            ++externalCalls;
            return true;
        });
        window.show();
        window.openPath(fixture.path());
        auto *view = table(window);
        QTRY_VERIFY(view->isEnabled());
        const int row = filesystemRow(window, QFileInfo(archive).fileName());
        QVERIFY(row >= 0);
        auto *address = window.findChild<QLineEdit *>("archivePath");
        QVERIFY(address);
        QVERIFY(QTest::qWaitForWindowActive(&window));
        if (!typedFolder)
            view->setFocus();
        const auto open = [&] {
            if (!typedFolder) {
                openRow(window, row);
                return;
            }
            address->setFocus();
            address->setText(archive + "/inner folder");
            QTest::keyClick(address, Qt::Key_Return);
        };
        WidgetDialogFixture dialogs;
        enum class Answer { Cancel, Incorrect, Correct };
        Answer answer = Answer::Cancel;
        int prompts = 0;
        int errors = 0;
        QTimer drive;
        dialogs.connectResponder(drive, &window, [&] {
            if (auto *dialog = dynamic_cast<CPasswordDialog *>(dialogs.activeDialog())) {
                ++prompts;
                if (answer == Answer::Cancel)
                    QTest::keyClick(dialog->findChild<QLineEdit *>("passwordEdit"), Qt::Key_Escape);
                else {
                    dialog->findChild<QLineEdit *>("passwordEdit")
                        ->setText(answer == Answer::Correct ? password : QString("incorrect"));
                    dialog->accept();
                }
            } else if (auto *warning = qobject_cast<QMessageBox *>(dialogs.activeDialog())) {
                ++errors;
                warning->accept();
            }
        });
        drive.start(5);
        open();
        QTRY_VERIFY(view->isEnabled() && view->sourcePanel->IsFSFolder());
        QCOMPARE(prompts, 1);
        QCOMPARE(errors, 0);
        QCOMPARE(externalCalls, 0);
        if (!typedFolder) {
            QTRY_COMPARE(window.focusWidget(), view);
            if (window.isActiveWindow())
                QVERIFY(view->hasFocus());
        }
        answer = Answer::Incorrect;
        open();
        QTRY_VERIFY(view->isEnabled() && view->sourcePanel->IsFSFolder());
        QCOMPARE(prompts, 2);
        QCOMPARE(errors, typedFolder ? 0 : 1);
        if (typedFolder)
            QVERIFY(window.statusBar()->currentMessage().contains(QFileInfo(archive).fileName()));
        QCOMPARE(externalCalls, 0);
        answer = Answer::Correct;
        open();
        QTRY_VERIFY(view->isEnabled() && view->sourcePanel->IsArcFolder());
        QCOMPARE(prompts, 3);
        QCOMPARE(externalCalls, 0);
        QVERIFY(rowWithPath(window, typedFolder ? innerPath : name) >= 0);
    }

    void passwordPromptCancellationAndReopen() {
        ControlledBackend backend;
        MainWindow window(&backend);
        QVERIFY(backend.prepareArchiveFixture("protected.7z"));
        QTimer responder;
        connect(&responder, &QTimer::timeout, &window, [] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (dialog && dialog->objectName() == "passwordDialog")
                dialog->reject();
        });
        responder.start(1);
        backend.queueListAction([&] { emit backend.passwordRequired(); });
        window.openArchive("protected.7z");
        responder.stop();
        QCOMPARE(backend.cancelCalls, 1);
        QVERIFY(!action(window, "extractArchiveAction")->isEnabled());
        openListedArchive(window, backend, "empty.zip", {});
        QCOMPARE(backend.listCalls, 2);
        QVERIFY(action(window, "testArchiveAction")->isEnabled());
        QVERIFY(action(window, "extractArchiveAction")->isEnabled());
        action(window, "extractArchiveAction")->trigger();
        QVERIFY(!QApplication::activeModalWidget());
        QCOMPARE(backend.extractCalls, 0);
    }

    void passwordReuseAndResetOnReopen_data() {
        QTest::addColumn<QString>("password");
        QTest::newRow("unicode") << QStringLiteral("disposable-δ-password");
        QTest::newRow("spaces") << QStringLiteral("  пароль 密碼  ");
        QTest::newRow("empty") << QString();
    }
    void passwordReuseAndResetOnReopen() {
        QFETCH(QString, password);
        ControlledBackend backend;
        MainWindow window(&backend);
        QVERIFY(backend.prepareArchiveFixture("protected.7z"));
        QTimer responder;
        bool supplied = false;
        connect(&responder, &QTimer::timeout, &window, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog || dialog->objectName() != "passwordDialog" || supplied)
                return;
            supplied = true;
            const auto finish = qScopeGuard([&] {
                dialog->accept();
                QTimer::singleShot(0, &backend, [&] {
                    emit backend.infoReady(
                        ArchiveInfo{QStringLiteral("7z"), 120, 0, 3, 1, 0, true});
                    backend.completeList({ArchiveEntry{QStringLiteral("δ file.txt"), 3, 3}});
                });
            });
            auto *edit = dialog->findChild<QLineEdit *>("passwordEdit");
            QVERIFY(edit);
            edit->setText(password);
            auto *show = dialog->findChild<QCheckBox *>("passwordShow");
            QVERIFY(show);
            if (!show->isChecked())
                show->click();
            QCOMPARE(edit->echoMode(), QLineEdit::Normal);
            for (int width : {360, 260}) {
                dialog->resize(width, 300);
                QApplication::processEvents();
                auto *source = dialog->findChild<QWidget *>("passwordArchiveSource");
                QVERIFY(source && UiInspection::fullyVisible(*source, *dialog));
                QVERIFY(UiInspection::fullyVisible(*edit, *dialog));
                QVERIFY(UiInspection::fullyVisible(*show, *dialog));
                const auto report = UiInspection::dump(*dialog);
                if (!password.isEmpty())
                    QVERIFY(!report.contains(password));
                QVERIFY(UiInspection::writeReport(
                    *dialog,
                    QString("password-window-%1-%2").arg(QTest::currentDataTag()).arg(width)));
            }
        });
        responder.start(1);
        backend.queueListAction([&] { emit backend.passwordRequired(); });
        window.openArchive("protected.7z");
        responder.stop();
        QVERIFY(supplied);
        QCOMPARE(backend.cancelCalls, 0);
        QVERIFY(!action(window, "toolbarAddAction")->isEnabled());
        window.testArchive();
        QCOMPARE(backend.lastTestPassword, password);
        bool transportPrompt = false;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = dynamic_cast<CPasswordDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            transportPrompt = true;
            dialog->findChild<QLineEdit *>("passwordEdit")->setText(password + "-retry");
            dialog->accept();
        });
        // This asynchronous process prompt has no source modal owner. Opening
        // above instead routes the same signal through COpenArchiveCallback.
        emit backend.passwordRequired();
        QVERIFY(transportPrompt);
        QCOMPARE(backend.cancelCalls, 0);
        QVERIFY(!action(window, "openInsideAction")->isEnabled());
        backend.fail(QStringLiteral("Операция отменена."), E_ABORT);
        auto *result = TestProgressDialog::ResultWindow(QApplication::activeModalWidget());
        QVERIFY(result);
        TestProgressDialog::CloseResult(*result);
        QVERIFY(!QApplication::activeModalWidget());
        QVERIFY(action(window, "testArchiveAction")->isEnabled());
        window.extractTo("/temporary/destination");
        QCOMPARE(backend.extractCalls, 1);
        QCOMPARE(backend.lastExtractPassword, password);
        backend.cancel();
        QVERIFY(TestProgressDialog::DismissVisibleResult(QApplication::activeModalWidget()));
        openListedArchive(window, backend, "empty.zip", {});
        QVERIFY(action(window, "testArchiveAction")->isEnabled());
        QVERIFY(action(window, "extractArchiveAction")->isEnabled());
        action(window, "extractArchiveAction")->trigger();
        QVERIFY(!QApplication::activeModalWidget());
        QCOMPARE(backend.extractCalls, 1);
        window.testArchive();
        QVERIFY(backend.lastTestPassword.isEmpty());
    }
};
QTEST_MAIN(PasswordWindowTests)
#include "password_window_tests.moc"
