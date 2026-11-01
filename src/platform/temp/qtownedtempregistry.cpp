#include "platform/temp/qtownedtempregistry.h"
#include "platform/temp/qtownedtempidentity.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <optional>

namespace {
QString RecordPath(const QString &root, const QString &key) { return root + '/' + key + ".json"; }
QString IdentityKey(const QString &identity) {
    return QUuid::createUuidV5(QUuid("ae8d385e-4ab9-4bbf-818e-088be7c96ef2"), identity.toUtf8())
        .toString(QUuid::WithoutBraces);
}
bool SafeRoot(const QString &root, bool create) {
    QFileInfo info(root);
    if (!info.exists() && create) {
        if (!QDir().mkpath(root))
            return false;
        QFile::setPermissions(root, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        info.refresh();
    }
    return info.isDir() && !info.isSymLink() && !info.isJunction();
}
std::unique_ptr<QLockFile> ClaimLock(const QString &root, const QString &key) {
    auto lock = std::make_unique<QLockFile>(root + '/' + key + ".lock");
    // Elapsed age does not prove an active archive/editor has finished.
    lock->setStaleLockTime(0);
    return lock->tryLock() ? std::move(lock) : nullptr;
}
bool WriteRecord(const QString &root, const QtOwnedTempEntry &entry) {
    const QJsonObject data{{"version", 1},
                           {"path", entry.path},
                           {"identity", entry.identity},
                           {"external", entry.externallyProtected}};
    QSaveFile file(RecordPath(root, entry.key));
    const QByteArray bytes = QJsonDocument(data).toJson(QJsonDocument::Compact);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
std::optional<QtOwnedTempEntry> ReadRecord(const QString &root, const QString &key) {
    QtOwnedTempEntry entry;
    // Registry filenames come from generated UUIDs, including after restart.
    if (QUuid(key).isNull())
        return std::nullopt;
    QFile file(RecordPath(root, key));
    const QFileInfo record(file);
    if (record.isSymLink() || record.isJunction())
        return std::nullopt;
    if (!file.open(QIODevice::ReadOnly) || file.size() > 16384)
        return std::nullopt;
    const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
    if (object.value("version").toInt() != 1 || !object.value("external").isBool())
        return std::nullopt;
    entry.key = key;
    entry.path = object.value("path").toString();
    entry.identity = object.value("identity").toString();
    entry.externallyProtected = object.value("external").toBool();
    if (!QDir::isAbsolutePath(entry.path) || entry.identity.isEmpty())
        return std::nullopt;
    return entry;
}
bool Matches(const QtOwnedTempEntry &entry) {
    return QtOwnedTempMatchesIdentity(entry.path, entry.identity);
}
} // namespace

QtOwnedTempLease::QtOwnedTempLease(QString root, QtOwnedTempEntry entry,
                                   std::unique_ptr<QLockFile> lock)
    : root_(std::move(root)), entry_(std::move(entry)), lock_(std::move(lock)) {}

QtOwnedTempLease::~QtOwnedTempLease() {
    // Preserve a record for a retained edit or failed automatic cleanup. A later
    // native cleanup can remove it only after all active leases have finished.
    if (!Matches(entry_))
        QFile::remove(RecordPath(root_, entry_.key));
}
bool QtOwnedTempLease::setExternalProtection(bool value) {
    QtOwnedTempEntry next = entry_;
    next.externallyProtected = value;
    if (!WriteRecord(root_, next))
        return false;
    entry_ = std::move(next);
    return true;
}
bool QtOwnedTempLease::ProtectExternal() { return setExternalProtection(true); }
bool QtOwnedTempLease::ExternalFinished() { return setExternalProtection(false); }
bool QtOwnedTempLease::MatchesObjectAt(const QString &path) const {
    return QtOwnedTempMatchesIdentity(path, entry_.identity);
}
bool QtOwnedTempLease::RelocateAfterMove(const QString &path) {
    const QString destination = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    if (destination == entry_.path)
        return Matches(entry_);
    if (Matches(entry_) || !MatchesObjectAt(destination) || !SafeRoot(root_, false))
        return false;
    auto next = entry_;
    next.path = destination;
    if (!WriteRecord(root_, next))
        return false;
    entry_ = std::move(next);
    return true;
}

QtOwnedTempRegistry::QtOwnedTempRegistry(const QString &root) : root_(root) {
    if (root_.isEmpty())
        root_ = qEnvironmentVariable("SEVENZIP_OWNED_TEMP_REGISTRY");
    if (root_.isEmpty())
        root_ = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) +
                "/sevenzip-desktop/owned-temp";
    root_ = QDir::cleanPath(QFileInfo(root_).absoluteFilePath());
}

std::shared_ptr<QtOwnedTempLease> QtOwnedTempRegistry::RegisterCreated(const QString &path) const {
    const QFileInfo info(path);
    QtOwnedTempEntry entry;
    entry.path = QDir::cleanPath(info.absoluteFilePath());
    entry.identity = QtOwnedTempIdentity(entry.path);
    if (entry.identity.isEmpty() || !SafeRoot(root_, true))
        return {};
    // One lock per OS object also protects aliases and accidental duplicate
    // registration. A creation call cannot replace an existing live record.
    entry.key = IdentityKey(entry.identity);
#ifdef __linux__
    const QString legacy = QtOwnedTempLegacyIdentity(entry.path);
    if (!legacy.isEmpty() && legacy != entry.identity) {
        const QString key = IdentityKey(legacy);
        if (const auto previous = ReadRecord(root_, key);
            previous && QtOwnedTempMatchesIdentity(entry.path, previous->identity))
            entry.key = key;
    }
#endif
    auto lock = ClaimLock(root_, entry.key);
    if (!lock || ReadRecord(root_, entry.key).transform(Matches).value_or(false) ||
        !WriteRecord(root_, entry))
        return {};
    return std::shared_ptr<QtOwnedTempLease>(new QtOwnedTempLease(root_, entry, std::move(lock)));
}

QVector<QtOwnedTempEntry> QtOwnedTempRegistry::Entries() const {
    QVector<QtOwnedTempEntry> entries;
    if (!SafeRoot(root_, false))
        return entries;
    const auto files = QDir(root_).entryList({"*.json"}, QDir::Files | QDir::NoSymLinks);
    for (const QString &file : files) {
        auto entry = ReadRecord(root_, file.chopped(5));
        if (!entry || !Matches(*entry))
            continue;
        auto lock = ClaimLock(root_, entry->key);
        entry->active = !lock;
        entries.append(std::move(*entry));
    }
    return entries;
}

std::shared_ptr<QtOwnedTempLease>
QtOwnedTempRegistry::ClaimForDeletion(const QtOwnedTempEntry &requested) const {
    if (!SafeRoot(root_, false) || QUuid(requested.key).isNull())
        return {};
    auto lock = ClaimLock(root_, requested.key);
    if (!lock)
        return {};
    const auto entry = ReadRecord(root_, requested.key);
    if (!entry || entry->path != requested.path || entry->identity != requested.identity ||
        entry->externallyProtected || !Matches(*entry))
        return {};
    return std::shared_ptr<QtOwnedTempLease>(new QtOwnedTempLease(root_, *entry, std::move(lock)));
}
