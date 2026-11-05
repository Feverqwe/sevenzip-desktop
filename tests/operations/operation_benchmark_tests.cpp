#include "archive/codecs/archivecodelocation.h"
#include "archive/support/source_extract_fixture.h"
#include "operations/qtoperationlauncher.h"
#include "support/operation_kill.h"
#include "support/operation_parent.h"
#include "support/operation_temp_cleanup.h"
#include "support/scoped_environment.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

using Outcome = QtOperationResult::Outcome;
using namespace SourceExtractFixture;

class OperationBenchmarkTests final : public QObject {
    Q_OBJECT
  private slots:
    void cleanup() { QVERIFY(CleanupOperationTestTemps()); }
    void nativeResultAndCancellation_data() {
        QTest::addColumn<QString>("profile");
        QTest::addColumn<bool>("total");
        QTest::addColumn<Outcome>("expected");
        QTest::newRow("native LZMA") << QString("bounded") << false << Outcome::Success;
        QTest::newRow("native error") << QString("failure") << false << Outcome::Failure;
        QTest::newRow("normal cancel") << QString("normal-cancel") << false << Outcome::Cancelled;
        QTest::newRow("total cancel") << QString("cancel") << true << Outcome::Cancelled;
        QTest::newRow("stop then review") << QString("normal-stop") << false << Outcome::Cancelled;
        QTest::newRow("total native hash report") << QString("total") << true << Outcome::Success;
    }
    void nativeResultAndCancellation() {
        QFETCH(QString, profile);
        QFETCH(bool, total);
        QFETCH(Outcome, expected);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
#ifdef SEVENZIP_CODEC_MODULE
        const QString module = QStringLiteral(SEVENZIP_CODEC_MODULE);
        QVERIFY(QFile::copy(module, directory.filePath(QFileInfo(module).fileName())));
#endif
        ScopedTestEnvironment console("SEVENZIP_CODEC_MODULE_PATH",
                                      FindArchiveModule(directory.path()));
        ScopedTestEnvironment bounded("SEVENZIP_OPERATION_BENCHMARK_PROFILE", profile);
        const auto report = directory.filePath("benchmark.json");
        ScopedTestEnvironment output("SEVENZIP_OPERATION_BENCHMARK_REPORT", report);
        QtOperationJob job;
        job.Operation = QtOperationJob::Command::Benchmark;
        job.TotalMode = total;
        QtOperationLauncher launcher;
        QEventLoop wait;
        QtOperationResult result{Outcome::ProtocolFailed, 2, "Benchmark did not finish"};
        connect(&launcher, &QtOperationLauncher::finished, &wait,
                [&](const QtOperationResult &value) {
                    result = value;
                    wait.quit();
                });
        QString error;
        QVERIFY2(launcher.start(QStringLiteral(SEVENZIP_OPERATION_HOST), job, error),
                 qPrintable(error));
        QTimer::singleShot(20000, &wait, [&] {
            KillOperationProbe(launcher.processId());
            QTimer::singleShot(2000, &wait, &QEventLoop::quit);
        });
        wait.exec();
        QCOMPARE(result.Status, expected);
        QCOMPARE(result.CancellationNeedsReview,
                 expected == Outcome::Cancelled && profile != "normal-stop");
        QVERIFY(result.ChangedArchives.isEmpty() && result.ChangedDirectories.isEmpty());
        QVERIFY(readFile(report).contains(total ? "IDD_BENCH_TOTAL" : "IDD_BENCH"));
        if (profile == "total")
            QVERIFY(readFile(report).contains("CRC32"));
    }
    void acceptedBenchmarkOutlivesParent() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto barrier = directory.filePath("continue");
        const auto report = directory.filePath("result");
        QProcess parent;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("SEVENZIP_OPERATION_PROBE_BENCHMARK", "1");
        environment.insert("SEVENZIP_OPERATION_BENCHMARK_PROFILE", "bounded");
        environment.insert("SEVENZIP_OPERATION_BARRIER", barrier);
        environment.insert("SEVENZIP_OPERATION_REPORT", report);
        parent.setProcessEnvironment(environment);
        parent.start(QCoreApplication::applicationFilePath(), {"--probe-parent"});
        QVERIFY(parent.waitForFinished(15000));
        QCOMPARE(parent.exitCode(), 0);
        QVERIFY(!QFileInfo::exists(report));
        QVERIFY(writeFile(barrier, "continue"));
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(report), 15000);
        QCOMPARE(readFile(report), QByteArray("0"));
    }
};
int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    if (application.arguments().contains("--probe-parent"))
        return RunOperationParent(application, QStringLiteral(SEVENZIP_OPERATION_HOST));
    OperationBenchmarkTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "operation_benchmark_tests.moc"
