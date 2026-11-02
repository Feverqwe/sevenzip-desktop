#include "platform/process/qtchildprocessreaper.h"
#include <cerrno>
#include <latch>
#include <sys/wait.h>
#include <thread>
#include <utility>

struct QtChildProcessReaper::State {
    std::latch Ready{1};
    pid_t Id = 0;
};

QtChildProcessReaper::~QtChildProcessReaper() {
    if (state_)
        state_->Ready.count_down();
}

void QtChildProcessReaper::Prepare() {
    if (state_)
        return;
    auto state = std::make_shared<State>();
    std::thread worker([state] {
        state->Ready.wait();
        if (const pid_t id = state->Id; id > 0)
            while (::waitpid(id, nullptr, 0) < 0 && errno == EINTR) {
            }
    });
    try {
        worker.detach();
    } catch (...) {
        state->Ready.count_down();
        worker.join();
        throw;
    }
    state_ = std::move(state);
}

void QtChildProcessReaper::Adopt(pid_t id) noexcept { state_->Id = id; }

void QtChildProcessReaper::Reaped() noexcept {
    if (!state_)
        return;
    state_->Id = 0;
    state_->Ready.count_down();
    state_.reset();
}

pid_t QtChildProcessReaper::Id() const noexcept { return state_ ? state_->Id : 0; }
