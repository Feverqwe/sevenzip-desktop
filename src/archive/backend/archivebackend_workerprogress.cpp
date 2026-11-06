#include "archive/backend/archivebackend.h"
#include <QMutexLocker>
#include <QPointer>
#include <QThread>
#include <utility>

void ArchiveBackend::startWorkerPhase(QThread &worker) {
    const QPointer<ArchiveBackend> owner(this);
    const QPointer<QThread> pending(&worker);
    // Apply the dialog's retained controls before the new worker can do I/O.
    // A synchronous control failure can cancel/join and release this worker.
    emit phaseStarted();
    if (!owner || !pending)
        return;
    pending->start();
}

// External-worker transport: like original CProgressSync's mutex snapshot and
// CProgressDialog's timer, retain the latest state instead of posting every
// native callback to the GUI queue. Native scan/hash/deletion policy is unchanged.
std::function<void(quint64, quint64, const QString &)> ArchiveBackend::beginWorkerProgress() {
    QMutexLocker lock(&workerProgressMutex_);
    pendingWorkerProgress_.reset();
    workerProgressTimer_.start();
    const quint64 generation = ++workerProgressGeneration_;
    return [this, generation](quint64 completed, quint64 total, const QString &path) {
        const int percent = total ? int(qMin(100.0, 100.0 * completed / total)) : -1;
        QMutexLocker lock(&workerProgressMutex_);
        if (generation == workerProgressGeneration_)
            pendingWorkerProgress_ = WorkerProgress{percent, path};
    };
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
