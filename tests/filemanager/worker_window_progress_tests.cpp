#include "dialogs/copy/alias.h"
#include "dialogs/split/SplitDialog.h"
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
    void nextVolumeCommandRunsWhenReady() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath("source");
        const QString first = fixture.filePath("first output");
        const QString second = fixture.filePath("second output");
        QVERIFY(QDir().mkpath(source) && QDir().mkpath(first) && QDir().mkpath(second));
        const QString name = QStringLiteral("данные.bin");
        QFile input(QDir(source).filePath(name));
        QVERIFY(input.open(QIODevice::WriteOnly));
        QCOMPARE(input.write("0123456789abcdef"), qint64(16));
        input.close();
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openPath(source);
        auto *view = table(window);
        auto *split = action(window, "splitFileAction");
        auto *ready = action(window, "createFolderAction");
        const auto selectSource = [&] {
            const int row = filesystemRow(window, name);
            if (row < 0)
                return false;
            view->setCurrentCell(row, 0);
            view->selectRow(row);
            return true;
        };
        QVERIFY(selectSource());
        int dialogs = 0;
        const auto launch = [&](const QString &destination) {
            QTimer::singleShot(0, &window, [&, destination] {
                auto *dialog = window.findChild<QDialog *>("splitDialog");
                if (!dialog)
                    return;
                ++dialogs;
                dialog->findChild<QComboBox *>("volumeDestination")->setCurrentText(destination);
                dialog->findChild<QComboBox *>("volumeSize")->setCurrentText("4B");
                dialog->accept();
            });
            split->trigger();
        };
        bool armed = false, selected = false, requested = false;
        const auto requestNext = [&] {
            if (armed && selected && !requested && split->isEnabled()) {
                requested = true;
                launch(second);
            }
        };
        QObject observer;
        connect(split, &QAction::changed, &observer, requestNext);
        connect(ready, &QAction::changed, &observer, [&] {
            if (!armed || selected || !ready->isEnabled())
                return;
            selected = selectSource();
            requestNext();
        });
        launch(first);
        QCOMPARE(dialogs, 1);
        armed = true;
        QTRY_VERIFY(requested);
        QTRY_COMPARE(dialogs, 2);
        QTRY_VERIFY(QFileInfo::exists(QDir(second).filePath(name + ".004")));
        QTRY_VERIFY(ready->isEnabled());
        QTRY_VERIFY(QtLocalFileTask::Active().isEmpty());
        for (const QString &destination : {first, second}) {
            QFile part(QDir(destination).filePath(name + ".001"));
            QVERIFY(part.open(QIODevice::ReadOnly));
            QCOMPARE(part.readAll(), QByteArray("0123"));
        }
    }
    void nativeWorkerUpdatesVisibleProgressWithoutGuiQueue_data() {
        QTest::addColumn<int>("mode");
        QTest::addColumn<bool>("destroyDuringCallback");
        QTest::newRow("copy") << 0 << false;
        QTest::newRow("split") << 1 << false;
        QTest::newRow("combine") << 2 << false;
        QTest::newRow("copy-close") << 0 << true;
        QTest::newRow("split-close") << 1 << true;
        QTest::newRow("combine-close") << 2 << true;
    }
    void nativeWorkerUpdatesVisibleProgressWithoutGuiQueue() {
        QFETCH(int, mode);
        QFETCH(bool, destroyDuringCallback);
        QtWorkspaceApplication application(*qApp);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString name = QStringLiteral("источник с пробелом.bin");
        const QString selected = name + (mode == 2 ? ".001" : "");
        QVERIFY(writeBlocks(fixture.filePath(selected), mode == 2 ? 32 : 64));
        if (mode == 2)
            QVERIFY(writeBlocks(fixture.filePath(name + ".002"), 32));
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
            auto *dialog = window.findChild<QDialog *>(mode == 1   ? "splitDialog"
                                                       : mode == 2 ? "combineDialog"
                                                                   : "copyDialog");
            if (!dialog)
                return;
            if (mode == 0)
                dialog->findChild<QLineEdit *>("copyDestination")->setText(destination);
            else
                dialog->findChild<QComboBox *>(mode == 1 ? "volumeDestination" : "copyDestination")
                    ->setCurrentText(destination);
            if (mode == 1)
                dialog->findChild<QComboBox *>("volumeSize")->setCurrentText("32M");
            accepted = true;
            dialog->accept();
        });
        action(window, mode == 1   ? "splitFileAction"
                       : mode == 2 ? "combineFilesAction"
                                   : "copyAction")
            ->trigger();
        QVERIFY(accepted);
        auto *worker = mode == 0
                           ? static_cast<QThread *>(qApp->findChild<FileTransferOperation *>())
                           : static_cast<QThread *>(qApp->findChild<VolumeOperation *>());
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
            if (auto *copy = qobject_cast<FileTransferOperation *>(worker))
                connect(
                    copy, &FileTransferOperation::advanced, copy,
                    [hold, &entered](qint64) {
                        entered.release();
                        while (*hold)
                            QThread::msleep(1);
                    },
                    Qt::DirectConnection);
            else {
                auto *volume = qobject_cast<VolumeOperation *>(worker);
                connect(
                    volume, &VolumeOperation::advanced, volume,
                    [hold, &entered](qint64) {
                        entered.release();
                        while (*hold)
                            QThread::msleep(1);
                    },
                    Qt::DirectConnection);
            }
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
        QStringList outputs;
        if (mode == 1)
            outputs = {QDir(destination).filePath(name + ".001"),
                       QDir(destination).filePath(name + ".002")};
        else
            outputs = {QDir(destination).filePath(name)};
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
