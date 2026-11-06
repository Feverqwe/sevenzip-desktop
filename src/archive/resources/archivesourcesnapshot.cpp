#include "archive/resources/archivesourcesnapshot.h"
#include "Common/StringConvert.h"
#include "Windows/TimeUtils.h"
#include "fileops/native/qtnativefilestamp.h"
#include "platform/temp/qtownedtempidentity.h"
#include <QFileInfo>
#include <utility>

ArchiveSourceSnapshot::ArchiveSourceSnapshot(QString path, QString identity,
                                             const NWindows::NFile::NFind::CFileInfo &info,
                                             bool stable)
    : path_(std::move(path)), identity_(std::move(identity)), size_(stable ? info.Size : 0),
      mtime_(stable ? info.MTime : CFiTime{}), stable_(stable) {}

bool ArchiveSourceSnapshot::IsCurrent() const {
    if (!stable_ || identity_ != QtOwnedTempIdentity(QFileInfo(path_).canonicalFilePath()))
        return false;
    NWindows::NFile::NFind::CFileInfo current;
    return current.Find_FollowLink(us2fs(path_.toStdWString().c_str())) && current.Size == size_ &&
           Compare_FiTime(&current.MTime, &mtime_) == 0;
}

bool ArchiveSourceSnapshot::IsCurrentFor(const QString &path) const {
    return IsCurrent() &&
           QFileInfo(path).canonicalFilePath() == QFileInfo(path_).canonicalFilePath();
}

QJsonObject ArchiveSourceSnapshot::Save() const {
    if (!stable_)
        return {};
    return {{"path", path_},
            {"identity", identity_},
            {"stamp", QtNativeFileStamp{size_, mtime_}.Save()}};
}

std::optional<ArchiveSourceSnapshot> ArchiveSourceSnapshot::Restore(const QJsonObject &value) {
    const auto stamp = QtNativeFileStamp::Restore(value.value("stamp").toObject());
    const QString path = value.value("path").toString();
    const QString identity = value.value("identity").toString();
    if (!stamp || !QFileInfo(path).isAbsolute() || path.contains(QChar::Null) || identity.isEmpty())
        return {};
    NWindows::NFile::NFind::CFileInfo info;
    info.Size = stamp->size;
    info.MTime = stamp->modified;
    return ArchiveSourceSnapshot(path, identity, info, true);
}
