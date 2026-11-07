#include "archive/codecs/archivecodelocation.h"
#include "archive/support/source_extract_fixture.h"
#include "operations/qtoperationlauncher.h"
#include "support/operation_kill.h"
#include "support/operation_parent.h"
#include "support/operation_temp_cleanup.h"
#include "support/scoped_environment.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

namespace {
using namespace SourceExtractFixture;
using Outcome = QtOperationResult::Outcome;
QtOperationResult Run(const QtOperationJob &job) {
    QtOperationLauncher launcher;
    QEventLoop wait;
    QtOperationResult result{Outcome::ProtocolFailed, 2, "Hash operation did not finish."};
    QObject::connect(&launcher, &QtOperationLauncher::finished, &wait,
                     [&](const QtOperationResult &value) {
                         result = value;
                         wait.quit();
                     });
    QString error;
    if (!launcher.start(QStringLiteral(SEVENZIP_OPERATION_HOST), job, error))
        return {Outcome::LaunchFailed, 2, error};
    QTimer::singleShot(20000, &wait, [&] {
        KillOperationProbe(launcher.processId());
        QTimer::singleShot(2000, &wait, &QEventLoop::quit);
    });
    wait.exec();
    return result;
}
QtOperationJob Hash(const QString &root, const QStringList &paths, bool panel) {
    QtOperationJob job;
    job.Operation = QtOperationJob::Command::Hash;
    job.HashMethod = "SHA256";
    if (panel) {
        job.HashBasePrefix = QDir::toNativeSeparators(root + '/');
        for (const auto &path : paths)
            job.Paths.append(QDir::toNativeSeparators(path));
    } else
        for (const auto &path : paths)
            job.Paths.append(QDir(root).filePath(path));
    return job;
}
} // namespace

class OperationHashTests final : public QObject {
    Q_OBJECT
  private slots:
    void cleanup() { QVERIFY(CleanupOperationTestTemps()); }
    void nativeHashKeepsExactSelectionAndDirectoryMode_data() {
        QTest::addColumn<bool>("panel");
        QTest::addColumn<bool>("flat");
        QTest::addColumn<bool>("builtin");
        QTest::addColumn<bool>("single");
        QTest::newRow("shell recursive") << false << false << false << false;
        QTest::newRow("panel recursive") << true << false << false << false;
        QTest::newRow("panel flat selection") << true << true << false << false;
        QTest::newRow("shell registered hashers") << false << false << true << false;
        QTest::newRow("panel registered hashers") << true << true << true << false;
        QTest::newRow("shell literal file") << false << false << false << true;
    }
    void nativeHashKeepsExactSelectionAndDirectoryMode() {
        QFETCH(bool, panel);
        QFETCH(bool, flat);
        QFETCH(bool, builtin);
        QFETCH(bool, single);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
#ifdef Q_OS_WIN
        const QString name = QStringLiteral("tree/sub/leaf [literal] 空.txt");
#else
        const QString name = QStringLiteral("tree/sub/leaf * [literal] 空 \"q\".txt");
#endif
        const QByteArray bytes = "native checksum payload";
        QVERIFY(writeFile(directory.filePath(name), bytes));
        const QString secondName = "tree/sub/second.txt";
        const QByteArray secondBytes = "second checksum payload";
        if (!single)
            QVERIFY(writeFile(directory.filePath(secondName), secondBytes));
        if (single) {
#ifdef Q_OS_WIN
            QVERIFY(
                writeFile(directory.filePath("tree/sub/unselected.txt"), "unselected neighbour"));
#else
            QVERIFY(writeFile(directory.filePath("tree/sub/leaf X [literal] 空 \"q\".txt"),
                              "unselected wildcard neighbour"));
#endif
        }
        QVERIFY(writeFile(directory.filePath("unselected.txt"), "excluded selection"));
        const QString report = directory.filePath("hash-results.json");
        ScopedTestEnvironment output("SEVENZIP_OPERATION_HASH_REPORT", report);
        const QString deployment = directory.filePath("native runtime");
        QVERIFY(QDir().mkpath(deployment));
#ifdef SEVENZIP_CODEC_MODULE
        const QString module = QStringLiteral(SEVENZIP_CODEC_MODULE);
        if (!builtin)
            QVERIFY(QFile::copy(module, QDir(deployment).filePath(QFileInfo(module).fileName())));
#else
        Q_UNUSED(builtin);
#endif
        ScopedTestEnvironment console("SEVENZIP_CODEC_MODULE_PATH", FindArchiveModule(deployment));
        auto job = Hash(directory.path(),
                        single ? QStringList{name}
                        : flat ? QStringList{"tree", "tree/sub", name, secondName}
                               : QStringList{"tree"},
                        panel);
        job.HashEnterToDirs = !flat;
        const auto result = Run(job);
        QCOMPARE(result.Status, Outcome::Success);
        QVERIFY(result.ChangedDirectories.isEmpty());
        QVERIFY(result.ChangedArchives.isEmpty());
        const auto digest = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
        const auto results = readFile(report);
        QVERIFY2(results.contains(digest), results.constData());
        if (!single) {
            QVERIFY(results.contains("second.txt"));
            QVERIFY(results.contains(
                QCryptographicHash::hash(secondBytes, QCryptographicHash::Sha256).toHex()));
        }
        QCOMPARE(readFile(directory.filePath(name)), bytes);
    }
    void partialInputIsWarningAndInvalidMethodIsFailure_data() {
        QTest::addColumn<bool>("panel");
        QTest::addColumn<bool>("invalid");
        for (bool panel : {false, true})
            for (bool invalid : {false, true})
                QTest::newRow(qPrintable(QString("%1 %2").arg(panel).arg(invalid)))
                    << panel << invalid;
    }
    void partialInputIsWarningAndInvalidMethodIsFailure() {
        QFETCH(bool, panel);
        QFETCH(bool, invalid);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(writeFile(directory.filePath("input.txt"), "retained bytes"));
        const QString report = directory.filePath("hash-results.json");
        ScopedTestEnvironment output("SEVENZIP_OPERATION_HASH_REPORT", report);
        auto job = Hash(directory.path(), {"input.txt", "missing.txt"}, panel);
        if (invalid)
            job.HashMethod = "unavailable-hash-method";
        const auto result = Run(job);
        QCOMPARE(result.Status, invalid ? Outcome::Failure : Outcome::Warning);
        QCOMPARE(readFile(directory.filePath("input.txt")), QByteArray("retained bytes"));
        if (!invalid)
            QVERIFY(readFile(report).contains(
                QCryptographicHash::hash("retained bytes", QCryptographicHash::Sha256).toHex()));
    }
    void cancellationIsDistinctFromSuccess_data() {
        QTest::addColumn<bool>("panel");
        QTest::newRow("shell") << false;
        QTest::newRow("panel") << true;
    }
    void guiTestRejectsChecksumManifest() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString name = QStringLiteral("checked 空.txt");
        QVERIFY(writeFile(directory.filePath(name), "abc"));
        const QString manifest = directory.filePath("checksums.sha256");
        const QByteArray contents =
            QCryptographicHash::hash("abc", QCryptographicHash::Sha256).toHex() + "  " +
            name.toUtf8() + '\n';
        QVERIFY(writeFile(manifest, contents));
        QtOperationJob job;
        job.Paths = {manifest};
        job.HashMode = true;
        job.YesToAll = true;
        QCOMPARE(Run(job).Status, Outcome::Failure);
        QCOMPARE(readFile(manifest), contents);
        QCOMPARE(readFile(directory.filePath(name)), QByteArray("abc"));
    }
    void cancellationIsDistinctFromSuccess() {
        QFETCH(bool, panel);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile input(directory.filePath("large.bin"));
        QVERIFY(input.open(QIODevice::WriteOnly));
        QVERIFY(input.resize(256LL * 1024 * 1024));
        input.close();
        ScopedTestEnvironment cancel("SEVENZIP_OPERATION_CANCEL_HASH", "1");
        auto job = Hash(directory.path(), {"large.bin"}, panel);
        job.HashMethod = "*";
        QCOMPARE(Run(job).Status, Outcome::Cancelled);
        QCOMPARE(QFileInfo(input).size(), 256LL * 1024 * 1024);
    }
    void acceptedHashContinuesAfterParentExit() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString source = directory.filePath("source 空.txt");
        QVERIFY(writeFile(source, "independent checksum"));
        const QString barrier = directory.filePath("continue");
        const QString report = directory.filePath("outcome");
        const QString hashes = directory.filePath("hashes.json");
        QProcess parent;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("SEVENZIP_OPERATION_PROBE_HASH", "1");
        environment.insert("SEVENZIP_OPERATION_ARCHIVE", source);
        environment.insert("SEVENZIP_OPERATION_BARRIER", barrier);
        environment.insert("SEVENZIP_OPERATION_REPORT", report);
        environment.insert("SEVENZIP_OPERATION_HASH_REPORT", hashes);
        parent.setProcessEnvironment(environment);
        parent.start(QCoreApplication::applicationFilePath(), {"--probe-parent"});
        QVERIFY(parent.waitForFinished(15000));
        QCOMPARE(parent.exitCode(), 0);
        QVERIFY(!QFileInfo::exists(report));
        QVERIFY(writeFile(barrier, "continue"));
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(report), 15000);
        QCOMPARE(readFile(report), QByteArray("0"));
        QVERIFY(readFile(hashes).contains(
            QCryptographicHash::hash("independent checksum", QCryptographicHash::Sha256).toHex()));
    }
};
int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    if (application.arguments().contains("--probe-parent"))
        return RunOperationParent(application, QStringLiteral(SEVENZIP_OPERATION_HOST));
    OperationHashTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "operation_hash_tests.moc"
