#include "archive/backend/archivebackend.h"
#include <QMutexLocker>
#include <QPointer>
#include <QThread>
#include <utility>

bool ArchiveBackend::startWorkerPhase(QThread &worker, std::function<void()> launch) {
    const QPointer<ArchiveBackend> owner(this);
    const QPointer<QThread> pending(&worker);
    // Apply the dialog's retained controls before the new worker can do I/O.
    // A synchronous control failure can cancel/join and release this worker.
    emit phaseStarted();
    if (!owner || !pending)
        return false;
    if (launch)
        launch();
    else
        pending->start();
    return true;
}

// External-worker transport: like original CProgressSync's mutex snapshot and
// CProgressDialog's timer, retain the latest state instead of posting every
// native callback to the GUI queue. Native scan/hash/deletion policy is unchanged.
quint64 ArchiveBackend::beginWorkerProgress() {
    QMutexLocker lock(&workerProgressMutex_);
    pendingWorkerProgress_.reset();
    workerProgressTimer_.start();
    return ++workerProgressGeneration_;
}

void ArchiveBackend::postWorkerProgress(quint64 generation, int percent, const QString &path) {
    QMutexLocker lock(&workerProgressMutex_);
    if (generation != workerProgressGeneration_)
        return;
    pendingWorkerProgress_ = WorkerProgress{percent, path};
}

void ArchiveBackend::reportWorkerProgress() {
    std::optional<WorkerProgress> update;
    {
        QMutexLocker lock(&workerProgressMutex_);
        update = std::exchange(pendingWorkerProgress_, std::nullopt);
    }
    if (update)
        emit progress(update->Percent, update->Path);
}

void ArchiveBackend::finishWorkerProgress() {
    workerProgressTimer_.stop();
    {
        QMutexLocker lock(&workerProgressMutex_);
        ++workerProgressGeneration_;
        pendingWorkerProgress_.reset();
    }
}
