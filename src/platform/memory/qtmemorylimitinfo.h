// RAM/limit range adapted from 7-Zip MemDialog.cpp::CMemDialog::OnInit.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#pragma once
#include "Common/MyTypes.h"
#include "Common/MyWindows.h"
#include <cstddef>

struct QtMemoryLimitInfo {
    size_t RamSize;
    UInt32 RamSizeGB;
    unsigned MaximumGB;
    bool RamSizeDefined;
};

QtMemoryLimitInfo QtQueryMemoryLimitInfo();
