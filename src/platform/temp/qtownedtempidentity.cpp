#include "platform/temp/qtownedtempidentity.h"
#include "Common/StringConvert.h"
#include "Windows/FileIO.h"
#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#ifdef _WIN32
namespace {
class CIdentityFile final : public NWindows::NFile::NIO::CFileBase {
  public:
    bool Open(CFSTR path) {
        return Create(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                      OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT);
    }
};
} // namespace
#else
#include <sys/stat.h>
#ifdef __linux__
#include <QScopeGuard>
#include <cstdlib>
#include <fcntl.h>
#include <linux/fs.h>
#include <memory>
#include <sys/ioctl.h>
#include <unistd.h>
#endif
#endif

namespace {
#ifdef __linux__
QString NativeHandleIdentity(int descriptor) {
    std::unique_ptr<file_handle, decltype(&std::free)> handle(
        static_cast<file_handle *>(std::calloc(1, sizeof(file_handle) + MAX_HANDLE_SZ)),
        &std::free);
    if (!handle)
        return {};
    handle->handle_bytes = MAX_HANDLE_SZ;
    int mount = 0;
    if (::name_to_handle_at(descriptor, "", handle.get(), &mount, AT_EMPTY_PATH) != 0 ||
        !handle->handle_bytes || handle->handle_bytes > MAX_HANDLE_SZ)
        return {};
    const QByteArray bytes(reinterpret_cast<const char *>(handle->f_handle), handle->handle_bytes);
    return QString(":h%1:%2").arg(handle->handle_type).arg(QString::fromLatin1(bytes.toHex()));
}
#endif

QString ObjectIdentity(const QString &path, [[maybe_unused]] bool nativeHandle) {
    const QFileInfo file(path);
    if (file.isSymLink() || file.isJunction())
        return {};
#ifdef _WIN32
    // The same native handle information used by source FSFolder::ReadFileInfo.
    BY_HANDLE_FILE_INFORMATION info = {};
    const auto name = path.toStdWString();
    CIdentityFile handle;
    if (!handle.Open(us2fs(name.c_str())) || !handle.GetFileInformation(&info) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
        return {};
    return QString("%1:%2:%3:%4")
        .arg(info.dwVolumeSerialNumber)
        .arg(info.nFileIndexHigh)
        .arg(info.nFileIndexLow)
        .arg((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
#else
    struct stat info;
    if (::lstat(QFile::encodeName(path).constData(), &info) != 0 ||
        (!S_ISREG(info.st_mode) && !S_ISDIR(info.st_mode)))
        return {};
    QString identity = QString("%1:%2:%3")
                           .arg(qulonglong(info.st_dev))
                           .arg(qulonglong(info.st_ino))
                           .arg(S_ISDIR(info.st_mode));
#ifdef __linux__
    // O_PATH binds metadata without requiring permission to read the object.
    const int metadata =
        ::open(QFile::encodeName(path).constData(), O_PATH | O_CLOEXEC | O_NOFOLLOW);
    if (metadata < 0)
        return {};
    const auto closeMetadata = qScopeGuard([metadata] { ::close(metadata); });
    struct stat bound;
    if (::fstat(metadata, &bound) != 0 || bound.st_dev != info.st_dev ||
        bound.st_ino != info.st_ino || (bound.st_mode & S_IFMT) != (info.st_mode & S_IFMT))
        return {};
    // Linux can immediately reuse an unlinked inode, including at the same
    // source-generated temp path. Birth time distinguishes that new object
    // from a retained edit without changing when its contents are updated.
    struct statx extended = {};
    if (::statx(metadata, "", AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW, STATX_INO | STATX_BTIME,
                &extended) == 0 &&
        (extended.stx_mask & STATX_BTIME)) {
        if (extended.stx_ino != qulonglong(info.st_ino))
            return {};
        identity +=
            QString(":%1:%2").arg(extended.stx_btime.tv_sec).arg(extended.stx_btime.tv_nsec);
    }
    if (nativeHandle) {
        const QString handle = NativeHandleIdentity(metadata);
        if (!handle.isEmpty())
            return identity + handle;
    }
    // ext4 can reuse both an inode and its coarse birth timestamp immediately.
    // Its generation distinguishes the replacement without changing on writes.
    const int descriptor =
        ::open(QFile::encodeName(path).constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (descriptor >= 0) {
        const auto close = qScopeGuard([descriptor] { ::close(descriptor); });
        struct stat opened;
        if (::fstat(descriptor, &opened) != 0 || opened.st_dev != info.st_dev ||
            opened.st_ino != info.st_ino)
            return {};
        long generation = 0;
        if (::ioctl(descriptor, FS_IOC_GETVERSION, &generation) == 0)
            identity += QString(":g%1").arg(qulonglong(static_cast<unsigned long>(generation)));
    }
#endif
    return identity;
#endif
}
} // namespace

QString QtOwnedTempIdentity(const QString &path) { return ObjectIdentity(path, true); }

QString QtOwnedTempLegacyIdentity(const QString &path) { return ObjectIdentity(path, false); }

bool QtOwnedTempMatchesIdentity(const QString &path, const QString &identity) {
    if (identity.isEmpty())
        return false;
    if (QtOwnedTempIdentity(path) == identity)
        return true;
#ifdef __linux__
    return QtOwnedTempLegacyIdentity(path) == identity;
#else
    return false;
#endif
}
