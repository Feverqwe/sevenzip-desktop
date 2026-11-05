#include "archive/callbacks/ArchiveOpenCallback.h"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <new>
#include <sys/syscall.h>
#include <unistd.h>

namespace {
thread_local const char *requestedPath = nullptr;
thread_local bool injectAfterOpen = false;
thread_local bool armed = false;
thread_local bool injected = false;
thread_local std::size_t remaining = 0;
thread_local int openedDescriptor = -1;
thread_local bool failReopenSeek = false;
thread_local bool seekFailed = false;
thread_local int seekDescriptor = -1;
} // namespace

void *operator new(std::size_t size) {
    if (armed) {
        if (remaining == 0) {
            armed = false;
            injected = true;
            throw std::bad_alloc();
        }
        --remaining;
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

// Sweep allocations after opening this fixture's real volume. The sweep stops
// when an operation completes without injection; no allocation count is pinned.
extern "C" int open(const char *path, int flags, ...) {
    mode_t mode = 0;
    if ((flags & O_CREAT) || (flags & O_TMPFILE) == O_TMPFILE) {
        va_list arguments;
        va_start(arguments, flags);
        mode = va_arg(arguments, int);
        va_end(arguments);
    }
    const int descriptor = int(::syscall(SYS_openat, AT_FDCWD, path, flags, mode));
    if (descriptor >= 0 && (injectAfterOpen || failReopenSeek) && requestedPath &&
        std::strcmp(path, requestedPath) == 0) {
        if (injectAfterOpen) {
            injectAfterOpen = false;
            openedDescriptor = descriptor;
            armed = true;
        }
        if (failReopenSeek)
            seekDescriptor = descriptor;
    }
    return descriptor;
}

extern "C" off_t lseek(int descriptor, off_t offset, int origin) noexcept {
    if (failReopenSeek && descriptor == seekDescriptor && offset == 6 && origin == SEEK_SET) {
        failReopenSeek = false;
        seekFailed = true;
        errno = EIO;
        return -1;
    }
    return off_t(::syscall(SYS_lseek, descriptor, offset, origin));
}

namespace {
bool readVolume(IInStream *stream, const char *expected) {
    char bytes[32]{};
    UInt32 read = 0;
    const auto size = UInt32(std::strlen(expected));
    return stream && stream->Seek(0, STREAM_SEEK_SET, nullptr) == S_OK &&
           stream->Read(bytes, size, &read) == S_OK && read == size &&
           std::memcmp(bytes, expected, size) == 0;
}

enum class Attempt { Failed, Injected, Complete };

bool retryAfterFailedPositionRestore(const QString &directory) {
    auto *implementation = new COpenCallbackImp;
    CMyComPtr<IArchiveOpenCallback> owner = implementation;
    CMyComPtr<IArchiveOpenVolumeCallback> volumes;
    const auto prefix = (directory + '/').toStdWString();
    if (owner.QueryInterface(IID_IArchiveOpenVolumeCallback, &volumes) != S_OK ||
        implementation->Init2(us2fs(prefix.c_str()), us2fs(L"first.001")) != S_OK)
        return false;
    CMyComPtr<IInStream> stream;
    if (volumes->GetStream(L"first.001", &stream) != S_OK ||
        stream->Seek(6, STREAM_SEEK_SET, nullptr) != S_OK)
        return false;
    // Exercise the same evicted-stream condition as the callback's LRU test.
    implementation->Volumes.Streams[0].LocalPos = 6;
    implementation->Volumes.CloseFile(0);
    const QByteArray path = QFile::encodeName(directory + "/first.001");
    requestedPath = path.constData();
    failReopenSeek = true;
    seekFailed = false;
    seekDescriptor = -1;
    char bytes[6]{};
    UInt32 read = 99;
    const HRESULT failed = stream->Read(bytes, sizeof(bytes), &read);
    failReopenSeek = false;
    requestedPath = nullptr;
    if (!seekFailed || failed == S_OK || read != 0)
        return false;
    volumes.Release();
    owner.Release();
    return stream->Read(bytes, sizeof(bytes), &read) == S_OK && read == sizeof(bytes) &&
           std::memcmp(bytes, "volume", sizeof(bytes)) == 0;
}

Attempt exercise(const QString &directory, const QString &name, std::size_t allocation) {
    auto *implementation = new COpenCallbackImp;
    CMyComPtr<IArchiveOpenCallback> owner = implementation;
    CMyComPtr<IArchiveOpenVolumeCallback> volumes;
    const auto prefix = (directory + '/').toStdWString();
    if (owner.QueryInterface(IID_IArchiveOpenVolumeCallback, &volumes) != S_OK ||
        implementation->Init2(us2fs(prefix.c_str()), us2fs(L"first.001")) != S_OK)
        return Attempt::Failed;
    CMyComPtr<IInStream> first;
    if (volumes->GetStream(L"first.001", &first) != S_OK || !readVolume(first, "first-volume"))
        return Attempt::Failed;
    const auto nativeName = name.toStdWString();
    const QByteArray path = QFile::encodeName(directory + '/' + name);
    requestedPath = path.constData();
    remaining = allocation;
    injected = false;
    openedDescriptor = -1;
    injectAfterOpen = true;
    CMyComPtr<IInStream> second;
    const HRESULT result = volumes->GetStream(nativeName.c_str(), &second);
    injectAfterOpen = armed = false;
    requestedPath = nullptr;
    if (openedDescriptor < 0)
        return Attempt::Failed;
    if (injected) {
        const bool closed = ::fcntl(openedDescriptor, F_GETFD) == -1 && errno == EBADF;
        if (result == S_OK || second || !closed) {
            std::fprintf(stderr, "failed volume registration: result=%x, descriptor closed=%d\n",
                         unsigned(result), int(closed));
            return Attempt::Failed;
        }
        if (volumes->GetStream(nativeName.c_str(), &second) != S_OK)
            return Attempt::Failed;
    } else if (result != S_OK)
        return Attempt::Failed;
    volumes.Release();
    owner.Release();
    if (!readVolume(first, "first-volume") || !readVolume(second, "second-volume"))
        return Attempt::Failed;
    return injected ? Attempt::Injected : Attempt::Complete;
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir fixture;
    if (!fixture.isValid())
        return 1;
    const QString second = QStringLiteral("второй том 空.002");
    for (const auto &name : {QString("first.001"), second}) {
        QFile file(fixture.filePath(name));
        const QByteArray bytes = name == second ? "second-volume" : "first-volume";
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
            return 1;
    }
    if (!retryAfterFailedPositionRestore(fixture.path())) {
        std::fputs("volume stream did not recover its position after a failed reopen seek\n",
                   stderr);
        return 1;
    }
    for (std::size_t allocation = 0;; ++allocation) {
        switch (exercise(fixture.path(), second, allocation)) {
        case Attempt::Failed:
            return 1;
        case Attempt::Complete:
            return 0;
        case Attempt::Injected:
            break;
        }
    }
}
