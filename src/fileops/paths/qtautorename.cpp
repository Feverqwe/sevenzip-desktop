#include "fileops/paths/qtautorename.h"
#include "7zip/Common/FilePathAutoRename.h"
#include "Common/StringConvert.h"
#include "Windows/FileSystem.h"
#include "fileops/links/qtlinkservices.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>
#ifndef _WIN32
#include <unistd.h>
#endif

namespace {
qsizetype NameLength(const QString &name) {
#ifdef _WIN32
    return name.size();
#else
    return QFile::encodeName(name).size();
#endif
}

qsizetype MaximumNameLength(const QString &directory, const QString &existingName) {
#ifdef _WIN32
    const QString root = QDir::toNativeSeparators(QStorageInfo(directory).rootPath());
    if (!root.isEmpty()) {
        DWORD length = 0;
        UString volume, filesystem;
        if (NWindows::NFile::NSystem::MyGetVolumeInformation(us2fs(root.toStdWString().c_str()),
                                                             volume, nullptr, &length, nullptr,
                                                             filesystem) &&
            length)
            return length;
    }
#else
    const long length = ::pathconf(QFile::encodeName(directory).constData(), _PC_NAME_MAX);
    if (length > 0)
        return length;
#endif
    return qMax(qsizetype{255}, NameLength(existingName));
}
} // namespace

QString QtAvailableRenamePath(const QString &target,
                              const std::function<bool(const QString &)> &reserved) {
    FString renamed = us2fs(target.toStdWString().c_str());
    if (!AutoRenamePath(renamed))
        return {};
    const QString candidate = QString::fromWCharArray(fs2us(renamed));
    const QFileInfo original(target);
    const QString name = original.fileName();
    const QString directory = original.absolutePath();
    const auto limit = MaximumNameLength(directory, name);
    const QString candidateName = QFileInfo(candidate).fileName();
    const bool fits = NameLength(candidateName) <= limit;
    if (fits && (!reserved || !reserved(candidate)) && !QtIsFileSystemLink(candidate))
        return candidate;
    const auto dot = name.lastIndexOf(u'.');
    const QString extension = dot > 0 ? name.mid(dot) : QString();
    const QString stem = name.chopped(extension.size());
    unsigned number =
        fits ? candidateName.mid(stem.size() + 1).chopped(extension.size()).toUInt() + 1 : 1;
    for (; number < (1u << 30); ++number) {
        const QString suffix = u'_' + QString::number(number);
        const bool keepExtension = NameLength(suffix + extension) < limit;
        const QString tail = keepExtension ? suffix + extension : suffix;
        if (NameLength(tail) >= limit)
            break;
        QString base = keepExtension ? stem : name;
        while (!base.isEmpty() && NameLength(base + tail) > limit)
            base.chop(base.at(base.size() - 1).isLowSurrogate() ? 2 : 1);
        const QString next = QDir(directory).filePath(base + tail);
        if ((!reserved || !reserved(next)) && !QFileInfo::exists(next) && !QtIsFileSystemLink(next))
            return next;
    }
    return {};
}

bool QtAutoRenamePath(FString &path) {
    const QString renamed = QtAvailableRenamePath(QString::fromWCharArray(fs2us(path)));
    if (renamed.isEmpty())
        return false;
    path = us2fs(QDir::toNativeSeparators(renamed).toStdWString().c_str());
    return true;
}
