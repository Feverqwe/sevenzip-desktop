#include "operations/qtoperationprotocol.h"
#include "rep_operation_merged.h"
#include <QRemoteObjectHost>
#include <QRemoteObjectNode>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

namespace {
class SelectionSource final : public OperationSource {
  public:
    OperationRequest Request;
    bool Accepted = false;
    OperationRequest request() const override { return Request; }
    void accept() override { Accepted = true; }
    void reject(QString) override {}
    void outputs(QStringList, QStringList) override {}
    void presentation(TaskPresentation) override {}
    bool complete(OperationReport report) override {
        return Accepted && OperationResultFromReport(report);
    }
};
} // namespace

class OperationProtocolTests final : public QObject {
    Q_OBJECT
  private slots:
    void largeSelectionRemainsLiteralOverRemoteObjects() {
        QTemporaryDir root;
#ifdef Q_OS_WIN
        QTemporaryDir socketRoot;
        const QUrl url("local:sevenzip-test-" + QUuid::createUuid().toString(QUuid::Id128));
#else
        QTemporaryDir socketRoot("/tmp/7zt-XXXXXX");
        const QUrl url("local:" + socketRoot.filePath("operation"));
#endif
        QVERIFY(root.isValid());
        QVERIFY(socketRoot.isValid());
        QtOperationJob job;
        for (int i = 0; i < 10000; ++i)
            job.Paths.append(
                root.filePath(QStringLiteral("空 файл %1 \"quotes\" [literal].zip").arg(i)));
        QVERIFY(ValidateOperationJob(job));
        SelectionSource source;
        source.Request = OperationRequestFromJob(job);
        QRemoteObjectHost host(url);
        QVERIFY(host.enableRemoting<OperationSourceAPI>(&source));
        QRemoteObjectNode node;
        std::unique_ptr<OperationReplica> replica(node.acquire<OperationReplica>());
        QVERIFY(node.connectToNode(url));
        QTRY_VERIFY_WITH_TIMEOUT(replica->isInitialized(), 5000);
        const auto received = OperationJobFromRequest(replica->request());
        QCOMPARE(received.Paths, job.Paths);
        QVERIFY(ValidateOperationJob(received));
        QVERIFY(!source.Accepted);
        replica->accept();
        QTRY_VERIFY(source.Accepted);
        auto completion = replica->complete(
            OperationReportFromResult({QtOperationResult::Outcome::Cancelled, 255, {}}));
        QVERIFY(completion.waitForFinished(1000));
        QVERIFY(completion.returnValue());
    }
    void benchmarkHasNoFileSelection() {
        QtOperationJob job;
        job.Operation = QtOperationJob::Command::Benchmark;
        job.TotalMode = true;
        QVERIFY(ValidateOperationJob(job));
        job.Paths = {"/unrelated-file"};
        QVERIFY(!ValidateOperationJob(job));
    }
    void unavailableSelectionIsRejected() {
        QtOperationJob job;
        QVERIFY(!ValidateOperationJob(job));
        job.Paths = {"relative/file.zip"};
        QVERIFY(!ValidateOperationJob(job));
        QTemporaryDir root;
        QVERIFY(root.isValid());
        job.Paths = {root.filePath("archive.zip") + QChar::Null + "hidden"};
        QVERIFY(!ValidateOperationJob(job));
        job.Paths = {root.filePath("archive.zip")};
        job.Operation = QtOperationJob::Command(-1);
        QVERIFY(!ValidateOperationJob(job));
    }
    void panelHashSelectionRejectsTraversalAndKeepsLiteralNames() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QtOperationJob job;
        job.Operation = QtOperationJob::Command::Hash;
        job.HashMethod = "SHA256";
        job.HashBasePrefix = root.path() + '/';
        QStringList invalidPaths{"../outside", root.filePath("absolute"), "sub/../../outside"};
#ifdef Q_OS_WIN
        invalidPaths.append({"..\\outside", "sub\\..\\outside"});
#endif
        for (const QString &path : invalidPaths) {
            job.Paths = {path};
            QVERIFY(!ValidateOperationJob(job));
        }
        job.Paths = {QStringLiteral("nested/literal [*] 空.txt")};
        QVERIFY(ValidateOperationJob(job));
    }
    void extractionRequiresAbsoluteDestinationAndSupportedZone() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QtOperationJob job;
        job.Operation = QtOperationJob::Command::Extract;
        job.Paths = {root.filePath("archive.zip")};
        job.Destination = "relative output";
        QVERIFY(!ValidateOperationJob(job));
        job.Destination = root.filePath("output");
        job.WriteZone = 3;
        QVERIFY(!ValidateOperationJob(job));
        job.WriteZone = quint32(-1);
        QVERIFY(ValidateOperationJob(job));
    }
    void contradictoryResultCannotBeReportedAsSuccess() {
        QVERIFY(!OperationResultFromReport(
            OperationReportFromResult({QtOperationResult::Outcome::Success, 2, {}})));
        QVERIFY(!OperationResultFromReport(
            OperationReportFromResult({QtOperationResult::Outcome::Crashed, 2, {}})));
        QVERIFY(!OperationResultFromReport(
            OperationReportFromResult({QtOperationResult::Outcome::Cancelled, 0, {}})));
    }
};

QTEST_GUILESS_MAIN(OperationProtocolTests)
#include "operation_protocol_tests.moc"
