#include "dialogs/overwrite/OverwriteDialog.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/window/qtlocalfiletask.h"
#include "filemanager/window/qtworkspaceapplication.h"
#include "localization/LangUtils.h"
#include "settings/RegistryUtils.h"
#include "support/widget_dialogs.h"
#include "support/window_activation.h"
#include "ui/lifecycle/qttaskpresentation.h"
#include "ui/progress/ProgressDialog2.h"
#include "ui/support/layout_inspection.h"
#include <QClipboard>
#include <QScopeGuard>
#include <QSemaphore>
#include <QSignalSpy>
#include <atomic>
#include <thread>

class LocalTaskLifecycleTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    WidgetDialogFixture dialogs_;
    static QByteArray read(const QString &path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }
    static bool write(const QString &path, const QByteArray &bytes) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    }
  private slots:
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }
    void cleanup() {
        QTimer answer;
        connect(&answer, &QTimer::timeout, this, [] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                box->accept();
            if (auto *question =
                    dynamic_cast<COverwriteDialog *>(QApplication::activeModalWidget()))
                question->reject();
        });
        answer.start(5);
        for (auto *task : QtLocalFileTask::Active()) {
            if (auto *copy = task->findChild<FileTransferOperation *>()) {
                copy->cancel();
                copy->wait();
            }
            if (auto *volume = task->findChild<VolumeOperation *>()) {
                volume->cancel();
                volume->wait();
            }
        }
        QTRY_VERIFY(QtLocalFileTask::Active().isEmpty());
        QApplication::clipboard()->clear();
        SaveRegLang(UString("-"), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
    }
    void overwriteKeepsOwnerUntilAnswered_data() {
        QTest::addColumn<QString>("choice");
        QTest::addColumn<QString>("locale");
        for (const QString &locale : {QString("-"), QString("ru")})
            for (const QString &choice :
                 {QString("overwriteYes"), QString("overwriteNo"), QString("overwriteCancel")})
                QTest::newRow(qPrintable(locale + '-' + choice)) << choice << locale;
    }
    void overwriteKeepsOwnerUntilAnswered() {
        QFETCH(QString, choice);
        QFETCH(QString, locale);
        SaveRegLang(ToUString(locale), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir fixture;
        const auto source = fixture.filePath("source 空");
        const auto destination = fixture.filePath("destination with spaces");
        const QString name = QStringLiteral("данные.txt");
        QVERIFY(QDir().mkpath(source) && QDir().mkpath(destination));
        QVERIFY(write(source + '/' + name, "new bytes"));
        QVERIFY(write(destination + '/' + name, "old bytes"));
        QtWorkspaceApplication application(*qApp);
        ControlledBackend sourceBackend, destinationBackend;
        auto sourceWindow = std::make_unique<MainWindow>(&sourceBackend);
        auto destinationWindow = std::make_unique<MainWindow>(&destinationBackend);
        sourceWindow->show();
        destinationWindow->show();
        sourceWindow->openPath(source);
        destinationWindow->openPath(destination);
        QTRY_VERIFY(filesystemRow(*sourceWindow, name) >= 0);
        QTRY_VERIFY(filesystemRow(*destinationWindow, name) >= 0);
        const int row = filesystemRow(*sourceWindow, name);
        QVERIFY(row >= 0);
        table(*sourceWindow)->selectRow(row);
        table(*sourceWindow)->setCurrentCell(row, 0);
        auto *cut = action(*sourceWindow, "cutFilesAction");
        QVERIFY(cut->isEnabled());
        cut->trigger();
        const QPointer<const QMimeData> original(QApplication::clipboard()->mimeData());
        auto *paste = action(*destinationWindow, "pasteFilesAction");
        QVERIFY(paste->isEnabled());
        paste->trigger();
        const auto tasks = QtLocalFileTask::Active();
        QCOMPARE(tasks.size(), 1);
        QSignalSpy finished(tasks.first(), &QtLocalFileTask::finished);
        bool answered = false, closed = false, retained = false, contextModal = false;
        bool reviewed = false;
        QTimer drive;
        connect(&drive, &QTimer::timeout, &application, [&] {
            auto *modal = QApplication::activeModalWidget();
            if (auto *question = dynamic_cast<COverwriteDialog *>(modal);
                question && !answered && dialogs_.ready(*question)) {
                answered = true;
                contextModal = question->windowModality() == Qt::WindowModal;
                QVERIFY(UiInspection::writeReport(*question,
                                                  "local-task-question-" + locale + '-' + choice));
                closed = sourceWindow->close() && !destinationWindow->close();
                sourceWindow.reset();
                retained = question->isVisible() && destinationWindow->isVisible();
                question->findChild<QPushButton *>(choice)->click();
            } else if (auto *report = qobject_cast<QMessageBox *>(modal);
                       report && report->objectName() == "progressFinalMessage" && !reviewed &&
                       dialogs_.ready(*report)) {
                reviewed = true;
                drive.stop();
                contextModal &= report->windowModality() == Qt::WindowModal;
                QVERIFY(UiInspection::writeReport(*report,
                                                  "local-task-result-" + locale + '-' + choice));
                QTimer::singleShot(150, report, [&, report] {
                    retained &= report->isVisible() && !QtLocalFileTask::Active().isEmpty() &&
                                QApplication::clipboard()->mimeData() == original;
                    report->accept();
                });
            }
        });
        drive.start(5);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
        drive.stop();
        QVERIFY(answered && closed && retained && contextModal);
        QCOMPARE(reviewed, choice != "overwriteYes");
        QCOMPARE(finished.size(), 1);
        QCOMPARE(finished[0][0].toBool(), choice != "overwriteCancel");
        QCOMPARE(finished[0][1].toBool(), choice == "overwriteCancel");
        QCOMPARE(read(destination + '/' + name),
                 choice == "overwriteYes" ? QByteArray("new bytes") : QByteArray("old bytes"));
        QCOMPARE(QFileInfo::exists(source + '/' + name), choice != "overwriteYes");
        if (choice == "overwriteYes")
            QVERIFY(!QApplication::clipboard()->mimeData() ||
                    !QApplication::clipboard()->mimeData()->hasUrls());
        else {
            QCOMPARE(QApplication::clipboard()->mimeData(), original.data());
            QCOMPARE(read(source + '/' + name), QByteArray("new bytes"));
        }
    }
    void destroyingOwnerDuringModalDoesNotPublishAfterClose_data() {
        QTest::addColumn<bool>("result");
        QTest::newRow("overwrite") << false;
        QTest::newRow("result") << true;
    }
    void destroyingOwnerDuringModalDoesNotPublishAfterClose() {
        QFETCH(bool, result);
        QTemporaryDir fixture;
        const auto source = fixture.filePath("source 空");
        const auto destination = fixture.filePath("destination with spaces");
        const QString name = QStringLiteral("данные.txt");
        QVERIFY(QDir().mkpath(source) && QDir().mkpath(destination));
        QVERIFY(write(source + '/' + name, "new bytes"));
        QVERIFY(write(destination + '/' + name, "old bytes"));
        ControlledBackend sourceBackend, destinationBackend;
        MainWindow peer(&sourceBackend);
        auto owner = std::make_unique<MainWindow>(&destinationBackend);
        peer.show();
        owner->show();
        peer.openPath(source);
        owner->openPath(destination);
        QTRY_VERIFY(filesystemRow(peer, name) >= 0);
        QTRY_VERIFY(filesystemRow(*owner, name) >= 0);
        const int row = filesystemRow(peer, name);
        QVERIFY(row >= 0);
        table(peer)->selectRow(row);
        table(peer)->setCurrentCell(row, 0);
        action(peer, "cutFilesAction")->trigger();
        const QPointer<const QMimeData> original(QApplication::clipboard()->mimeData());
        action(*owner, "pasteFilesAction")->trigger();
        const auto tasks = QtLocalFileTask::Active();
        QCOMPARE(tasks.size(), 1);
        const QPointer<FileTransferOperation> worker =
            tasks.first()->findChild<FileTransferOperation *>();
        QVERIFY(worker);
        QSignalSpy completed(tasks.first(), &QtLocalFileTask::transferCompleted);
        QSignalSpy finished(tasks.first(), &QtLocalFileTask::finished);
        bool destroyed = false, joined = false;
        QTimer drive;
        connect(&drive, &QTimer::timeout, &peer, [&] {
            if (destroyed)
                return;
            auto *question = dynamic_cast<COverwriteDialog *>(QApplication::activeModalWidget());
            if (question && dialogs_.ready(*question) && result) {
                question->findChild<QPushButton *>("overwriteNo")->click();
                return;
            }
            auto *report = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (result ? (!report || report->objectName() != "progressFinalMessage" ||
                          !dialogs_.ready(*report))
                       : (!question || !dialogs_.ready(*question)))
                return;
            destroyed = true;
            owner.reset();
            joined = !worker || !worker->isRunning();
        });
        drive.start(5);
        QTRY_VERIFY_WITH_TIMEOUT(destroyed, 10000);
        drive.stop();
        QVERIFY(joined);
        QTRY_VERIFY(QtLocalFileTask::Active().isEmpty());
        QCOMPARE(completed.size(), result ? 1 : 0);
        if (result)
            QVERIFY(!completed[0][0].toBool());
        QVERIFY(finished.isEmpty());
        QVERIFY(peer.isVisible());
        QTRY_VERIFY(action(peer, "createFolderAction")->isEnabled());
        QCOMPARE(QApplication::clipboard()->mimeData(), original.data());
        QCOMPARE(read(source + '/' + name), QByteArray("new bytes"));
        QCOMPARE(read(destination + '/' + name), QByteArray("old bytes"));
    }
    void destroyingOwnerFromCancelQuestionJoinsBeforeReturning() {
        QTemporaryDir fixture;
        const auto source = fixture.filePath("source.txt");
        const auto destination = fixture.filePath("output");
        QVERIFY(write(source, "original bytes"));
        QVERIFY(QDir().mkpath(destination));
        ControlledBackend ownerBackend, peerBackend;
        auto owner = std::make_unique<MainWindow>(&ownerBackend);
        MainWindow peer(&peerBackend);
        owner->show();
        peer.show();
        owner->openPath(fixture.path());
        peer.openPath(fixture.path());
        QSemaphore entered;
        auto hold = std::make_shared<std::atomic<bool>>(true);
        FileTransferOperation::FileSystemAdapter adapter;
        adapter.trySourceRename = [hold, &entered](const QString &, const QString &) {
            entered.release();
            while (*hold)
                QThread::msleep(1);
            return false;
        };
        const QPointer<FileTransferOperation> worker =
            new FileTransferOperation({source}, destination, true, nullptr, adapter);
        const auto release = qScopeGuard([&] {
            *hold = false;
            if (worker)
                worker->wait();
        });
        auto *task = new QtLocalFileTask(*worker, "Move", destination);
        task->BindOwner(*table(*owner)->sourcePanel, *owner);
        QSignalSpy completed(task, &QtLocalFileTask::transferCompleted);
        QSignalSpy finished(task, &QtLocalFileTask::finished);
        task->Start();
        QVERIFY(entered.tryAcquire(1, 2000));
        auto *progress = owner->findChild<QDialog *>("progressDialog");
        QVERIFY(progress);
        bool destroyed = false, joined = false;
        QTimer answer;
        connect(&answer, &QTimer::timeout, &peer, [&] {
            auto *question = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!question || destroyed || question->objectName() != "progressCancelQuestion" ||
                !dialogs_.ready(*question))
                return;
            destroyed = true;
            std::thread releaseWorker([hold] {
                QThread::msleep(50);
                *hold = false;
            });
            owner.reset();
            releaseWorker.join();
            joined = !worker || !worker->isRunning();
        });
        answer.start(5);
        progress->findChild<QPushButton *>("IDCANCEL")->click();
        answer.stop();
        QVERIFY(destroyed && joined);
        QTRY_VERIFY(QtLocalFileTask::Active().isEmpty());
        QVERIFY(completed.isEmpty() && finished.isEmpty());
        QCOMPARE(read(source), QByteArray("original bytes"));
        QVERIFY(!QFileInfo::exists(QDir(destination).filePath("source.txt")));
        QVERIFY(peer.isVisible());
    }
    void lateStopDoesNotHideVolumeFailure_data() {
        QTest::addColumn<bool>("nativeQuestion");
        QTest::newRow("ready") << false;
        QTest::newRow("native-question-without-widget-modal") << true;
    }
    void lateStopDoesNotHideVolumeFailure() {
        QFETCH(bool, nativeQuestion);
        QTemporaryDir fixture;
        auto *worker = new VolumeOperation(VolumeOperation::Mode::Split,
                                           fixture.filePath("missing input"), fixture.path(), 4);
        auto *task = new QtLocalFileTask(*worker, "Split", fixture.filePath("missing input"), 8);
        connect(
            worker, &VolumeOperation::result, worker, [worker] { worker->cancel(); },
            Qt::DirectConnection);
        QSignalSpy finished(task, &QtLocalFileTask::finished);
        bool shown = false;
        QTimer answer;
        connect(&answer, &QTimer::timeout, this, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                box && box->objectName() == "progressFinalMessage") {
                shown = true;
                box->accept();
            }
        });
        answer.start(5);
        std::unique_ptr<QtTaskPresentation::QuestionScope> question;
        if (nativeQuestion)
            question = std::make_unique<QtTaskPresentation::QuestionScope>();
        task->Start();
        if (question) {
            QVERIFY(worker->wait(10000));
            QTest::qWait(100);
            QVERIFY(finished.isEmpty());
            QVERIFY(!shown);
            question.reset(); // Presentation must resume without a focusChanged event.
        }
        QTRY_COMPARE(finished.size(), 1);
        QVERIFY(shown);
        QVERIFY(!finished[0][0].toBool());
        QVERIFY(!finished[0][1].toBool());
        QVERIFY(!finished[0][2].toString().isEmpty());
    }
    void lateFailurePreservesAnotherWindowsDraft_data() {
        QTest::addColumn<bool>("closeTask");
        QTest::newRow("explicit-show") << false;
        QTest::newRow("close-completed-task") << true;
    }
    void lateFailurePreservesAnotherWindowsDraft() {
        QFETCH(bool, closeTask);
        QTemporaryDir fixture;
        const QString source = fixture.filePath("source.txt");
        const QString destination = fixture.filePath("output");
        QVERIFY(write(source, "unchanged bytes"));
        QVERIFY(QDir().mkpath(destination));
        QSemaphore entered, release;
        FileTransferOperation::FileSystemAdapter adapter;
        adapter.trySourceRename = [&](const QString &, const QString &) {
            entered.release();
            release.acquire();
            return false;
        };
        adapter.removeSource = [](const QString &) { return false; };
        QPointer<FileTransferOperation> worker =
            new FileTransferOperation({source}, destination, true, nullptr, adapter);
        const auto unblock = qScopeGuard([&] {
            release.release();
            if (worker)
                worker->wait();
        });
        auto *task = new QtLocalFileTask(*worker, "Move", destination);
        QSignalSpy finished(task, &QtLocalFileTask::finished);
        task->Start();
        QVERIFY(entered.tryAcquire(1, 2000));
        QPointer<CProgressDialog> progress;
        for (auto *widget : QApplication::topLevelWidgets())
            if (auto *candidate = dynamic_cast<CProgressDialog *>(widget);
                candidate && candidate->isVisible())
                progress = candidate;
        QVERIFY(progress);
        progress->showMinimized();
        int commits = 0;
        QWidget peer;
        QLineEdit field(&peer);
        peer.show();
        QVERIFY(WindowActivation::waitForActive(peer));
        field.setFocus();
        QTRY_VERIFY(field.hasFocus());
        QTest::keyClicks(&field, "untouched draft");
        field.setSelection(2, 5);
        const auto selection = field.selectedText();
        const auto cursor = field.cursorPosition();
        connect(&field, &QLineEdit::editingFinished, &peer, [&] { ++commits; });
        release.release();
        QVERIFY(worker->wait(10000));
        QTest::qWait(100);
        QVERIFY(finished.isEmpty());
        QVERIFY(!QApplication::activeModalWidget());
        QCOMPARE(field.text(), QString("untouched draft"));
        QCOMPARE(field.selectedText(), selection);
        QCOMPARE(field.cursorPosition(), cursor);
        QVERIFY(field.hasFocus());
        QCOMPARE(commits, 0);
        QTimer answer;
        bool reviewed = false, restored = false, askedToCancel = false;
        connect(&answer, &QTimer::timeout, &peer, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                box && box->objectName() == "progressFinalMessage") {
                reviewed = true;
                restored = progress && !progress->isMinimized();
                box->accept();
            } else if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                       box && box->objectName() == "progressCancelQuestion") {
                askedToCancel = true;
                box->button(QMessageBox::No)->click();
            }
        });
        answer.start(5);
        if (closeTask)
            progress->close();
        else
            task->Show();
        QCOMPARE(finished.size(), 1);
        QVERIFY(reviewed);
        QVERIFY(restored);
        QVERIFY(!askedToCancel);
        QVERIFY(!finished[0][0].toBool());
        QCOMPARE(read(source), QByteArray("unchanged bytes"));
    }
};

#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(LocalTaskLifecycleTests)
#include "local_task_lifecycle_tests.moc"
