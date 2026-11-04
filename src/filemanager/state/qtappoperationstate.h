#pragma once
#include "filemanager/panels/Panel.h"
#include <array>
#include <functional>
#include <memory>

// Original FM/App/Panel operation guards surround synchronous modal calls.
// Qt workers can return before completion, so this adapter retains those same
// guards until the application owner observes completion or shutdown.
class QtAppOperationState {
  public:
    enum class Kind { Idle, Scan, Open, Operation };
    template <class Panels>
    explicit QtAppOperationState(Panels &panels)
        : panel_([&panels](unsigned index) -> CPanel & { return panels[index]; }) {}
    bool Begin(Kind kind, unsigned scanningPanels = 3);
    void End();
    void ContinueScanAfterSourceCall(unsigned panel);
    void Close();
    void ResetForCreate();
    void SetCallFinishedCallback(std::function<void()> callback);
    Kind kind() const { return kind_; }
    bool active() const { return kind_ != Kind::Idle; }
    bool blocking() const { return kind_ == Kind::Open || kind_ == Kind::Operation; }
    bool closed() const { return closed_; }
    bool callActive() const { return calls_ != 0; }
    class CallScope {
        QtAppOperationState &owner_;

      public:
        explicit CallScope(QtAppOperationState &owner) : owner_(owner) { ++owner_.calls_; }
        ~CallScope();
        CallScope(const CallScope &) = delete;
        CallScope &operator=(const CallScope &) = delete;
    };

  private:
    struct PanelGuards {
        CPanel::CDisableTimerProcessing2 timer;
        CPanel::CDisableNotify notify;
        explicit PanelGuards(CPanel &panel) : timer(&panel), notify(panel) {}
    };
    std::function<CPanel &(unsigned)> panel_;
    std::array<std::unique_ptr<PanelGuards>, 2> guards_;
    Kind kind_ = Kind::Idle;
    bool closed_ = false;
    unsigned calls_ = 0;
    std::function<void()> callFinished_;
};
