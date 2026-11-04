#include "operations/qtoperationendpoint.h"
#include "7zip/UI/Common/ExitCode.h"
#include "operations/qtoperationoptions.h"
#include "operations/qtoperationprotocol.h"
#include "rep_operation_merged.h"
#include <QApplication>
#include <QCoreApplication>
#include <QFileInfo>
#include <QRemoteObjectNode>
#include <QTimer>
#include <memory>

namespace {
QtOperationResult Result(int code) {
    using Outcome = QtOperationResult::Outcome;
    return {code == NExitCode::kSuccess     ? Outcome::Success
            : code == NExitCode::kWarning   ? Outcome::Warning
            : code == NExitCode::kUserBreak ? Outcome::Cancelled
                                            : Outcome::Failure,
            code,
            {}};
}
} // namespace

int RunGUIOperationEndpoint(QApplication &application, const QString &channel,
                            const QtGUIOperationRunner &run) {
    QRemoteObjectNode node;
    std::unique_ptr<OperationReplica> replica(node.acquire<OperationReplica>());
    QTimer startup;
    startup.setSingleShot(true);
    bool accepted = false;
    QObject context; // Disconnect stack callbacks before node/timer destruction.
    QObject::connect(&startup, &QTimer::timeout, &context,
                     [&] { application.exit(NExitCode::kUserError); });
    QObject::connect(replica.get(), &QRemoteObjectReplica::stateChanged, &context,
                     [&](QRemoteObjectReplica::State state, QRemoteObjectReplica::State) {
                         if (!accepted && (state == QRemoteObjectReplica::Suspect ||
                                           state == QRemoteObjectReplica::SignatureMismatch))
                             application.exit(NExitCode::kUserError);
                         // Losing a source after acceptance never cancels native work.
                     });
    QObject::connect(replica.get(), &QRemoteObjectReplica::initialized, &context, [&] {
        if (accepted)
            return;
        const auto request = OperationJobFromRequest(replica->request());
        const auto validated = ValidateOperationJob(request).transform([&] { return request; });
        if (!validated) {
            replica->reject(validated.error());
            application.exit(NExitCode::kUserError);
            return;
        }
        // Capture all request values before the explicit acceptance call. No
        // native operation depends on the parent's replica/property lifetime.
        accepted = true;
        startup.stop();
        replica->accept();
        QTimer::singleShot(0, &context, [&, job = *validated] {
            QStringList changedDirectories;
            QStringList changedArchives;
            QtGUICommandSource command;
            command.Configure = [&](CArcCmdLineOptions &options) {
                ConfigureOperationOptions(job, options);
            };
            if (job.Operation == QtOperationJob::Command::Hash && !job.HashBasePrefix.isEmpty())
                command.FileHash =
                    QtGUIFileHashSelection{job.HashBasePrefix, job.Paths, job.HashEnterToDirs};
            const int code = run(command, [&](const QString &path, QtGUIOutputKind kind) {
                const QString directory =
                    kind == QtGUIOutputKind::Archive ? QFileInfo(path).absolutePath() : path;
                if (kind == QtGUIOutputKind::Archive && !changedArchives.contains(path))
                    changedArchives.append(path);
                if (!changedDirectories.contains(directory))
                    changedDirectories.append(directory);
                if (replica->isReplicaValid())
                    replica->outputs(changedDirectories, changedArchives);
            });
            if (replica->isReplicaValid()) {
                auto result = Result(code);
                result.ChangedDirectories = changedDirectories;
                result.ChangedArchives = changedArchives;
                auto completion = replica->complete(OperationReportFromResult(result));
                // Await delivery, not just a local socket write. A departed
                // manager cannot hold the independent child open indefinitely.
                completion.waitForFinished(1000);
            }
            application.exit(code);
        });
    });
    QTimer::singleShot(0, &context, [&] {
        startup.start(10000);
        if (!node.connectToNode(QUrl(QStringLiteral("local:") + channel)))
            application.exit(NExitCode::kUserError);
    });
    return application.exec();
}
