#pragma once
#include <memory>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/select.h>
#endif

// Project-owned notification for the library's select/extra_fdset boundary.
// A completion only signals this socket; the server thread owns SMB replies.
class SmbWakeup final {
    struct State;
    std::unique_ptr<State> state_;

  public:
    SmbWakeup();
    ~SmbWakeup();
    bool valid() const;
    void addTo(fd_set &, int &) const;
    void notify() const;
    void drain() const;
};
