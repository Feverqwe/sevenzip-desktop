#include "dialogs/copy/alias.h"
#include "filemanager/support/task_quit_support.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/window/qtlocalfiletask.h"
#include "filemanager/window/qtworkspaceapplication.h"
#include "ui/progress/ProgressDialog2.h"
#include <QAbstractEventDispatcher>
#include <QCryptographicHash>
#include <QScopeGuard>
#include <QSemaphore>
#include <QTimerEvent>
#include <thread>

namespace {
class MetaCallCounter final : public QObject {
  public:
    int calls = 0;

  protected:
    bool eventFilter(QObject *, QEvent *event) override {
        calls += event->type() == QEvent::MetaCall;
        return false;
    }
};
} // namespace

class WorkerWindowProgressTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    static bool writeBlocks(const QString &path, int count) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return false;
        const QByteArray block(1 << 20, 'p');
        for (int i = 0; i < count; ++i)
            if (file.write(block) != block.size())
                return false;
        return file.flush();
    }
  private slots:
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }
    void nativeWorkerUpdatesVisibleProgressWithoutGuiQueue_data() {
        QTest::addColumn<bool>("destroyDuringCallback");
        QTest::newRow("copy") << false;
        QTest::newRow("copy-close") << true;
    }
    void nativeWorkerUpdatesVisibleProgressWithoutGuiQueue() {
        QFETCH(bool, destroyDuringCallback);
        QtWorkspaceApplication application(*qApp);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString name = QStringLiteral("источник с пробелом.bin");
        const QString selected = name;
        QVERIFY(writeBlocks(fixture.filePath(selected), 64));
        const QString destination = fixture.filePath(QStringLiteral("папка назначения"));
        QVERIFY(QDir().mkpath(destination));
        ControlledBackend backend;
        auto owner = std::make_unique<MainWindow>(&backend);
        MainWindow &window = *owner;
        window.show();
        auto *address = window.findChild<QLineEdit *>("archivePath");
        address->setText(fixture.path());
        QTest::keyClick(address, Qt::Key_Return);
        const int row = filesystemRow(window, selected);
        QVERIFY(row >= 0);
        table(window)->selectRow(row);
        table(window)->setCurrentCell(row, 0);
        bool accepted = false;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<QDialog *>("copyDialog");
            if (!dialog)
                return;
            dialog->findChild<QLineEdit *>("copyDestination")->setText(destination);
            accepted = true;
            dialog->accept();
        });
        action(window, "copyAction")->trigger();
        QVERIFY(accepted);
        auto *worker = qApp->findChild<FileTransferOperation *>();
        QVERIFY(worker);
        CProgressDialog *progress = nullptr;
        for (auto *widget : QApplication::topLevelWidgets())
            if (widget->isVisible())
                if (auto *dialog = dynamic_cast<CProgressDialog *>(widget))
                    progress = dialog;
        QVERIFY(progress);
        if (destroyDuringCallback) {
            QSignalSpy completed(QtLocalFileTask::Active().first(), &QtLocalFileTask::finished);
            const QPointer<QThread> retainedWorker(worker);
            QSemaphore entered;
            auto hold = std::make_shared<std::atomic<bool>>(true);
            const auto release = qScopeGuard([&] {
                *hold = false;
                if (retainedWorker)
                    retainedWorker->wait();
            });
            connect(
                worker, &FileTransferOperation::advanced, worker,
                [hold, &entered](qint64) {
                    entered.release();
                    while (*hold)
                        QThread::msleep(1);
                },
                Qt::DirectConnection);
            QVERIFY(entered.tryAcquire(1, 2000));
            const QPointer<QDialog> retainedProgress(progress);
            progress->showMinimized();
            ShowTasksOnQuit(application);
            QVERIFY(window.isVisible());
            QVERIFY(!progress->isMinimized());
            QVERIFY(worker->isRunning());
            QVERIFY(!window.close());
            std::thread releaseWorker([hold] {
                QThread::msleep(50);
                *hold = false;
            });
            owner.reset();
            releaseWorker.join();
            QVERIFY(retainedWorker);
            QVERIFY(!retainedWorker->isRunning());
            QVERIFY(!retainedProgress || !retainedProgress->isVisible());
            QVERIFY(QFileInfo::exists(fixture.filePath(selected)));
            QTRY_VERIFY(QtLocalFileTask::Active().isEmpty());
            QVERIFY(completed.isEmpty());
            return;
        }
        MetaCallCounter counter;
        if (owner)
            window.installEventFilter(&counter);
        // Join the public worker without pumping GUI events. Its source progress
        // snapshot must already be usable by the dialog's real timer handler.
        QVERIFY(worker->wait(30000));
        bool timerDelivered = false;
        for (const auto &timer : QAbstractEventDispatcher::instance()->registeredTimers(progress)) {
            QTimerEvent event(timer.timerId);
            QCoreApplication::sendEvent(progress, &event);
            timerDelivered = true;
        }
        QVERIFY(timerDelivered);
        auto *file = progress->findChild<QLabel *>("IDT_PROGRESS_FILE_NAME");
        QVERIFY(file);
        const QString currentFile = file->text();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        if (owner)
            window.removeEventFilter(&counter);
        QVERIFY(currentFile.contains(name));
        QVERIFY2(counter.calls <= 4,
                 qPrintable(QString("%1 queued window callbacks").arg(counter.calls)));
        if (owner)
            QTRY_VERIFY(action(window, "createFolderAction")->isEnabled());
        QTRY_VERIFY(QtLocalFileTask::Active().isEmpty());
        const QStringList outputs = {QDir(destination).filePath(name)};
        QCryptographicHash actual(QCryptographicHash::Sha256), expected(QCryptographicHash::Sha256);
        const QByteArray block(1 << 20, 'p');
        for (int i = 0; i < 64; ++i)
            expected.addData(block);
        qint64 total = 0;
        for (const QString &path : outputs) {
            QFile output(path);
            QVERIFY(output.open(QIODevice::ReadOnly));
            total += output.size();
            QVERIFY(actual.addData(&output));
        }
        QCOMPARE(total, qint64(64 << 20));
        QCOMPARE(actual.result(), expected.result());
        QVERIFY(QFileInfo::exists(fixture.filePath(selected)));
        QVERIFY(
            QDir(destination).entryList({"*.sevenzip-*"}, QDir::Files | QDir::Hidden).isEmpty());
    }
};
QTEST_MAIN(WorkerWindowProgressTests)
#include "worker_window_progress_tests.moc"
