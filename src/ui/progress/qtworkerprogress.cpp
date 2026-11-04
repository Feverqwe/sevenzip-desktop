#include "ui/progress/qtworkerprogress.h"
#include "ui/progress/progresssync.h"
#include <QMutex>
#include <QMutexLocker>

struct QtWorkerProgress::State {
    CProgressSync Sync;
    QMutex Publication;
    const HRESULT Initialization;
    State() : Initialization(HRESULT_FROM_WIN32(Sync.EnableSingleWorkerWake())) {}
};

QtWorkerProgress::QtWorkerProgress() : state_(std::make_unique<State>()) {}
QtWorkerProgress::~QtWorkerProgress() = default;
void QtWorkerProgress::cancel() {
    QMutexLocker lock(&state_->Publication);
    state_->Sync.Set_Stopped(true);
}
void QtWorkerProgress::setPaused(bool paused) { state_->Sync.Set_Paused(paused); }
HRESULT QtWorkerProgress::checkpoint() {
    RINOK(state_->Initialization)
    return state_->Sync.CheckStop();
}
HRESULT QtWorkerProgress::withPublicationGuard(const std::function<HRESULT()> &action) {
    RINOK(state_->Initialization)
    QMutexLocker lock(&state_->Publication);
    if (state_->Sync.Get_Stopped())
        return E_ABORT;
    return action();
}
