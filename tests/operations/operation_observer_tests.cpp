#include "operations/qtoperationlauncher.h"
#include "rep_operation_merged.h"
#include "support/operation_kill.h"
#include <QFileInfo>
#include <QRemoteObjectNode>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

namespace {
using Outcome = QtOperationResult::Outcome;

// A real independent peer controls report/exit disagreement without replacing
// the production observer, transport or native process adapter.
int RunPeer(QCoreApplication &application, const QString &channel) {
    QRemoteObjectNode node;
    std::unique_ptr<OperationReplica> replica(node.acquire<OperationReplica>());
    QObject context;
    QTimer exitAfterAcceptance;
    QObject::connect(replica.get(), &QRemoteObjectReplica::initialized, &context, [&] {
        // Leave QtRO initialization before waiting for a remote reply, as the
        // production endpoint does before running the native operation.
        QTimer::singleShot(0, &context, [&] {
            const auto job = OperationJobFromRequest(replica->request());
            const auto mode = job.HashMethod;
            replica->accept();
            const QString directory = QFileInfo(job.Paths.first()).absolutePath();
            replica->outputs({directory}, job.Paths);
            // A reply establishes delivery of all preceding messages before exit.
            QtOperationResult result{Outcome::Success, 0, {}};
            result.ChangedDirectories = {directory};
            result.ChangedArchives = job.Paths;
            if (mode == "missing") {
                // Exit without a report only after the parent has observed acceptance.
                QObject::connect(&exitAfterAcceptance, &QTimer::timeout, &context,
                                 [&, marker = job.Paths.first() + ".accepted"] {
                                     if (QFileInfo::exists(marker))
                                         application.exit(0);
                                 });
                exitAfterAcceptance.start(10);
                return;
            }
            result.ChangedDirectories.prepend(job.Destination);
            if (mode == "invalid")
                result.ExitCode = 2;
            auto reply = replica->complete(OperationReportFromResult(result));
            if (!reply.waitForFinished(3000)) {
                application.exit(100);
                return;
            }
            if (mode == "crash") {
                KillOperationProbe(QCoreApplication::applicationPid());
                return;
            }
            application.exit(mode == "mismatch" ? 2 : 0);
        });
    });
    QTimer::singleShot(10000, &context, [&] { application.exit(101); });
    if (!node.connectToNode(QUrl(QStringLiteral("local:") + channel)))
        return 102;
    return application.exec();
}
} // namespace

class OperationObserverTests final : public QObject {
    Q_OBJECT
  private slots:
    void reportAndNativeExit_data() {
        QTest::addColumn<QString>("mode");
        QTest::addColumn<Outcome>("outcome");
        QTest::addColumn<int>("code");
        QTest::newRow("complete") << "complete" << Outcome::Success << 0;
        QTest::newRow("missing report") << "missing" << Outcome::ProtocolFailed << 0;
        QTest::newRow("different native exit") << "mismatch" << Outcome::ProtocolFailed << 2;
        QTest::newRow("invalid success report") << "invalid" << Outcome::ProtocolFailed << 2;
        QTest::newRow("crash overrides success report") << "crash" << Outcome::Crashed << -1;
    }
    void reportAndNativeExit() {
        QFETCH(QString, mode);
        QFETCH(Outcome, outcome);
        QFETCH(int, code);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QtOperationJob job;
        job.Operation = QtOperationJob::Command::Hash;
        job.HashMethod = mode;
        job.Paths = {directory.filePath(QStringLiteral("выход 空 archive.zip"))};
        job.Destination = directory.filePath("report only directory");
        QtOperationLauncher launcher;
        QSignalSpy accepted(&launcher, &QtOperationLauncher::accepted);
        QSignalSpy finished(&launcher, &QtOperationLauncher::finished);
        bool acceptedFirst = false;
        bool markerWritten = false;
        connect(&launcher, &QtOperationLauncher::accepted, this, [&] {
            if (mode == "missing") {
                QFile marker(job.Paths.first() + ".accepted");
                markerWritten = marker.open(QIODevice::WriteOnly);
            }
        });
        connect(&launcher, &QtOperationLauncher::finished, this,
                [&] { acceptedFirst = accepted.size() == 1; });
        QString error;
        QVERIFY2(launcher.start(QCoreApplication::applicationFilePath(), job, error),
                 qPrintable(error));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 15000);
        QVERIFY(acceptedFirst);
        if (mode == "missing")
            QVERIFY(markerWritten);
        const auto result = qvariant_cast<QtOperationResult>(finished[0][0]);
        QCOMPARE(result.Status, outcome);
        if (code >= 0)
            QCOMPARE(result.ExitCode, code);
        const QStringList directories = outcome == Outcome::Success
                                            ? QStringList{job.Destination, directory.path()}
                                            : QStringList{directory.path()};
        QCOMPARE(result.ChangedDirectories, directories);
        QCOMPARE(result.ChangedArchives, job.Paths);
        if (outcome != Outcome::Success)
            QVERIFY(!result.Error.isEmpty());
        // A later poll must not publish the same completion again.
        QTest::qWait(75);
        QCOMPARE(finished.size(), 1);
    }
};

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    const auto arguments = application.arguments();
    const int channel = arguments.indexOf(QStringLiteral("--operation-channel"));
    if (channel >= 0)
        return RunPeer(application, arguments.value(channel + 1));
    OperationObserverTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "operation_observer_tests.moc"
