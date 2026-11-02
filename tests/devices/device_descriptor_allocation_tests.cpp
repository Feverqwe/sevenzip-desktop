#include "devices/deviceaccess_posix.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <new>
#include <unistd.h>

namespace {
thread_local bool failNextAllocation = false;
}

// Arm only adoption of a real read-only descriptor. No event loop or other
// allocating threads run in this standalone fixture.
void *operator new(std::size_t size) {
    if (failNextAllocation) {
        failNextAllocation = false;
        throw std::bad_alloc();
    }
    if (void *memory = std::malloc(size ? size : 1))
        return memory;
    throw std::bad_alloc();
}
void *operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete[](void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void *memory, std::size_t) noexcept { std::free(memory); }

int main() {
    const int descriptor = ::open("/dev/null", O_RDONLY);
    if (descriptor < 0)
        return 1;
    bool failed = false;
    failNextAllocation = true;
    try {
        auto handle = AdoptReadOnlyDeviceDescriptor(descriptor);
    } catch (const std::bad_alloc &) {
        failed = true;
    }
    failNextAllocation = false;
    const bool closed = ::fcntl(descriptor, F_GETFD) == -1 && errno == EBADF;
    if (!closed)
        ::close(descriptor);
    if (!failed || !closed) {
        std::fprintf(stderr, "descriptor adoption: allocation exception=%d, descriptor closed=%d\n",
                     int(failed), int(closed));
        return 1;
    }
    return 0;
}
