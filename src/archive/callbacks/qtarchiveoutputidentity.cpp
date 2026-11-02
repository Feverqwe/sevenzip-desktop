#include "archive/callbacks/qtarchiveoutputidentity.h"
#include "platform/temp/qtownedtempidentity.h"
#ifdef _WIN32
#include "Common/StringConvert.h"
#include "Windows/FileFind.h"
#include "Windows/FileName.h"
#endif

namespace {
QString OutputIdentity(const QString &path) {
    const QString identity = QtOwnedTempIdentity(path);
    if (identity.isEmpty())
        return {};
#ifdef _WIN32
    const FString native = us2fs(path.toStdWString().c_str());
    NWindows::NFile::NFind::CFileInfo file;
    if (!file.Find(native))
        return {};
    if (file.IsAltStream) {
        // Find returns the actual stream spelling. Use a full path so a
        // one-character host is not mistaken for a drive prefix.
        const FString canonical =
            native.Left((unsigned)(native.ReverseFind_PathSepar() + 1)) + file.Name;
        const int colon = NWindows::NFile::NName::FindAltStreamColon(canonical);
        if (colon < 0)
            return {};
        const UString stream = fs2us(canonical.Ptr((unsigned)colon));
        if (!stream.IsEqualTo("::$DATA"))
            return identity + ":stream:" + QString::fromWCharArray(stream);
    }
#endif
    return identity;
}
} // namespace

bool QtArchiveOutputIdentity::Record(const QString &path, const QString &logical) {
    const QString identity = QtOwnedTempIdentity(path);
    if (identity.isEmpty())
        return false;
    const auto previousPhysical = logicalToPhysical_.constFind(logical);
    if (previousPhysical != logicalToPhysical_.cend() && *previousPhysical != identity)
        return false;
    const auto previousLogical = physicalToLogical_.constFind(identity);
    if (previousLogical != physicalToLogical_.cend() && *previousLogical != logical)
        return false;
    logicalToPhysical_.insert(logical, identity);
    physicalToLogical_.insert(identity, logical);
    return true;
}

void QtArchiveOutputIdentity::Clear() {
    logicalToPhysical_.clear();
    physicalToLogical_.clear();
    outputPaths_.clear();
    outputIdentities_.clear();
}

std::optional<bool> QtArchiveOutputIdentity::WasOutput(const QString &path) const {
    if (outputPaths_.contains(path))
        return true;
    if (outputPaths_.isEmpty())
        return false;
    const QString identity = OutputIdentity(path);
    if (identity.isEmpty())
        return std::nullopt;
    return outputIdentities_.contains(identity);
}

bool QtArchiveOutputIdentity::RecordOutput(const QString &path) {
    const auto identity = OutputIdentity(path);
    if (identity.isEmpty())
        return false;
    outputPaths_.insert(path);
    outputIdentities_.insert(identity);
    return true;
}
