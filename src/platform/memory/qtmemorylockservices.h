// Native services for the original File Manager large-page policy.
#pragma once
#include "Common/MyString.h"
#include <QString>

class IQtMemoryLockServices {
  public:
    virtual ~IQtMemoryLockServices() = default;
    virtual bool IsSupported() const = 0;
    virtual bool AddLockMemoryPrivilege() const = 0;
    virtual unsigned Get_LargePages_RiskLevel() const = 0;
    virtual bool EnablePrivilege_LockMemory(bool enable = true) const = 0;
};

class QtMemoryLockServices final : public IQtMemoryLockServices {
  public:
    bool IsSupported() const override;
    bool AddLockMemoryPrivilege() const override;
    unsigned Get_LargePages_RiskLevel() const override;
    bool EnablePrivilege_LockMemory(bool enable = true) const override;
};

void AddLagePagesSwitch(UString &params, const QString &settingsPath,
                        const IQtMemoryLockServices *services = nullptr);

namespace NQtFileManager {
void SetMemoryLock(const QString &settingsPath, const IQtMemoryLockServices &services);
}
