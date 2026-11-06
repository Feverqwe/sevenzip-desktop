#include "archive/support/source_extract_fixture.h"
#include "filemanager/support/task_quit_support.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/window/qtworkspaceapplication.h"
#include "support/operation_kill.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QSignalSpy>

class TaskLifecycleWindowTests final : public QObject {
    Q_OBJECT
  private slots:
    void lastFileWindowDoesNotEndTheTaskOwner() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const auto archive = SourceExtractFixture::makeArchive(fixture.path(), "retained bytes");
        QVERIFY(!archive.isEmpty());
        const auto release = fixture.filePath("release");
        qputenv("SEVENZIP_OPERATION_WINDOW_REPORT", fixture.filePath("task.json").toUtf8());
        qputenv("SEVENZIP_OPERATION_RELEASE_WINDOW", release.toUtf8());
        const bool quit = qApp->quitOnLastWindowClosed();
        qApp->setQuitOnLastWindowClosed(true);
        const auto restore = qScopeGuard([quit] {
            qunsetenv("SEVENZIP_OPERATION_WINDOW_REPORT");
            qunsetenv("SEVENZIP_OPERATION_RELEASE_WINDOW");
            qApp->setQuitOnLastWindowClosed(quit);
        });
        QtWorkspaceApplication application(*qApp);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openPath(fixture.path());
        QtOperationJob job;
        job.Operation = QtOperationJob::Command::Extract;
        job.Paths = {archive};
        job.Destination = fixture.filePath("unapplied extraction");
        QString error;
        QPointer<QtOperationLauncher> operation =
            QtOperationLauncher::Launch(QStringLiteral(SEVENZIP_OPERATION_HOST), job, error);
        QVERIFY2(operation, qPrintable(error));
        const auto stopOnFailure = qScopeGuard([&] {
            if (operation && operation->active())
                KillOperationProbe(operation->processId());
        });
        QSignalSpy finished(operation, &QtOperationLauncher::finished);
        QTRY_VERIFY_WITH_TIMEOUT(
            operation && operation->presentation().Phase == QtOperationPhase::Draft, 15000);
        bool closed = false, retained = false, timedOut = false, released = false;
        QTimer::singleShot(0, &window, [&] { closed = window.close(); });
        QTimer releaseTask;
        releaseTask.setSingleShot(true);
        connect(&releaseTask, &QTimer::timeout, &window, [&] {
            retained = closed && !window.isVisible() && operation && operation->active() &&
                       finished.isEmpty();
            released = SourceExtractFixture::writeFile(release, "cancel form");
        });
        releaseTask.start(150);
        QTimer deadline;
        deadline.setSingleShot(true);
        connect(&deadline, &QTimer::timeout, &window, [&] {
            timedOut = true;
            qApp->quit();
        });
        deadline.start(5000);
        QCOMPARE(qApp->exec(), 0);
        QVERIFY(closed);
        QVERIFY(retained);
        QVERIFY(released);
        QVERIFY(!timedOut);
        QCOMPARE(finished.size(), 1);
        QVERIFY(!QFileInfo::exists(job.Destination));
    }
    void quitShowsIndependentTaskWithoutClosingPeers() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const auto archive = SourceExtractFixture::makeArchive(fixture.path(), "unchanged bytes");
        QVERIFY(!archive.isEmpty());
        const auto report = fixture.filePath("task.json");
        const auto release = fixture.filePath("release");
        qputenv("SEVENZIP_OPERATION_WINDOW_REPORT", report.toUtf8());
        qputenv("SEVENZIP_OPERATION_RELEASE_WINDOW", release.toUtf8());
        const auto restore = qScopeGuard([] {
            qunsetenv("SEVENZIP_OPERATION_WINDOW_REPORT");
            qunsetenv("SEVENZIP_OPERATION_RELEASE_WINDOW");
        });
        QtWorkspaceApplication application(*qApp);
        ControlledBackend firstBackend, secondBackend;
        MainWindow first(&firstBackend), second(&secondBackend);
        first.show();
        second.show();
        first.openPath(fixture.path());
        second.openPath(fixture.path());
        QtOperationJob job;
        job.Operation = QtOperationJob::Command::Hash;
        job.Paths = {archive};
        job.HashMethod = "SHA256";
        QString error;
        QPointer<QtOperationLauncher> operation =
            QtOperationLauncher::Launch(QStringLiteral(SEVENZIP_OPERATION_HOST), job, error);
        QVERIFY2(operation, qPrintable(error));
        const auto stopOnFailure = qScopeGuard([&] {
            if (operation && operation->active())
                KillOperationProbe(operation->processId());
        });
        QSignalSpy finished(operation, &QtOperationLauncher::finished);
        QTRY_VERIFY_WITH_TIMEOUT(
            operation && operation->presentation().Phase == QtOperationPhase::Reviewing, 15000);
        const auto minimized = [&] {
            return QJsonDocument::fromJson(SourceExtractFixture::readFile(report))
                .object()
                .value("minimized");
        };
        QTRY_VERIFY(minimized().toBool());
        ShowTasksOnQuit(application);
        QVERIFY(first.isVisible() && second.isVisible());
        QTRY_VERIFY(minimized().isBool() && !minimized().toBool());
        QVERIFY(finished.isEmpty());
        QVERIFY(SourceExtractFixture::writeFile(release, "cancel form"));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 15000);
    }
};

QTEST_MAIN(TaskLifecycleWindowTests)
#include "task_lifecycle_window_tests.moc"
