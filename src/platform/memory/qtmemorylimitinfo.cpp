// RAM/limit range adapted from 7-Zip MemDialog.cpp::CMemDialog::OnInit.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "platform/memory/qtmemorylimitinfo.h"
#include "Windows/System.h"

QtMemoryLimitInfo QtQueryMemoryLimitInfo() {
    QtMemoryLimitInfo info{};
    info.RamSize = (size_t)sizeof(size_t) << 29;
    info.RamSizeDefined = NWindows::NSystem::GetRamSize(info.RamSize);
    info.RamSizeGB = (UInt32)(((UInt64)info.RamSize + (1u << 29)) >> 30);
    if (info.RamSizeGB == 0)
        info.RamSizeGB = 1;
    info.MaximumGB = 64;
    if (info.RamSizeDefined) {
        const unsigned k_max_val = 1u << 14;
        if (info.RamSizeGB >= k_max_val)
            info.MaximumGB = k_max_val;
        else if (info.RamSizeGB > 1)
            info.MaximumGB = info.RamSizeGB - 1;
        else
            info.MaximumGB = 1;
    }
    return info;
}
