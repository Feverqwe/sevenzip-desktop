#include "mount/smb/smbreadonlyserver.h"
#include "rep_archivemount_merged.h"
#include <QCoreApplication>
#include <QRemoteObjectNode>
#include <QRemoteObjectPendingCallWatcher>
#include <QTimer>
#include <atomic>
#include <cstdio>
#include <thread>

namespace {
SmbTreeEntry ServerEntry(const MountTreeEntry &entry) {
    return {entry.id(),   entry.parent(),    entry.size(),           entry.modified(),
            entry.name(), entry.directory(), entry.childrenLoaded(), entry.symlinkTarget()};
}

quint32 NetworkReadStatus(quint32 result) {
    if (!result)
        return 0;
    if (result == 0x80004004U) // E_ABORT
        return 0xc0000120U;    // STATUS_CANCELLED
    if (result == 0x8007000eU) // E_OUTOFMEMORY
        return 0xc000009aU;    // STATUS_INSUFFICIENT_RESOURCES
    if (result == 0x80070057U) // E_INVALIDARG
        return 0xc000000dU;    // STATUS_INVALID_PARAMETER
    return 0xc0000185U;        // STATUS_IO_DEVICE_ERROR
}
SmbReadOnlyServer::AsyncReader RemoteReader(QRemoteObjectNode &node, ArchiveMountReplica &replica,
                                            const std::atomic_bool &stopping) {
    return [&node, &replica, &stopping](quint64 id, quint64 offset, quint32 length,
                                        SmbReadOnlyServer::ReadCompletion complete) {
        // Qt owns IPC; the library event loop stays available for metadata,
        // other clients and disconnects while extraction/consent is pending.
        QMetaObject::invokeMethod(
            &node,
            [&node, &replica, &stopping, id, offset, length, complete = std::move(complete)] {
                if (stopping || !replica.isReplicaValid()) {
                    complete({0xc0000120U, {}});
                    return;
                }
                auto pending = replica.read(id, offset, length);
                auto *watcher = new QRemoteObjectPendingCallWatcher(pending, &node);
                QObject::connect(
                    watcher, &QRemoteObjectPendingCallWatcher::finished, &node,
                    [watcher, pending, complete] {
                        if (pending.error() != QRemoteObjectPendingCall::NoError)
                            complete({0xc0000185U, {}});
                        else {
                            const auto response = pending.returnValue();
                            complete({NetworkReadStatus(response.result()), response.data()});
                        }
                        watcher->deleteLater();
                    });
            },
            Qt::QueuedConnection);
    };
}

SmbReadOnlyServer::AsyncDirectoryReader RemoteDirectoryReader(QRemoteObjectNode &node,
                                                              ArchiveMountReplica &replica,
                                                              const std::atomic_bool &stopping) {
    return
        [&node, &replica, &stopping](quint64 id, SmbReadOnlyServer::DirectoryCompletion complete) {
            QMetaObject::invokeMethod(
                &node,
                [&node, &replica, &stopping, id, complete = std::move(complete)] {
                    if (stopping || !replica.isReplicaValid()) {
                        complete({0xc0000120U, {}});
                        return;
                    }
                    auto pending = replica.listDirectory(id);
                    auto *watcher = new QRemoteObjectPendingCallWatcher(pending, &node);
                    QObject::connect(watcher, &QRemoteObjectPendingCallWatcher::finished, &node,
                                     [watcher, pending, complete] {
                                         SmbTreeDirectory result;
                                         if (pending.error() != QRemoteObjectPendingCall::NoError)
                                             result.Status = 0xc0000185U;
                                         else {
                                             const auto response = pending.returnValue();
                                             result.Status = NetworkReadStatus(response.result());
                                             for (const auto &entry : response.entries())
                                                 result.Entries.append(ServerEntry(entry));
                                         }
                                         complete(std::move(result));
                                         watcher->deleteLater();
                                     });
                },
                Qt::QueuedConnection);
        };
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    const auto arguments = application.arguments();
    if (arguments.size() != 3 || arguments[1] != QStringLiteral("--source"))
        return 2;
    const QUrl endpoint(arguments[2]);
    if (endpoint.scheme() != QStringLiteral("local") || !endpoint.authority().isEmpty())
        return 2;
    QRemoteObjectNode node;
    if (!node.connectToNode(endpoint))
        return 2;
    std::unique_ptr<ArchiveMountReplica> replica(node.acquire<ArchiveMountReplica>());
    std::unique_ptr<SmbReadOnlyServer> server;
    std::thread worker;
    std::atomic_bool stopping{false};
    QTimer startup;
    startup.setSingleShot(true);
    startup.setInterval(10000);
    QObject::connect(&startup, &QTimer::timeout, &application, [&] { application.exit(3); });
    QObject::connect(replica.get(), &QRemoteObjectReplica::stateChanged, &application,
                     [&](QRemoteObjectReplica::State state, QRemoteObjectReplica::State) {
                         if (state == QRemoteObjectReplica::Valid)
                             return;
                         if (server) {
                             stopping = true;
                             server->requestStop();
                         }
                     });
    QObject::connect(replica.get(), &QRemoteObjectReplica::initialized, &application, [&] {
        if (server)
            return;
        startup.stop();
        QList<SmbTreeEntry> entries;
        for (const auto &entry : replica->entries())
            entries.append(ServerEntry(entry));
        server = std::make_unique<SmbReadOnlyServer>(
            std::move(entries), replica->user(), replica->password(),
            [](quint64, quint64, quint32) { return SmbTreeRead{0xc0000185U, {}}; },
            replica->share());
        if (qEnvironmentVariableIsSet("SEVENZIP_SMB_TRACE"))
            server->setDiagnosticHandler(
                [](const QString &message) { std::fprintf(stderr, "%s\n", qPrintable(message)); });
        server->setAsyncReader(RemoteReader(node, *replica, stopping));
        server->setAsyncDirectoryReader(RemoteDirectoryReader(node, *replica, stopping));
        QString error;
        if (!server->validate(error)) {
            replica->failed(error);
            application.exit(4);
            return;
        }
        worker = std::thread([&] {
            const int result = server->serve(0, [&](quint16 port) {
                QMetaObject::invokeMethod(
                    &node, [&, port] { replica->listening(port); }, Qt::QueuedConnection);
            });
            QMetaObject::invokeMethod(
                &node,
                [&, result] {
                    if (result && replica->isReplicaValid())
                        replica->failed(
                            QStringLiteral("The local SMB server stopped unexpectedly."));
                    application.exit(result ? 5 : 0);
                },
                Qt::QueuedConnection);
        });
    });
    startup.start();
    const int result = application.exec();
    stopping = true;
    if (server)
        server->requestStop();
    if (worker.joinable())
        worker.join();
    return result;
}
