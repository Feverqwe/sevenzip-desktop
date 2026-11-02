#include "localization/qtlanguageservices.h"
#include "platform/associations/qtassociationplatform.h"
#include <QHash>

namespace {
QString key(unsigned group, const UString &ext) {
    return QString::number(group) + ':' + QString::fromWCharArray(ext.Ptr()).toCaseFolded();
}
class QtAssociationServices final : public IQtAssociationServices {
    std::shared_ptr<IQtAssociationPlatform> platform_;
    QHash<QString, QtAssociationRecord> observed_;
    LONG apply(unsigned group, const UString &ext, bool associate, const UString &title,
               const UString &command, const UString &iconPath, int iconIndex) {
        if (!CanApply(group))
            return 5; // ERROR_ACCESS_DENIED, including unsupported all-users scope.
        const QString id = key(group, ext);
        const auto previous = observed_.constFind(id);
        if (previous == observed_.cend())
            return 50; // ERROR_NOT_SUPPORTED: no successful detection snapshot.
        // Native reads can reenter detection and replace the cached record.
        // This Apply must retain the revision on which its choice was based.
        const QByteArray expectedRevision = previous->revision;
        QtAssociationRecord current;
        const LONG readResult = platform_->Read(group, ext, current);
        if (readResult != 0)
            return readResult;
        if (current.revision != expectedRevision)
            return 1237; // ERROR_RETRY: an external association change won.
        const LONG result =
            platform_->Write(group, ext, associate, current, title, command, iconPath, iconIndex);
        if (result != 0)
            return result;
        // Source page retains its original Other record for the display cycle;
        // the platform snapshot follows our successful writes for later Apply.
        QtAssociationRecord updated;
        const LONG updatedResult = platform_->Read(group, ext, updated);
        if (updatedResult != 0) {
            observed_.remove(id);
            return updatedResult;
        }
        if (!current.scope.isEmpty())
            for (auto it = observed_.begin(); it != observed_.end(); ++it)
                if (it->scope == current.scope && it->revision == current.revision)
                    *it = updated;
        observed_[id] = updated;
        return 0;
    }

  public:
    explicit QtAssociationServices(std::shared_ptr<IQtAssociationPlatform> platform)
        : platform_(std::move(platform)) {}
    bool CanApply(unsigned group) const override {
        return platform_ && group < 2 && platform_->CanApply(group);
    }
    bool CanApplyFormat(unsigned group, const UString &ext) const override {
        return CanApply(group) && observed_.contains(key(group, ext));
    }
    QString UnavailableReason(unsigned group) const override {
        return platform_
                   ? platform_->UnavailableReason(group)
                   : QCoreApplication::translate("qtassociationservices", "No association service");
    }
    QString ApplyExplanation(unsigned group) const override {
        return platform_ ? platform_->ApplyExplanation(group) : QString();
    }
    QString CurrentUserName() const override {
        return platform_ ? platform_->CurrentUserName() : QString();
    }
    bool RefreshNativeStateAfterApply() const override {
        for (const auto &record : observed_)
            if (!record.scope.isEmpty())
                return true;
        return false;
    }
    UString OwnIconPath() const override {
        return platform_ ? platform_->OwnIconPath() : UString();
    }
    QIcon OwnIcon() const override { return platform_ ? platform_->OwnIcon() : QIcon(); }
    QIcon AssociationIcon(unsigned group, const UString &ext) const override {
        const auto found = observed_.constFind(key(group, ext));
        return found == observed_.cend() ? QIcon() : found->icon;
    }
    bool Read(unsigned group, const UString &ext, NRegistryAssoc::CShellExtInfo &info) override {
        if (!platform_)
            return false;
        QtAssociationRecord record;
        if (platform_->Read(group, ext, record) != 0) {
            observed_.remove(key(group, ext));
            return false;
        }
        observed_[key(group, ext)] = record;
        info = record.info;
        return true;
    }
    LONG Add(unsigned group, const UString &ext, const NRegistryAssoc::CShellExtInfo &,
             const UString &title, const UString &command, const UString &iconPath,
             int iconIndex) override {
        return apply(group, ext, true, title, command, iconPath, iconIndex);
    }
    LONG Delete(unsigned group, const UString &ext,
                const NRegistryAssoc::CShellExtInfo &) override {
        return apply(group, ext, false, UString(), UString(), UString(), -1);
    }
    void NotifyChanged() override {
        if (platform_)
            platform_->NotifyChanged();
    }
};
} // namespace

std::shared_ptr<IQtAssociationServices>
CreateQtAssociationServices(std::shared_ptr<IQtAssociationPlatform> platform) {
    return std::make_shared<QtAssociationServices>(std::move(platform));
}
