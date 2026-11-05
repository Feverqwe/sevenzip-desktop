#include "archive/codecs/archivecodelocation.h"
#include "archive/support/source_extract_fixture.h"
#include "operations/qtoperationlauncher.h"
#include "support/operation_kill.h"
#include "support/operation_parent.h"
#include "support/operation_program.h"
#include "support/operation_temp_cleanup.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class OperationLauncherTests final : public QObject {
    Q_OBJECT
    QTemporaryDir settings_;
    QByteArray oldSettings_;
    static QString guiProgram() { return NativeOperationProgram(SEVENZIP_NATIVE_GUI); }
  private slots:
    void cleanup() { QVERIFY(CleanupOperationTestTemps()); }
    void initTestCase() {
        ConfigureInstalledOperationPlatform();
        QVERIFY(settings_.isValid());
        oldSettings_ = qgetenv("SEVENZIP_SETTINGS_PATH");
        qputenv("SEVENZIP_SETTINGS_PATH", settings_.filePath("settings.ini").toUtf8());
    }
    void cleanupTestCase() {
        if (oldSettings_.isNull())
            qunsetenv("SEVENZIP_SETTINGS_PATH");
        else
            qputenv("SEVENZIP_SETTINGS_PATH", oldSettings_);
    }
    void taskWindowCanBeShownUntilReviewed_data() {
        QTest::addColumn<bool>("draft");
        QTest::newRow("unstarted-extract-form") << true;
        QTest::newRow("completed-hash-awaiting-review") << false;
    }
    void taskWindowCanBeShownUntilReviewed() {
        QFETCH(bool, draft);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto archive = SourceExtractFixture::makeArchive(directory.path(), "retained bytes");
        QVERIFY(!archive.isEmpty());
        const auto original = SourceExtractFixture::readFile(archive);
        const auto report = directory.filePath("window.json");
        const auto release = directory.filePath("release");
        qputenv("SEVENZIP_OPERATION_WINDOW_REPORT", report.toUtf8());
        qputenv("SEVENZIP_OPERATION_RELEASE_WINDOW", release.toUtf8());
        const auto restore = qScopeGuard([] {
            qunsetenv("SEVENZIP_OPERATION_WINDOW_REPORT");
            qunsetenv("SEVENZIP_OPERATION_RELEASE_WINDOW");
        });
        QtOperationJob job;
        job.Operation = draft ? QtOperationJob::Command::Extract : QtOperationJob::Command::Hash;
        job.Paths = {archive};
        job.Destination = directory.filePath("unapplied extraction");
        job.HashMethod = QStringLiteral("SHA256");
        QString error;
        QPointer<QtOperationLauncher> launcher =
            QtOperationLauncher::Launch(QStringLiteral(SEVENZIP_OPERATION_HOST), job, error);
        QVERIFY2(launcher, qPrintable(error));
        const auto stopOnFailure = qScopeGuard([&] {
            if (launcher && launcher->active())
                KillOperationProbe(launcher->processId());
        });
        QSignalSpy finished(launcher, &QtOperationLauncher::finished);
        const auto expected = draft ? QtOperationPhase::Draft : QtOperationPhase::Reviewing;
        QTRY_VERIFY_WITH_TIMEOUT(launcher && launcher->presentation().Phase == expected, 15000);
        QVERIFY(launcher->presentation().HasWindow);
        QVERIFY(QtOperationLauncher::Active().contains(launcher));
        const auto windowState = [&] {
            return QJsonDocument::fromJson(SourceExtractFixture::readFile(report)).object();
        };
        QTRY_VERIFY(windowState().value("minimized").toBool());
        QVERIFY(finished.isEmpty());
        launcher->Show();
        QTRY_VERIFY(windowState().contains("minimized") &&
                    !windowState().value("minimized").toBool());
        QVERIFY(finished.isEmpty());
        QCOMPARE(SourceExtractFixture::readFile(archive), original);
        QVERIFY(!QFileInfo::exists(job.Destination));
        QVERIFY(SourceExtractFixture::writeFile(release, "close the task window"));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 15000);
        const auto result = qvariant_cast<QtOperationResult>(finished[0][0]);
        QCOMPARE(result.Status, draft ? QtOperationResult::Outcome::Cancelled
                                      : QtOperationResult::Outcome::Success);
        QVERIFY(!result.CancellationNeedsReview);
    }
    void nativeTest_data() {
        QTest::addColumn<bool>("damaged");
        QTest::newRow("healthy archive without console") << false;
        QTest::newRow("damaged archive without console") << true;
    }
    void nativeTest() {
#ifndef SEVENZIP_CODEC_MODULE
        QSKIP("No native runtime module configured");
#else
        QFETCH(bool, damaged);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto original =
            SourceExtractFixture::makeArchive(directory.path(), "native IPC bytes");
        QVERIFY(!original.isEmpty());
#ifdef Q_OS_WIN
        const auto archive = directory.filePath(QStringLiteral("архив 空 with spaces.zip"));
#else
        const auto archive = directory.filePath(QStringLiteral("архив 空 with \"quotes\".zip"));
#endif
        QVERIFY(QFile::rename(original, archive));
        if (damaged)
            QVERIFY(SourceExtractFixture::writeFile(archive, "invalid ZIP"));
        const auto before = SourceExtractFixture::readFile(archive);
        const auto deployment = directory.filePath("native runtime");
        QVERIFY(QDir().mkpath(deployment));
        const QString module = QStringLiteral(SEVENZIP_CODEC_MODULE);
        QVERIFY(QFile::copy(module, QDir(deployment).filePath(QFileInfo(module).fileName())));
        const QByteArray oldEngine = qgetenv("SEVENZIP_CODEC_MODULE_PATH");
        const auto restore = qScopeGuard([&] {
            if (oldEngine.isNull())
                qunsetenv("SEVENZIP_CODEC_MODULE_PATH");
            else
                qputenv("SEVENZIP_CODEC_MODULE_PATH", oldEngine);
        });
        qputenv("SEVENZIP_CODEC_MODULE_PATH", FindArchiveModule(deployment).toUtf8());
        QtOperationLauncher launcher;
        QSignalSpy accepted(&launcher, &QtOperationLauncher::accepted);
        QSignalSpy finished(&launcher, &QtOperationLauncher::finished);
        bool finishedBeforeAcceptance = false;
        connect(&launcher, &QtOperationLauncher::finished, this,
                [&] { finishedBeforeAcceptance = accepted.isEmpty(); });
        QtOperationJob job;
        job.Paths = {archive};
        job.YesToAll = true; // Original source -y behavior, no terminal modal in this fixture.
        QString error;
        QVERIFY2(launcher.start(guiProgram(), job, error), qPrintable(error));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 30000);
        QCOMPARE(accepted.size(), 1);
        QVERIFY(!finishedBeforeAcceptance);
        const auto result = qvariant_cast<QtOperationResult>(finished[0][0]);
        QCOMPARE(result.Status, damaged ? QtOperationResult::Outcome::Failure
                                        : QtOperationResult::Outcome::Success);
        QCOMPARE(result.ExitCode, damaged ? 2 : 0);
        QCOMPARE(SourceExtractFixture::readFile(archive), before);
        QCOMPARE(SourceExtractFixture::readFile(directory.filePath(SourceExtractFixture::kName)),
                 QByteArray("native IPC bytes"));
#endif
    }
    void acceptedJobSurvivesParentExit() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto archive = SourceExtractFixture::makeArchive(directory.path(), "surviving bytes");
        QVERIFY(!archive.isEmpty());
        const auto before = SourceExtractFixture::readFile(archive);
        const auto barrier = directory.filePath("continue");
        const auto report = directory.filePath("result");
        QProcess parent;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("SEVENZIP_OPERATION_BARRIER", barrier);
        environment.insert("SEVENZIP_OPERATION_REPORT", report);
        environment.insert("SEVENZIP_OPERATION_ARCHIVE", archive);
        parent.setProcessEnvironment(environment);
        parent.start(QCoreApplication::applicationFilePath(), {"--probe-parent"});
        QVERIFY(parent.waitForStarted(5000));
        QVERIFY(parent.waitForFinished(15000));
        QCOMPARE(parent.exitStatus(), QProcess::NormalExit);
        QCOMPARE(parent.exitCode(), 0);      // Parent exits only after the child's ACK.
        QVERIFY(!QFileInfo::exists(report)); // Native work is held until parent exit.
        QVERIFY(SourceExtractFixture::writeFile(barrier, "continue"));
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(report), 15000);
        QVERIFY2(SourceExtractFixture::readFile(report) == QByteArray("0"),
                 SourceExtractFixture::readFile(report + ".diagnostic").constData());
        QCOMPARE(SourceExtractFixture::readFile(archive), before);
    }
    void acceptedJobSurvivesObserverDeletion() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto archive = SourceExtractFixture::makeArchive(directory.path(), "surviving bytes");
        QVERIFY(!archive.isEmpty());
        const auto barrier = directory.filePath("continue");
        const auto report = directory.filePath("result");
        qputenv("SEVENZIP_OPERATION_BARRIER", barrier.toUtf8());
        qputenv("SEVENZIP_OPERATION_REPORT", report.toUtf8());
        const auto restore = qScopeGuard([] {
            qunsetenv("SEVENZIP_OPERATION_BARRIER");
            qunsetenv("SEVENZIP_OPERATION_REPORT");
        });
        QPointer<QtOperationLauncher> observer = new QtOperationLauncher(this);
        connect(observer, &QtOperationLauncher::accepted, this, [&] { delete observer.data(); });
        QtOperationJob job;
        job.Paths = {archive};
        job.YesToAll = true;
        QString error;
        QVERIFY2(observer->start(QStringLiteral(SEVENZIP_OPERATION_HOST), job, error),
                 qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(observer.isNull(), 10000);
        QVERIFY(!QFileInfo::exists(report));
        QVERIFY(SourceExtractFixture::writeFile(barrier, "continue"));
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(report), 15000);
        QVERIFY2(SourceExtractFixture::readFile(report) == QByteArray("0"),
                 SourceExtractFixture::readFile(report + ".diagnostic").constData());
    }
    void killedChildAndNextOperation() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto archive = SourceExtractFixture::makeArchive(directory.path(), "unchanged");
        QVERIFY(!archive.isEmpty());
        const auto before = SourceExtractFixture::readFile(archive);
        QtOperationJob job;
        job.Paths = {archive};
        for (bool terminate : {true, false}) {
            QtOperationLauncher launcher;
            QSignalSpy accepted(&launcher, &QtOperationLauncher::accepted);
            QSignalSpy finished(&launcher, &QtOperationLauncher::finished);
            // Without -y the real child retains its source result dialog.
            job.YesToAll = !terminate;
            QString error;
            QVERIFY2(launcher.start(guiProgram(), job, error), qPrintable(error));
            QTRY_COMPARE_WITH_TIMEOUT(accepted.size(), 1, 10000);
            if (terminate) {
                const auto pid = launcher.processId();
                QVERIFY(pid > 0);
                QVERIFY(KillOperationProbe(pid));
            }
            QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 15000);
            const auto result = qvariant_cast<QtOperationResult>(finished[0][0]);
            QCOMPARE(result.Status, terminate ? QtOperationResult::Outcome::Crashed
                                              : QtOperationResult::Outcome::Success);
            QCOMPARE(SourceExtractFixture::readFile(archive), before);
        }
    }
    void passwordCancellation() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(SourceExtractFixture::writeFile(directory.filePath("secret.txt"), "private bytes"));
        const auto archive = directory.filePath("protected.7z");
        QProcess create;
        create.setWorkingDirectory(directory.path());
        create.start(QStandardPaths::findExecutable("7z"),
                     {"a", "-t7z", "-pfixture-password", "-mhe=on", archive, "secret.txt"});
        QVERIFY(create.waitForFinished(10000));
        QCOMPARE(create.exitCode(), 0);
        const auto before = SourceExtractFixture::readFile(archive);
        qputenv("SEVENZIP_OPERATION_CANCEL_PASSWORD", "1");
        const auto restore = qScopeGuard([] { qunsetenv("SEVENZIP_OPERATION_CANCEL_PASSWORD"); });
        QtOperationJob job;
        job.Paths = {archive};
        job.YesToAll = true;
        QtOperationLauncher launcher;
        QSignalSpy finished(&launcher, &QtOperationLauncher::finished);
        QString error;
        QVERIFY2(launcher.start(QStringLiteral(SEVENZIP_OPERATION_HOST), job, error),
                 qPrintable(error));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 15000);
        const auto result = qvariant_cast<QtOperationResult>(finished[0][0]);
        QCOMPARE(result.Status, QtOperationResult::Outcome::Cancelled);
        QCOMPARE(result.ExitCode, 255);
        QCOMPARE(SourceExtractFixture::readFile(archive), before);
    }
};

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    if (application.arguments().contains("--probe-parent"))
        return RunOperationParent(application, QStringLiteral(SEVENZIP_OPERATION_HOST));
    OperationLauncherTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "operation_launcher_tests.moc"
