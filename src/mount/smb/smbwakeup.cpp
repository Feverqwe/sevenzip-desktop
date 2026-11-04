#include "mount/smb/smbwakeup.h"
#include <utility>
#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

struct SmbWakeup::State {
#ifdef _WIN32
    SOCKET Descriptor = INVALID_SOCKET;
    bool Started = false;
    ~State() {
        if (Descriptor != INVALID_SOCKET)
            closesocket(Descriptor);
        if (Started)
            WSACleanup();
    }
#else
    int Descriptor = -1;
    ~State() {
        if (Descriptor >= 0)
            close(Descriptor);
    }
#endif
};

SmbWakeup::SmbWakeup() {
    auto state = std::make_unique<State>();
#ifdef _WIN32
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data))
        return;
    state->Started = true;
    const auto fd =
        WSASocketW(AF_INET, SOCK_DGRAM, IPPROTO_UDP, nullptr, 0, WSA_FLAG_NO_HANDLE_INHERIT);
    state->Descriptor = fd;
    if (fd == INVALID_SOCKET)
        return;
    u_long nonblocking = 1;
    if (ioctlsocket(fd, FIONBIO, &nonblocking))
        return;
#else
    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    state->Descriptor = fd;
    if (fd < 0 || fd >= FD_SETSIZE || fcntl(fd, F_SETFD, FD_CLOEXEC) < 0 ||
        fcntl(fd, F_SETFL, O_NONBLOCK) < 0)
        return;
#endif
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)))
        return;
#ifdef _WIN32
    int size = sizeof(address);
#else
    socklen_t size = sizeof(address);
#endif
    if (getsockname(fd, reinterpret_cast<sockaddr *>(&address), &size) ||
        connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)))
        return;
    state_ = std::move(state);
}
SmbWakeup::~SmbWakeup() = default;
bool SmbWakeup::valid() const { return bool(state_); }
void SmbWakeup::addTo(fd_set &read, int &maximum) const {
    if (!valid())
        return;
    FD_SET(state_->Descriptor, &read);
#ifndef _WIN32
    if (state_->Descriptor > maximum)
        maximum = state_->Descriptor;
#else
    (void)maximum; // Winsock select ignores nfds.
#endif
}
void SmbWakeup::notify() const {
    if (valid()) {
        const char byte = 0;
        // Nonblocking. A full receive queue is already readable by select.
        send(state_->Descriptor, &byte, 1, 0);
    }
}
void SmbWakeup::drain() const {
    if (valid()) {
        char bytes[256];
        while (recv(state_->Descriptor, bytes, sizeof(bytes), 0) > 0) {
        }
    }
}
