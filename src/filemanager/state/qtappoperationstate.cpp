#include "filemanager/state/qtappoperationstate.h"
#include <utility>

void QtAppOperationState::SetCallFinishedCallback(std::function<void()> callback) {
    callFinished_ = std::move(callback);
}

QtAppOperationState::CallScope::~CallScope() {
    if (--owner_.calls_ == 0 && !owner_.closed_ && owner_.callFinished_)
        owner_.callFinished_();
}

bool QtAppOperationState::Begin(Kind kind, unsigned scanningPanels) {
    if (closed_)
        return false;
    if (kind == Kind::Idle) {
        End();
        return true;
    }
    {
        // Build transactionally: allocation failure restores every already
        // acquired source guard and cannot leave a half-disabled app owner.
        std::array<std::unique_ptr<PanelGuards>, 2> guards;
        const unsigned mask = kind == Kind::Scan ? scanningPanels : 3;
        for (unsigned i = 0; i < guards.size(); ++i)
            if ((mask & (1u << i)) && !guards_[i])
                guards[i] = std::make_unique<PanelGuards>(panel_(i));
        for (unsigned i = 0; i < guards.size(); ++i) {
            if (!(mask & (1u << i)))
                guards_[i].reset();
            else if (guards[i])
                guards_[i] = std::move(guards[i]);
        }
    }
    kind_ = kind;
    return true;
}

void QtAppOperationState::End() {
    for (auto &guard : guards_)
        guard.reset();
    kind_ = Kind::Idle;
}

void QtAppOperationState::ContinueScanAfterSourceCall(unsigned panel) {
    if (kind_ != Kind::Scan || panel >= guards_.size() || !guards_[panel])
        return;
    // Native Create holds synchronous guards through RefreshListCtrl. A Qt
    // scan can outlive that call: its initial snapshot then contains the
    // temporary disabled state. After native guards restore, retain that
    // restored state for scan completion while disabling callbacks again.
    auto replacement = std::make_unique<PanelGuards>(panel_(panel));
    guards_[panel] = std::move(replacement);
    panel_(panel).Disable_Processing_Timer_Notify_StatusBar();
}

void QtAppOperationState::Close() {
    // Restore retained operation guards BEFORE ReleaseApp disables all panels.
    // No guard may later re-enable notifications on an already released owner.
    End();
    closed_ = true;
}

void QtAppOperationState::ResetForCreate() {
    End();
    closed_ = false;
}
