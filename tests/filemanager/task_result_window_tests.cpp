#include "archive/support/source_extract_fixture.h"
#include "dialogs/taskresult/qttaskresultdialog.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/window/qtworkspaceapplication.h"
#include "localization/LangUtils.h"
#include "settings/RegistryUtils.h"
#include "support/operation_kill.h"
#include "support/window_activation.h"
#include "ui/support/layout_inspection.h"
#include <QClipboard>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QScopeGuard>
#include <QSignalSpy>

class TaskResultWindowTests final : public QObject {
    Q_OBJECT
    static QtTaskResultDialog *reportWindow() {
        for (auto *window : QApplication::topLevelWidgets())
            if (auto *report = qobject_cast<QtTaskResultDialog *>(window);
                report && report->isVisible())
                return report;
        return nullptr;
    }
    static void language(const QString &value) {
        SaveRegLang(ToUString(value), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
    }
  private slots:
    void cleanup() {
        for (auto *window : QApplication::topLevelWidgets())
            if (auto *report = qobject_cast<QtTaskResultDialog *>(window))
                report->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        for (const auto &review : QtOperationRegistry::Application().Reviews())
            QtOperationRegistry::Application().Acknowledge(review.Id);
        language("-");
    }
    void failureAfterLastWindowRequiresReview_data() {
        QTest::addColumn<QString>("terminal");
        QTest::addColumn<QString>("locale");
        QTest::newRow("crashed-child-en") << QString("crash") << QString("-");
        QTest::newRow("missing-report-ru") << QString("missing-report") << QString("ru");
        QTest::newRow("cancelled-work-ru") << QString("cancelled-work") << QString("ru");
    }
    void failureAfterLastWindowRequiresReview() {
        QFETCH(QString, terminal);
        QFETCH(QString, locale);
        language(locale);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const auto archive = SourceExtractFixture::makeArchive(fixture.path(), "unchanged bytes");
        QVERIFY(!archive.isEmpty());
        const auto original = SourceExtractFixture::readFile(archive);
        const auto barrier = fixture.filePath("continue");
        const bool cancelled = terminal == "cancelled-work";
        const auto hashInput = fixture.filePath("hash 空 with spaces.bin");
        if (cancelled) {
            QFile input(hashInput);
            QVERIFY(input.open(QIODevice::WriteOnly));
            QVERIFY(input.resize(256LL * 1024 * 1024));
            qputenv("SEVENZIP_OPERATION_CANCEL_HASH", "1");
        }
        qputenv("SEVENZIP_OPERATION_BARRIER", barrier.toUtf8());
        qputenv("SEVENZIP_OPERATION_FORCE_EXIT", terminal.toUtf8());
        const bool quit = qApp->quitOnLastWindowClosed();
        qApp->setQuitOnLastWindowClosed(true);
        const auto restore = qScopeGuard([quit] {
            qunsetenv("SEVENZIP_OPERATION_BARRIER");
            qunsetenv("SEVENZIP_OPERATION_FORCE_EXIT");
            qunsetenv("SEVENZIP_OPERATION_CANCEL_HASH");
            qApp->setQuitOnLastWindowClosed(quit);
        });
        QtWorkspaceApplication application(*qApp);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openPath(fixture.path());
        QtOperationJob job;
        job.Paths = {archive};
        if (cancelled) {
            job.Operation = QtOperationJob::Command::Hash;
            job.HashMethod = "*";
            job.Paths.append(hashInput);
        }
        QString error;
        QPointer<QtOperationLauncher> operation =
            QtOperationLauncher::Launch(QStringLiteral(SEVENZIP_OPERATION_HOST), job, error);
        QVERIFY2(operation, qPrintable(error));
        const auto stopOnFailure = qScopeGuard([&] {
            if (operation && operation->active()) {
                KillOperationProbe(operation->processId());
                QTest::qWait(100);
            }
        });
        QSignalSpy accepted(operation, &QtOperationLauncher::accepted);
        QSignalSpy finished(operation, &QtOperationLauncher::finished);
        QTRY_COMPARE_WITH_TIMEOUT(accepted.size(), 1, 10000);
        bool closed = false, released = false, retained = false, copied = false;
        bool quitRetained = false, timedOut = false;
        QtOperationResult observed;
        QTimer::singleShot(0, &window, [&] { closed = window.close(); });
        QTimer drive;
        drive.setInterval(25);
        connect(&drive, &QTimer::timeout, &application, [&] {
            if (!released && closed)
                released = SourceExtractFixture::writeFile(barrier, "finish child");
            const auto reviews = QtOperationRegistry::Application().Reviews();
            auto *dialog = reportWindow();
            if (reviews.isEmpty() || !dialog)
                return;
            drive.stop();
            observed = reviews.first().Result;
            QTimer::singleShot(150, dialog, [&, dialog] {
                retained = closed && !window.isVisible() && dialog->isVisible() &&
                           finished.size() == 1 &&
                           !QtOperationRegistry::Application().Reviews().isEmpty();
                application.Quit();
                quitRetained =
                    dialog->isVisible() && !QtOperationRegistry::Application().Reviews().isEmpty();
                auto *text = dialog->findChild<QPlainTextEdit *>("taskResultReport");
                QTextCursor cursor = text->textCursor();
                cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 4);
                text->setTextCursor(cursor);
                QApplication::clipboard()->setText("old clipboard");
                dialog->findChild<QPushButton *>("taskResultCopy")->click();
                copied = QApplication::clipboard()->text() == text->toPlainText() &&
                         text->toPlainText().contains(observed.Error) &&
                         text->toPlainText().contains(archive);
                if (terminal == "missing-report")
                    QTest::keyClick(dialog, Qt::Key_Escape);
                else
                    dialog->findChild<QDialogButtonBox *>("taskResultActions")
                        ->button(QDialogButtonBox::Close)
                        ->click();
            });
        });
        drive.start();
        QTimer deadline;
        deadline.setSingleShot(true);
        connect(&deadline, &QTimer::timeout, &application, [&] {
            timedOut = true;
            qApp->quit();
        });
        deadline.start(10000);
        QCOMPARE(qApp->exec(), 0);
        QVERIFY(!timedOut);
        QVERIFY(closed && released && retained && quitRetained && copied);
        QCOMPARE(observed.Status, cancelled ? QtOperationResult::Outcome::Cancelled
                                  : terminal == "crash"
                                      ? QtOperationResult::Outcome::Crashed
                                      : QtOperationResult::Outcome::ProtocolFailed);
        if (cancelled) {
            QVERIFY(observed.CancellationNeedsReview);
            QCOMPARE(QFileInfo(hashInput).size(), 256LL * 1024 * 1024);
        }
        QVERIFY(QtOperationRegistry::Application().Reviews().isEmpty());
        QCOMPARE(SourceExtractFixture::readFile(archive), original);
    }
    void compactReportKeepsFullDiagnostic_data() {
        QTest::addColumn<QString>("locale");
        QTest::addColumn<bool>("cancelled");
        QTest::newRow("en") << QString("-") << false;
        QTest::newRow("ru") << QString("ru") << false;
        QTest::newRow("cancelled-en") << QString("-") << true;
        QTest::newRow("cancelled-ru") << QString("ru") << true;
    }
    void lateFailurePreservesAnotherQuestion_data() {
        QTest::addColumn<bool>("modal");
        QTest::newRow("modal-question") << true;
        QTest::newRow("modeless-edit") << false;
    }
    void lateFailurePreservesAnotherQuestion() {
        QFETCH(bool, modal);
        language("-");
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const auto archive = SourceExtractFixture::makeArchive(fixture.path(), "retained bytes");
        QVERIFY(!archive.isEmpty());
        const auto barrier = fixture.filePath("continue");
        qputenv("SEVENZIP_OPERATION_BARRIER", barrier.toUtf8());
        qputenv("SEVENZIP_OPERATION_FORCE_EXIT", "crash");
        const auto restore = qScopeGuard([] {
            qunsetenv("SEVENZIP_OPERATION_BARRIER");
            qunsetenv("SEVENZIP_OPERATION_FORCE_EXIT");
        });
        QtWorkspaceApplication application(*qApp);
        ControlledBackend backend;
        MainWindow peer(&backend);
        peer.show();
        peer.openPath(fixture.path());
        QtOperationJob job;
        job.Paths = {archive};
        QString error;
        QPointer<QtOperationLauncher> operation =
            QtOperationLauncher::Launch(QStringLiteral(SEVENZIP_OPERATION_HOST), job, error);
        QVERIFY2(operation, qPrintable(error));
        const auto stopOnFailure = qScopeGuard([&] {
            if (operation && operation->active()) {
                KillOperationProbe(operation->processId());
                QTest::qWait(100);
            }
        });
        QSignalSpy accepted(operation, &QtOperationLauncher::accepted);
        QTRY_COMPARE_WITH_TIMEOUT(accepted.size(), 1, 10000);
        QDialog question(&peer);
        question.setWindowModality(modal ? Qt::WindowModal : Qt::NonModal);
        QLineEdit input(&question);
        input.setText("unfinished input");
        question.show();
        QVERIFY(WindowActivation::waitForActive(question));
        input.setFocus();
        QTRY_COMPARE(QApplication::activeModalWidget(), modal ? &question : nullptr);
        QTRY_VERIFY(input.hasFocus());
        QTest::keyClick(&input, Qt::Key_End);
        QTest::keyClicks(&input, "-draft");
        int commits = 0;
        connect(&input, &QLineEdit::editingFinished, &question, [&] { ++commits; });
        QVERIFY(SourceExtractFixture::writeFile(barrier, "crash child"));
        QTRY_VERIFY_WITH_TIMEOUT(!QtOperationRegistry::Application().Reviews().isEmpty(), 10000);
        QVERIFY(!reportWindow());
        QCOMPARE(QApplication::activeModalWidget(), modal ? &question : nullptr);
        QVERIFY(input.hasFocus());
        QCOMPARE(commits, 0);
        QTest::keyClick(&input, Qt::Key_End);
        QTest::keyClicks(&input, " retained");
        application.Quit();
        QCOMPARE(QApplication::activeModalWidget(), modal ? &question : nullptr);
        QCOMPARE(input.text(), QString("unfinished input-draft retained"));
        question.reject();
        QTRY_VERIFY(!QApplication::activeModalWidget());
        application.Quit();
        QTRY_VERIFY(reportWindow());
        QVERIFY(!reportWindow()
                     ->findChild<QPlainTextEdit *>("taskResultReport")
                     ->toPlainText()
                     .contains(input.text()));
        reportWindow()->reject();
        QVERIFY(QtOperationRegistry::Application().Reviews().isEmpty());
    }
    void compactReportKeepsFullDiagnostic() {
        QFETCH(QString, locale);
        QFETCH(bool, cancelled);
        language(locale);
        QtOperationReview review;
        review.Id = QUuid::createUuid();
        review.Job.Paths = {"/" + QString("длинный path 空/").repeated(12) + "archive.zip"};
        review.Result.Status = cancelled ? QtOperationResult::Outcome::Cancelled
                                         : QtOperationResult::Outcome::ProtocolFailed;
        review.Result.ChangedDirectories = {"/output/частичный результат 空"};
        review.Result.ChangedArchives = {"/output/изменённый архив 空.7z"};
        review.Result.Error =
            (cancelled
                 ? QString("Native messages before cancellation")
                 : QCoreApplication::translate(
                       "QtOperation", "Operation process exited without a complete result.")) +
            "\n" + QString("Original diagnostic: item 空.txt: input/output detail\n").repeated(80);
        QPointer<QtTaskResultDialog> dialog = new QtTaskResultDialog(review);
        const auto close = qScopeGuard([&] {
            if (dialog)
                delete dialog.data();
        });
        dialog->show();
        QCOMPARE(dialog->findChild<QLabel *>("taskResultSummary")->text(),
                 QtTaskResultDialog::tr(cancelled ? "The operation was cancelled."
                                                  : "The operation result could not be verified."));
        auto *text = dialog->findChild<QPlainTextEdit *>("taskResultReport");
        auto *actions = dialog->findChild<QDialogButtonBox *>("taskResultActions");
        for (const QSize size : {QSize(600, 380), QSize(480, 360), QSize(480, 640)}) {
            dialog->resize(size);
            QCoreApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(*text, *dialog));
            QVERIFY(UiInspection::fullyVisible(*actions, *dialog));
            QVERIFY(UiInspection::separate(*text, *actions, *dialog));
            QVERIFY(UiInspection::writeReport(
                *dialog, "task-result-" + QString(cancelled ? "cancelled-" : "") +
                             (locale == "ru" ? QString("ru") : QString("en")) + '-' +
                             QString::number(size.width()) + 'x' + QString::number(size.height())));
        }
        text->selectAll();
        QTest::keyClicks(text, "replacement");
        QVERIFY(text->toPlainText().contains(review.Result.Error));
        dialog->findChild<QPushButton *>("taskResultCopy")->click();
        QVERIFY(QApplication::clipboard()->text().contains(review.Result.Error));
        QVERIFY(QApplication::clipboard()->text().contains(review.Job.Paths.first()));
        QVERIFY(
            QApplication::clipboard()->text().contains(review.Result.ChangedDirectories.first()));
        QVERIFY(QApplication::clipboard()->text().contains(review.Result.ChangedArchives.first()));
        if (cancelled)
            QVERIFY(QApplication::clipboard()->text().contains(
                QtTaskResultDialog::tr("The operation was cancelled.")));
        QCOMPARE(dialog->windowTitle(), QtTaskResultDialog::tr("Operation result"));
        QCOMPARE(dialog->findChild<QPushButton *>("taskResultCopy")->text(),
                 QtTaskResultDialog::tr("Copy"));
        const auto originalReport = text->toPlainText();
        language(locale == "ru" ? "-" : "ru");
        QCoreApplication::processEvents();
        QCOMPARE(dialog->windowTitle(), QtTaskResultDialog::tr("Operation result"));
        QCOMPARE(dialog->findChild<QPushButton *>("taskResultCopy")->text(),
                 QtTaskResultDialog::tr("Copy"));
        QCOMPARE(text->toPlainText(), originalReport);
    }
};

#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(TaskResultWindowTests)
#include "task_result_window_tests.moc"
