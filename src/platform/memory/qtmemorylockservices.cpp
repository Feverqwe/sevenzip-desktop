#include "platform/memory/qtmemorylockservices.h"
#include "settings/RegistryUtils.h"
#ifdef _WIN32
#include "Windows/MemoryLock.h"
#include "Windows/SecurityUtils.h"
#endif

bool QtMemoryLockServices::IsSupported() const {
#ifdef _WIN32
#ifdef _WIN64
    return true;
#else
    DWORD v = GetVersion();
    // low byte is major version:
    // next byte is minor version:
    v = ((v & 0xff) << 8) | ((v >> 8) & 0xFF);
    return (v > 0x501);
#endif
#else
    // The source FM privilege/large-page command is Windows-only.
    return false;
#endif
}

bool QtMemoryLockServices::AddLockMemoryPrivilege() const {
#ifdef _WIN32
    return NWindows::NSecurity::AddLockMemoryPrivilege();
#else
    return false;
#endif
}

unsigned QtMemoryLockServices::Get_LargePages_RiskLevel() const {
#ifdef _WIN32
    return NWindows::NSecurity::Get_LargePages_RiskLevel();
#else
    return 0;
#endif
}

bool QtMemoryLockServices::EnablePrivilege_LockMemory(bool enable) const {
#ifdef _WIN32
    return NWindows::NSecurity::EnablePrivilege_LockMemory(enable);
#else
    Q_UNUSED(enable)
    return false;
#endif
}

void AddLagePagesSwitch(UString &params, const QString &settingsPath,
                        const IQtMemoryLockServices *services) {
    const QtMemoryLockServices nativeMemoryLock;
    const auto &memoryLock = services ? *services : nativeMemoryLock;
    if (ReadLockMemoryEnable(settingsPath))
        if (memoryLock.Get_LargePages_RiskLevel() == 0)
            params += " -slp";
}
