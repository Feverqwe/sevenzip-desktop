#pragma once
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <functional>
#include <memory>
#include <mutex>
class QLockFile;

struct QtOwnedTempEntry {
    QString key;
    QString path;
    QString identity;
    bool externallyProtected = false;
    bool active = false;
    QJsonObject context;
};

// Records ownership of application-created temporary objects and read leases.
class QtOwnedTempLease final : public std::enable_shared_from_this<QtOwnedTempLease> {
    friend class QtOwnedTempRegistry;
    QString root_;
    QtOwnedTempEntry entry_;
    std::unique_ptr<QLockFile> lock_;
    mutable std::mutex access_;
    unsigned readers_ = 0;
    QtOwnedTempLease(QString root, QtOwnedTempEntry entry, std::unique_ptr<QLockFile> lock);
    bool setExternalProtection(bool value);

  public:
    ~QtOwnedTempLease();
    bool ProtectExternal();
    bool ExternalFinished();
    bool MatchesObjectAt(const QString &path) const;
    bool RelocateAfterMove(const QString &path);
    bool SetContext(const QJsonObject &context);
    std::shared_ptr<const QtOwnedTempLease> PinForRead();
    bool RemoveIfUnused(const QString &ownedPath, const std::function<void()> &remove);
    QString path() const {
        const std::lock_guard access(access_);
        return entry_.path;
    }
};

class QtOwnedTempRegistry final {
    QString root_;
    std::shared_ptr<QtOwnedTempLease> ClaimInactive(const QtOwnedTempEntry &entry,
                                                    bool allowExternal) const;

  public:
    explicit QtOwnedTempRegistry(const QString &root = {});
    // Called immediately after an application-owned object is created, never
    // to discover ownership from a name prefix or archive-provided marker.
    std::shared_ptr<QtOwnedTempLease> RegisterCreated(const QString &path) const;
    QVector<QtOwnedTempEntry> Entries() const;
    void CleanUnusedSystemTemporaryObjects() const;
    // Claims one inactive object and revalidates its persisted OS identity.
    std::shared_ptr<QtOwnedTempLease> ClaimForDeletion(const QtOwnedTempEntry &entry) const;
    // Pins a retained copy for reading without granting permission to clear it.
    std::shared_ptr<const QtOwnedTempLease> PinForRead(const QtOwnedTempEntry &entry) const;
    bool SetContext(const QtOwnedTempEntry &entry, const QJsonObject &context) const;
    const QString &root() const { return root_; }
};
