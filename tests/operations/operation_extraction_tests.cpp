#include "archive/codecs/archivecodelocation.h"
#include "archive/support/source_extract_fixture.h"
#include "operations/qtoperationlauncher.h"
#include "support/operation_kill.h"
#include "support/operation_parent.h"
#include "support/operation_program.h"
#include "support/operation_temp_cleanup.h"
#include "support/scoped_environment.h"
#include <QElapsedTimer>
#include <QEventLoop>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QtTest>

namespace {
using namespace SourceExtractFixture;
using Outcome = QtOperationResult::Outcome;
using Environment = ScopedTestEnvironment;
QtOperationResult Run(const QString &program, const QtOperationJob &job) {
    QtOperationLauncher launcher;
    QEventLoop wait;
    QtOperationResult result{Outcome::ProtocolFailed, 2, "Fixture operation did not finish."};
    bool timedOut = false;
    QObject::connect(&launcher, &QtOperationLauncher::finished, &wait,
                     [&](const QtOperationResult &value) {
                         if (!timedOut)
                             result = value;
                         wait.quit();
                     });
    QString error;
    if (!launcher.start(program, job, error))
        return {Outcome::LaunchFailed, 2, error};
    QTimer::singleShot(20000, &wait, [&] {
        timedOut = true;
        KillOperationProbe(launcher.processId());
        QTimer::singleShot(2000, &wait, &QEventLoop::quit);
    });
    wait.exec();
    return result;
}
QtOperationJob Extract(const QString &archive, const QString &output) {
    QtOperationJob job;
    job.Operation = QtOperationJob::Command::Extract;
    job.Paths = {archive};
    job.Destination = output;
    job.ShowDialog = false;
    job.YesToAll = true;
    return job;
}
QStringList Roots(const QtOperationResult &result) {
    QStringList paths;
    for (const auto &directory : result.ChangedDirectories)
        paths.append(QDir::cleanPath(QFileInfo(directory).canonicalFilePath()));
    return paths;
}
} // namespace

class OperationExtractionTests final : public QObject {
    Q_OBJECT
    QTemporaryDir settings_;
    std::unique_ptr<Environment> settingsPath_;
  private slots:
    void initTestCase() {
        ConfigureInstalledOperationPlatform();
        QVERIFY(settings_.isValid());
        settingsPath_ = std::make_unique<Environment>("SEVENZIP_SETTINGS_PATH",
                                                      settings_.filePath("settings.ini"));
    }
    void init() { QFile::remove(settings_.filePath("settings.ini")); }
    void cleanup() { QVERIFY(CleanupOperationTestTemps()); }
    void nativeEntryExtractsLiteralUnicode() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QByteArray bytes("native standalone extraction");
        const auto archive = makeArchive(directory.path(), bytes);
        QVERIFY(!archive.isEmpty());
        const auto before = readFile(archive);
        const auto output = directory.filePath(QStringLiteral("выход 空 with spaces"));
#ifdef SEVENZIP_CODEC_MODULE
        const auto deployment = directory.filePath("native runtime");
        QVERIFY(QDir().mkpath(deployment));
        const auto module = QStringLiteral(SEVENZIP_CODEC_MODULE);
        QVERIFY(QFile::copy(module, QDir(deployment).filePath(QFileInfo(module).fileName())));
        Environment engine("SEVENZIP_CODEC_MODULE_PATH", FindArchiveModule(deployment));
#endif
        const auto result =
            Run(NativeOperationProgram(SEVENZIP_NATIVE_GUI), Extract(archive, output));
        QVERIFY2(result.Status == Outcome::Success, qPrintable(result.Error));
        QCOMPARE(readFile(QDir(output).filePath(kName)), bytes);
        QCOMPARE(Roots(result), QStringList{QFileInfo(output).canonicalFilePath()});
        QCOMPARE(readFile(archive), before);
    }
    void parameterDecision_data() {
        QTest::addColumn<bool>("cancel");
        QTest::newRow("edited destination") << false;
        QTest::newRow("cancel before output") << true;
    }
    void parameterDecision() {
        QFETCH(bool, cancel);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto archive = makeArchive(directory.path(), "dialog bytes");
        QVERIFY(!archive.isEmpty());
        const auto initial = directory.filePath("initial output");
        const auto actual = directory.filePath(QStringLiteral("другая папка 空"));
        Environment decision("SEVENZIP_OPERATION_EXTRACT_DECISION", cancel ? "cancel" : "accept");
        Environment destination("SEVENZIP_OPERATION_DESTINATION", actual);
        auto job = Extract(archive, initial);
        job.ShowDialog = true;
        const auto result = Run(QStringLiteral(SEVENZIP_OPERATION_HOST), job);
        QVERIFY2(
            result.Status == (cancel ? Outcome::Cancelled : Outcome::Success),
            qPrintable(
                QString("Operation outcome %1: %2").arg(int(result.Status)).arg(result.Error)));
        QVERIFY(!QFileInfo::exists(initial));
        if (cancel) {
            QVERIFY(!QFileInfo::exists(actual));
            QVERIFY(result.ChangedDirectories.isEmpty());
        } else {
            QCOMPARE(Roots(result), QStringList{QFileInfo(actual).canonicalFilePath()});
            QCOMPARE(readFile(QDir(actual).filePath(kName)), QByteArray("dialog bytes"));
        }
    }
    void overwriteDecision_data() {
        QTest::addColumn<QString>("button");
        QTest::addColumn<bool>("replace");
        QTest::addColumn<bool>("cancel");
        QTest::newRow("keep existing") << "overwriteNo" << false << false;
        QTest::newRow("replace existing") << "overwriteYes" << true << false;
        QTest::newRow("cancel overwrite") << "overwriteCancel" << false << true;
    }
    void overwriteDecision() {
        QFETCH(QString, button);
        QFETCH(bool, replace);
        QFETCH(bool, cancel);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto archive = makeArchive(directory.path(), "incoming bytes");
        QVERIFY(!archive.isEmpty());
        const auto before = readFile(archive);
        const auto output = directory.filePath("output");
        QVERIFY(writeFile(QDir(output).filePath(kName), "retained bytes"));
        Environment answer("SEVENZIP_OPERATION_OVERWRITE", button);
        auto job = Extract(archive, output);
        job.YesToAll = false;
        const auto result = Run(QStringLiteral(SEVENZIP_OPERATION_HOST), job);
        QVERIFY2(
            result.Status == (cancel    ? Outcome::Cancelled
                              : replace ? Outcome::Success
                                        : Outcome::Warning),
            qPrintable(
                QString("Operation outcome %1: %2").arg(int(result.Status)).arg(result.Error)));
        QCOMPARE(readFile(QDir(output).filePath(kName)),
                 replace ? QByteArray("incoming bytes") : QByteArray("retained bytes"));
        QCOMPARE(Roots(result), QStringList{QFileInfo(output).canonicalFilePath()});
        QCOMPARE(readFile(archive), before);
    }
    void cancellationKeepsCompletedEntries() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(writeFile(directory.filePath("a-new.txt"), "completed entry"));
        QVERIFY(writeFile(directory.filePath("z-existing.txt"), "incoming replacement"));
        QProcess create;
        create.setWorkingDirectory(directory.path());
        create.start(QStandardPaths::findExecutable("7z"),
                     {"a", "-tzip", "cancel.zip", "a-new.txt", "z-existing.txt"});
        QVERIFY(create.waitForFinished(10000));
        QCOMPARE(create.exitCode(), 0);
        const auto archive = directory.filePath("cancel.zip");
        const auto before = readFile(archive);
        const auto output = directory.filePath("output");
        QVERIFY(writeFile(QDir(output).filePath("z-existing.txt"), "retained existing entry"));
        auto job = Extract(archive, output);
        job.YesToAll = false;
        const auto result = [&] {
            Environment cancel("SEVENZIP_OPERATION_OVERWRITE", "overwriteCancel");
            return Run(QStringLiteral(SEVENZIP_OPERATION_HOST), job);
        }();
        QCOMPARE(result.Status, Outcome::Cancelled);
        QCOMPARE(readFile(QDir(output).filePath("a-new.txt")), QByteArray("completed entry"));
        QCOMPARE(readFile(QDir(output).filePath("z-existing.txt")),
                 QByteArray("retained existing entry"));
        QCOMPARE(Roots(result), QStringList{QFileInfo(output).canonicalFilePath()});
        QCOMPARE(readFile(archive), before);
        Environment replace("SEVENZIP_OPERATION_OVERWRITE", "overwriteYes");
        const auto retry = Run(QStringLiteral(SEVENZIP_OPERATION_HOST), job);
        QCOMPARE(retry.Status, Outcome::Success);
        QCOMPARE(readFile(QDir(output).filePath("a-new.txt")), QByteArray("completed entry"));
        QCOMPARE(readFile(QDir(output).filePath("z-existing.txt")),
                 QByteArray("incoming replacement"));
        QCOMPARE(readFile(archive), before);
    }
    void checksumFailureRetainsReadableAndPartialOutput() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QByteArray good("readable entry bytes"), bad("damaged entry unique payload bytes");
        QVERIFY(writeFile(directory.filePath("good.txt"), good));
        QVERIFY(writeFile(directory.filePath("bad.txt"), bad));
        QProcess create;
        create.setWorkingDirectory(directory.path());
        create.start(QStandardPaths::findExecutable("7z"),
                     {"a", "-tzip", "-mx=0", "partial.zip", "good.txt", "bad.txt"});
        QVERIFY(create.waitForFinished(10000));
        QCOMPARE(create.exitCode(), 0);
        const auto archive = directory.filePath("partial.zip");
        auto damaged = readFile(archive);
        const auto offset = damaged.indexOf(bad);
        QVERIFY(offset >= 0);
        damaged[offset] = char(damaged[offset] ^ 0x40);
        QVERIFY(writeFile(archive, damaged));
        const auto output = directory.filePath("output");
        const auto result =
            Run(NativeOperationProgram(SEVENZIP_NATIVE_GUI), Extract(archive, output));
        QCOMPARE(result.Status, Outcome::Failure);
        QCOMPARE(readFile(QDir(output).filePath("good.txt")), good);
        QVERIFY(QFileInfo::exists(QDir(output).filePath("bad.txt")));
        QVERIFY(readFile(QDir(output).filePath("bad.txt")) != bad);
        QCOMPARE(Roots(result), QStringList{QFileInfo(output).canonicalFilePath()});
        QCOMPARE(readFile(archive), damaged);
    }
    void passwordComesFromChildDialog_data() {
        QTest::addColumn<QString>("password");
        QTest::addColumn<bool>("cancel");
        QTest::newRow("correct password") << "fixture-password" << false;
        QTest::newRow("incorrect password") << "incorrect password" << false;
        QTest::newRow("cancel password") << "unused" << true;
    }
    void passwordComesFromChildDialog() {
        QFETCH(QString, password);
        QFETCH(bool, cancel);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(writeFile(directory.filePath("secret.txt"), "protected payload"));
        QProcess create;
        create.setWorkingDirectory(directory.path());
        create.start(QStandardPaths::findExecutable("7z"),
                     {"a", "-t7z", "-pfixture-password", "-mhe=on", "protected.7z", "secret.txt"});
        QVERIFY(create.waitForFinished(10000));
        QCOMPARE(create.exitCode(), 0);
        const auto archive = directory.filePath("protected.7z");
        const auto before = readFile(archive);
        const auto output = directory.filePath("output");
        auto job = Extract(archive, output);
        job.YesToAll = false;
        const auto result = [&] {
            Environment suppliedPassword("SEVENZIP_OPERATION_PASSWORD", password);
            std::unique_ptr<Environment> cancellation;
            if (cancel)
                cancellation =
                    std::make_unique<Environment>("SEVENZIP_OPERATION_CANCEL_PASSWORD", "1");
            return Run(QStringLiteral(SEVENZIP_OPERATION_HOST), job);
        }();
        const bool correct = !cancel && password == "fixture-password";
        QCOMPARE(result.Status, cancel    ? Outcome::Cancelled
                                : correct ? Outcome::Success
                                          : Outcome::Failure);
        if (!correct) {
            QVERIFY(!QFileInfo::exists(QDir(output).filePath("secret.txt")));
            QCOMPARE(readFile(archive), before);
            Environment retryPassword("SEVENZIP_OPERATION_PASSWORD", "fixture-password");
            const auto retry = Run(QStringLiteral(SEVENZIP_OPERATION_HOST), job);
            QCOMPARE(retry.Status, Outcome::Success);
        }
        QCOMPARE(readFile(QDir(output).filePath("secret.txt")), QByteArray("protected payload"));
        QCOMPARE(readFile(archive), before);
    }
    void extractionContinuesAfterParentExit() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QByteArray bytes("output produced after parent exit");
        const auto archive = makeArchive(directory.path(), bytes);
        QVERIFY(!archive.isEmpty());
        const auto before = readFile(archive);
        const auto output = directory.filePath("output");
        const auto barrier = directory.filePath("continue");
        const auto report = directory.filePath("result");
        QProcess parent;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("SEVENZIP_OPERATION_PROBE_EXTRACT", "1");
        environment.insert("SEVENZIP_OPERATION_ARCHIVE", archive);
        environment.insert("SEVENZIP_OPERATION_DESTINATION", output);
        environment.insert("SEVENZIP_OPERATION_BARRIER", barrier);
        environment.insert("SEVENZIP_OPERATION_REPORT", report);
        parent.setProcessEnvironment(environment);
        parent.start(QCoreApplication::applicationFilePath(), {"--probe-parent"});
        QVERIFY(parent.waitForStarted(5000));
        QVERIFY(parent.waitForFinished(15000));
        QCOMPARE(parent.exitStatus(), QProcess::NormalExit);
        QCOMPARE(parent.exitCode(), 0);
        QVERIFY(!QFileInfo::exists(report));
        QVERIFY(!QFileInfo::exists(output));
        QVERIFY(writeFile(barrier, "continue"));
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(report), 15000);
        QVERIFY2(readFile(report) == "0", readFile(report + ".diagnostic").constData());
        QCOMPARE(readFile(QDir(output).filePath(kName)), bytes);
        QCOMPARE(readFile(archive), before);
    }
    void crashRetainsAnnouncedOutputRoot() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto archive = makeArchive(directory.path(), "unchanged archive");
        QVERIFY(!archive.isEmpty());
        const auto before = readFile(archive);
        const auto output =
            QDir(QFileInfo(directory.path()).canonicalFilePath()).filePath("output");
        const auto gate = directory.filePath("output gate");
        Environment barrier("SEVENZIP_OPERATION_OUTPUT_BARRIER", gate);
        QtOperationLauncher launcher;
        QSignalSpy finished(&launcher, &QtOperationLauncher::finished);
        bool announced = false, killed = false;
        connect(&launcher, &QtOperationLauncher::accepted, &launcher, [&] {
            // Hold the observer's event delivery after ACK. The child writes
            // its metadata into the pipe and exits before queued reads run.
            QElapsedTimer wait;
            wait.start();
            while (!(announced = QFileInfo::exists(gate + ".ready")) && wait.elapsed() < 15000)
                QThread::msleep(10);
            killed = KillOperationProbe(launcher.processId());
        });
        QString error;
        QVERIFY2(launcher.start(QStringLiteral(SEVENZIP_OPERATION_HOST), Extract(archive, output),
                                error),
                 qPrintable(error));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 20000);
        QVERIFY(announced);
        QVERIFY(killed);
        const auto result = qvariant_cast<QtOperationResult>(finished[0][0]);
        QCOMPARE(result.Status, Outcome::Crashed);
        QCOMPARE(result.ChangedDirectories.size(), 1);
        QCOMPARE(QDir::cleanPath(result.ChangedDirectories.first()), QDir::cleanPath(output));
        QCOMPARE(readFile(archive), before);
    }
    void unsafeMemberCannotEscapeOutput() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto archive = makeArchive(directory.path(), "untrusted member bytes");
        QVERIFY(!archive.isEmpty());
        QProcess rename;
        rename.start(QStandardPaths::findExecutable("7z"),
                     {"rn", archive, kName, "../outside.txt"});
        QVERIFY(rename.waitForFinished(10000));
        QCOMPARE(rename.exitCode(), 0);
        const auto before = readFile(archive);
        const auto outside = directory.filePath("outside.txt");
        QVERIFY(writeFile(outside, "retained outside file"));
        const auto result = Run(NativeOperationProgram(SEVENZIP_NATIVE_GUI),
                                Extract(archive, directory.filePath("output")));
        QVERIFY(result.Status == Outcome::Success || result.Status == Outcome::Failure);
        QCOMPARE(readFile(outside), QByteArray("retained outside file"));
        QCOMPARE(readFile(archive), before);
    }
};
int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    if (application.arguments().contains("--probe-parent"))
        return RunOperationParent(application, QStringLiteral(SEVENZIP_OPERATION_HOST));
    OperationExtractionTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "operation_extraction_tests.moc"
