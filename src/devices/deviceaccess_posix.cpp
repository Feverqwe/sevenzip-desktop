#include "devices/deviceaccess_posix.h"
#include "Common/MyWindows.h"
#include <QScopeGuard>
#include <cerrno>
#include <fcntl.h>
#include <limits>
#include <unistd.h>

namespace {
class PosixDeviceHandle final : public DeviceReadHandle {
    int descriptor_;

  public:
    explicit PosixDeviceHandle(int descriptor) : descriptor_(descriptor) {}
    ~PosixDeviceHandle() override { ::close(descriptor_); }
    qint32 readAt(quint64 offset, void *data, quint32 size, quint32 &read) override {
        read = 0;
        if (offset > quint64(std::numeric_limits<off_t>::max()))
            return E_INVALIDARG;
        ssize_t result;
        do {
            result = ::pread(descriptor_, data, size, off_t(offset));
        } while (result < 0 && errno == EINTR);
        if (result < 0)
            return HRESULT_FROM_WIN32(errno);
        read = quint32(result);
        return S_OK;
    }
};
} // namespace

std::shared_ptr<DeviceReadHandle> AdoptReadOnlyDeviceDescriptor(int descriptor) {
    if (descriptor < 0)
        return {};
    auto close = qScopeGuard([descriptor] { ::close(descriptor); });
    const int flags = ::fcntl(descriptor, F_GETFL);
    if (flags < 0 || (flags & O_ACCMODE) != O_RDONLY ||
        ::fcntl(descriptor, F_SETFD, FD_CLOEXEC) < 0) {
        return {};
    }
    auto handle = std::make_shared<PosixDeviceHandle>(descriptor);
    close.dismiss();
    return handle;
}
