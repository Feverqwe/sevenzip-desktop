#pragma once
#include <memory>
#include <sys/types.h>

// POSIX child ownership only; clients retain launch and exit-status policy.
class QtChildProcessReaper final {
  public:
    QtChildProcessReaper() = default;
    ~QtChildProcessReaper();
    QtChildProcessReaper(const QtChildProcessReaper &) = delete;
    QtChildProcessReaper &operator=(const QtChildProcessReaper &) = delete;
    // Reserve before spawn so successful-child cleanup cannot allocate or wait.
    void Prepare();
    void Adopt(pid_t id) noexcept;
    // Failed spawn, successful waitpid, or ECHILD: no child remains to reap.
    void Reaped() noexcept;
    pid_t Id() const noexcept;

  private:
    struct State;
    std::shared_ptr<State> state_;
};
