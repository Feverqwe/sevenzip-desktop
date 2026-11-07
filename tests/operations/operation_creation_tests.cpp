#include "Common/StringConvert.h"
#include "archive/codecs/archivecodelocation.h"
#include "archive/operations/ZipRegistry.h"
#include "archive/resources/qtarchivewritelease.h"
#include "archive/support/source_extract_fixture.h"
#include "operations/qtoperationlauncher.h"
#include "support/operation_kill.h"
#include "support/operation_parent.h"
#include "support/operation_program.h"
#include "support/operation_temp_cleanup.h"
#include "support/scoped_environment.h"
#include <QEventLoop>
#include <QRandomGenerator>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

namespace {
using namespace SourceExtractFixture;
using Outcome = QtOperationResult::Outcome;
using Environment = ScopedTestEnvironment;
QtOperationJob Compress(const QString &archive, const QStringList &paths,
                        const QString &format = "zip") {
    QtOperationJob job;
    job.Operation = QtOperationJob::Command::Compress;
    job.Paths = paths;
    job.ArchivePath = archive;
    job.ArchiveType = format;
    job.AddExtension = false;
    job.ShowDialog = false;
    return job;
}
QtOperationResult Run(const QString &program, const QtOperationJob &job) {
    QtOperationLauncher launcher;
    QEventLoop wait;
    QtOperationResult result{Outcome::ProtocolFailed, 2, "Fixture operation did not finish."};
    QObject::connect(&launcher, &QtOperationLauncher::finished, &wait,
                     [&](const QtOperationResult &value) {
                         result = value;
                         wait.quit();
                     });
    QString error;
    if (!launcher.start(program, job, error))
        return {Outcome::LaunchFailed, 2, error};
    QTimer::singleShot(20000, &wait, [&] {
        // A failed fixture must finish its owned child before QTemporaryDir
        // releases inputs. Production observers deliberately never kill work.
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
} // namespace

class OperationCreationTests final : public QObject {
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
    void nativeEntryCreatesAndUpdatesWithoutConsole_data() {
        QTest::addColumn<QString>("format");
        QTest::newRow("zip") << QString("zip");
        QTest::newRow("7z") << QString("7z");
    }
    void nativeEntryCreatesAndUpdatesWithoutConsole() {
        QFETCH(QString, format);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString name = QStringLiteral("исходник 空 with spaces.txt");
        const QString source = directory.filePath(name);
        const QString keep = directory.filePath("keep.bin");
        const QString archive = directory.filePath("archive 空.unknown");
        QVERIFY(writeFile(source, "original"));
        QVERIFY(writeFile(keep, "keep original bytes"));
#ifdef SEVENZIP_CODEC_MODULE
        const QString deployment = directory.filePath("native runtime");
        QVERIFY(QDir().mkpath(deployment));
        const QString module = QStringLiteral(SEVENZIP_CODEC_MODULE);
        QVERIFY(QFile::copy(module, QDir(deployment).filePath(QFileInfo(module).fileName())));
        Environment engine("SEVENZIP_CODEC_MODULE_PATH", FindArchiveModule(deployment));
#endif
        const auto created = Run(NativeOperationProgram(SEVENZIP_NATIVE_GUI),
                                 Compress(archive, {source, keep}, format));
        QVERIFY2(created.Status == Outcome::Success, qPrintable(created.Error));
        QVERIFY(QFileInfo::exists(archive));
        QVERIFY(created.ChangedDirectories.contains(QFileInfo(archive).absolutePath()));
        QVERIFY(writeFile(source, "updated native bytes"));
        const auto updated =
            Run(NativeOperationProgram(SEVENZIP_NATIVE_GUI), Compress(archive, {source}, format));
        QVERIFY2(updated.Status == Outcome::Success, qPrintable(updated.Error));
        const QString output = directory.filePath("verified output");
        QCOMPARE(Run(NativeOperationProgram(SEVENZIP_NATIVE_GUI), Extract(archive, output)).Status,
                 Outcome::Success);
        QCOMPARE(readFile(QDir(output).filePath(name)), QByteArray("updated native bytes"));
        QCOMPARE(readFile(QDir(output).filePath("keep.bin")), QByteArray("keep original bytes"));
        QCOMPARE(readFile(source), QByteArray("updated native bytes"));
    }
    void dialogOwnsTargetPasswordAndInputDeletion_data() {
        QTest::addColumn<bool>("cancel");
        QTest::newRow("accept edits") << false;
        QTest::newRow("cancel before writing") << true;
    }
    void dialogOwnsTargetPasswordAndInputDeletion() {
        QFETCH(bool, cancel);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString source = directory.filePath("secret 空.txt");
        QVERIFY(writeFile(source, "protected native payload"));
        const QString initial = directory.filePath("initial.7z");
        const QString actual = directory.filePath("changed target 空.7z");
        Environment decision("SEVENZIP_OPERATION_CREATE_DECISION", cancel ? "cancel" : "accept");
        Environment target("SEVENZIP_OPERATION_CREATE_TARGET", actual);
        Environment password("SEVENZIP_OPERATION_CREATE_PASSWORD", "fixture password");
        Environment deletion("SEVENZIP_OPERATION_DELETE_INPUTS", "1");
        auto job = Compress(initial, {source}, "7z");
        job.ShowDialog = true;
        const auto result = Run(QStringLiteral(SEVENZIP_OPERATION_HOST), job);
        QCOMPARE(result.Status, cancel ? Outcome::Cancelled : Outcome::Success);
        QVERIFY(!QFileInfo::exists(initial));
        if (cancel) {
            QVERIFY(!QFileInfo::exists(actual));
            QCOMPARE(readFile(source), QByteArray("protected native payload"));
            QVERIFY(result.ChangedDirectories.isEmpty());
        } else {
            QVERIFY(!QFileInfo::exists(source));
            Environment suppliedPassword("SEVENZIP_OPERATION_PASSWORD", "fixture password");
            const QString output = directory.filePath("decrypted");
            auto extract = Extract(actual, output);
            extract.YesToAll = false;
            const auto extracted = Run(QStringLiteral(SEVENZIP_OPERATION_HOST), extract);
            QVERIFY2(extracted.Status == Outcome::Success, qPrintable(extracted.Error));
            QCOMPARE(readFile(QDir(output).filePath("secret 空.txt")),
                     QByteArray("protected native payload"));
        }
    }
    void conflictingWriterPreservesArchiveAndCanRetry() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString source = directory.filePath("source.txt");
        const QString archive = directory.filePath("target.zip");
        QVERIFY(writeFile(source, "original"));
        const auto job = Compress(archive, {source});
        QCOMPARE(Run(QStringLiteral(SEVENZIP_OPERATION_HOST), job).Status, Outcome::Success);
        const QByteArray original = readFile(archive);
        QVERIFY(writeFile(source, "replacement"));
        {
            const auto owner = QtArchiveWriteLease::Acquire({archive});
            QVERIFY2(owner.has_value(), owner ? "" : qPrintable(owner.error()));
            Environment report("SEVENZIP_OPERATION_UI_REPORT", directory.filePath("widgets.json"));
            const auto rejected = Run(QStringLiteral(SEVENZIP_OPERATION_HOST), job);
            QVERIFY2(rejected.Status == Outcome::Failure,
                     (rejected.Error.toUtf8() + '\n' + readFile(directory.filePath("widgets.json")))
                         .constData());
            QCOMPARE(readFile(archive), original);
            QCOMPARE(readFile(source), QByteArray("replacement"));
        }
        QCOMPARE(Run(QStringLiteral(SEVENZIP_OPERATION_HOST), job).Status, Outcome::Success);
        const QString output = directory.filePath("verified retry");
        QCOMPARE(Run(QStringLiteral(SEVENZIP_OPERATION_HOST), Extract(archive, output)).Status,
                 Outcome::Success);
        QCOMPARE(readFile(QDir(output).filePath("source.txt")), QByteArray("replacement"));
    }
    void creationContinuesAfterParentExit() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString source = directory.filePath("source 空.txt");
        const QString archive = directory.filePath("independent.zip");
        QVERIFY(writeFile(source, "created after parent exit"));
        const auto barrier = directory.filePath("continue");
        const auto report = directory.filePath("result");
        QProcess parent;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("SEVENZIP_OPERATION_PROBE_COMPRESS", "1");
        environment.insert("SEVENZIP_OPERATION_ARCHIVE", source);
        environment.insert("SEVENZIP_OPERATION_CREATE_TARGET", archive);
        environment.insert("SEVENZIP_OPERATION_BARRIER", barrier);
        environment.insert("SEVENZIP_OPERATION_REPORT", report);
        parent.setProcessEnvironment(environment);
        parent.start(QCoreApplication::applicationFilePath(), {"--probe-parent"});
        QVERIFY(parent.waitForStarted(5000));
        QVERIFY(parent.waitForFinished(15000));
        QCOMPARE(parent.exitStatus(), QProcess::NormalExit);
        QCOMPARE(parent.exitCode(), 0);
        QVERIFY(!QFileInfo::exists(report));
        QVERIFY(!QFileInfo::exists(archive));
        QVERIFY(writeFile(barrier, "continue"));
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(report), 15000);
        QVERIFY2(readFile(report) == "0", readFile(report + ".diagnostic").constData());
        const QString output = directory.filePath("verified independent");
        QCOMPARE(Run(QStringLiteral(SEVENZIP_OPERATION_HOST), Extract(archive, output)).Status,
                 Outcome::Success);
        QCOMPARE(readFile(QDir(output).filePath("source 空.txt")),
                 QByteArray("created after parent exit"));
    }
    void killedWriterKeepsOriginalAndReleasesProcessOwnership() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString source = directory.filePath("source.bin");
        const QString archive = directory.filePath("existing.7z");
        QVERIFY(writeFile(source, "original payload"));
        auto job = Compress(archive, {source}, "7z");
        job.ShowDialog = true;
        Environment decision("SEVENZIP_OPERATION_CREATE_DECISION", "accept");
        const QString work = directory.filePath("requested working directory");
        QVERIFY(QDir().mkpath(work));
        NWorkDir::CInfo working;
        working.SetDefault();
        working.Mode = NWorkDir::NMode::kSpecified;
        working.ForRemovableOnly = false;
        working.Path = us2fs(work.toStdWString().c_str());
        working.Save(settings_.filePath("settings.ini"));
        QCOMPARE(Run(QStringLiteral(SEVENZIP_OPERATION_HOST), job).Status, Outcome::Success);
        const QByteArray original = readFile(archive);
        QFile large(source);
        QVERIFY(large.open(QIODevice::WriteOnly));
        // Sparse logical input keeps the real native writer active without
        // allocating a large fixture or waiting for a completed compression.
        QVERIFY(large.resize(1024LL * 1024 * 1024));
        large.close();
        const QStringList before = QDir(work).entryList(QDir::Files | QDir::Hidden);
        const QString reportPath = directory.filePath("writer-result");
        Environment report("SEVENZIP_OPERATION_REPORT", reportPath);
        QtOperationLauncher writer;
        QSignalSpy finished(&writer, &QtOperationLauncher::finished);
        QString error;
        QVERIFY2(writer.start(QStringLiteral(SEVENZIP_OPERATION_HOST), job, error),
                 qPrintable(error));
        const auto terminate = qScopeGuard([&] {
            if (finished.isEmpty())
                KillOperationProbe(writer.processId());
        });
        const auto hasNewOutput = [&] {
            for (const QString &file : QDir(work).entryList(QDir::Files | QDir::Hidden))
                if (!before.contains(file))
                    return true;
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(hasNewOutput() || !finished.isEmpty(), 15000);
        QVERIFY2(finished.isEmpty(),
                 (readFile(reportPath) + '\n' + readFile(reportPath + ".diagnostic")).constData());
        QVERIFY(KillOperationProbe(writer.processId()));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        const auto result = qvariant_cast<QtOperationResult>(finished.first().first());
        QCOMPARE(result.Status, Outcome::Crashed);
        QVERIFY(result.ChangedArchives.contains(archive));
        QCOMPARE(readFile(archive), original);
        QVERIFY(writeFile(source, "retry after native writer crash"));
        QCOMPARE(Run(QStringLiteral(SEVENZIP_OPERATION_HOST), job).Status, Outcome::Success);
        const QString output = directory.filePath("verified retry");
        QCOMPARE(Run(QStringLiteral(SEVENZIP_OPERATION_HOST), Extract(archive, output)).Status,
                 Outcome::Success);
        QCOMPARE(readFile(QDir(output).filePath("source.bin")),
                 QByteArray("retry after native writer crash"));
    }
    void volumesAndSelfExtractorsOwnCompletedOutputs_data() {
        QTest::addColumn<bool>("volumes");
        QTest::addColumn<bool>("sfx");
        QTest::newRow("volumes") << true << false;
        QTest::newRow("self extractor") << false << true;
        QTest::newRow("self extractor with volumes") << true << true;
    }
    void volumesAndSelfExtractorsOwnCompletedOutputs() {
        QFETCH(bool, volumes);
        QFETCH(bool, sfx);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QByteArray bytes(512 * 1024, Qt::Uninitialized);
        QRandomGenerator random(42);
        for (auto &byte : bytes)
            byte = char(random.generate());
        const QString source = directory.filePath("payload 空.bin");
        QVERIFY(writeFile(source, bytes));
        const QString target = directory.filePath(sfx ? "package" : "package.7z");
        Environment decision("SEVENZIP_OPERATION_CREATE_DECISION", "accept");
        Environment actual("SEVENZIP_OPERATION_CREATE_TARGET", target);
        Environment deletion("SEVENZIP_OPERATION_DELETE_INPUTS", "1");
        std::unique_ptr<Environment> volumeOption, sfxOption;
        if (volumes)
            volumeOption = std::make_unique<Environment>("SEVENZIP_OPERATION_VOLUME", "128K");
        if (sfx)
            sfxOption = std::make_unique<Environment>("SEVENZIP_OPERATION_SFX", "1");
        auto job = Compress(target, {source}, "7z");
        job.ShowDialog = true;
        const auto result = Run(QStringLiteral(SEVENZIP_OPERATION_HOST), job);
        QVERIFY2(result.Status == Outcome::Success, qPrintable(result.Error));
        QVERIFY(!QFileInfo::exists(source));
        if (volumes)
            QVERIFY(QFileInfo::exists(directory.filePath("package.7z.002")));
        const QString output = directory.filePath("verified output");
        if (sfx) {
#ifdef Q_OS_WIN
            const QString executable = directory.filePath("package.exe");
            const QStringList arguments{"-y", "-o" + output};
#else
            const QString executable = directory.filePath("package");
            const QStringList arguments{"x", "-y", "-o" + output};
#endif
            QVERIFY(QFileInfo(executable).isExecutable());
            QProcess extractor;
            extractor.start(executable, arguments);
            QVERIFY(extractor.waitForFinished(10000));
            QCOMPARE(extractor.exitStatus(), QProcess::NormalExit);
            QCOMPARE(extractor.exitCode(), 0);
        } else
            QCOMPARE(Run(QStringLiteral(SEVENZIP_OPERATION_HOST),
                         Extract(directory.filePath("package.7z.001"), output))
                         .Status,
                     Outcome::Success);
        QCOMPARE(readFile(QDir(output).filePath("payload 空.bin")), bytes);
    }
};
int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    if (application.arguments().contains("--probe-parent"))
        return RunOperationParent(application, QStringLiteral(SEVENZIP_OPERATION_HOST));
    OperationCreationTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "operation_creation_tests.moc"
