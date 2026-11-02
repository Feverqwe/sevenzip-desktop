#include "fileops/native/FileDir.h"
#include <QCoreApplication>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <limits.h>
#include <new>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace {
thread_local bool injectAfterCreation = false;
thread_local bool failNextAllocation = false;
char createdPath[PATH_MAX]{};
constexpr char prefix[] = "sevenzip-temp-adoption-";
} // namespace

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

// Arm after the actual native creation, without a production fault hook or an
// allocation-count assumption. Other Qt/runtime directories are unaffected.
extern "C" int mkdir(const char *path, mode_t mode) noexcept {
    const int result = int(::syscall(SYS_mkdirat, AT_FDCWD, path, mode));
    if (result == 0 && injectAfterCreation && std::strstr(path, prefix) &&
        std::strlen(path) < sizeof(createdPath)) {
        std::strcpy(createdPath, path);
        injectAfterCreation = false;
        failNextAllocation = true;
    }
    return result;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    bool allocationFailed = false;
    {
        NWindows::NFile::NDir::CTempDir directory;
        injectAfterCreation = true;
        try {
            directory.Create(FTEXT("sevenzip-temp-adoption-"));
        } catch (const std::bad_alloc &) {
            allocationFailed = true;
        }
        injectAfterCreation = failNextAllocation = false;
    }
    struct stat information{};
    const bool removed =
        createdPath[0] && ::lstat(createdPath, &information) < 0 && errno == ENOENT;
    if (!allocationFailed || !removed) {
        if (createdPath[0])
            ::rmdir(createdPath);
        std::fprintf(stderr, "temporary directory: allocation exception=%d, object removed=%d\n",
                     int(allocationFailed), int(removed));
        return 1;
    }
    return 0;
}
