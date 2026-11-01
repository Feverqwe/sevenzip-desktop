// Declarations extracted from 7-Zip CPP/7zip/UI/GUI/CompressDialog.h (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#pragma once

#include "archive/operations/ZipRegistry.h"
#include "fileops/patterns/Wildcard.h"
#include <QString>

namespace NCompressDialog {
namespace NUpdateMode {
enum EEnum { kAdd, kUpdate, kFresh, kSync };
}

struct CInfo {
    UString ArcPath;
    QString Password;
    int FormatIndex = -1;
    bool KeepName = false;
    bool SFXMode = false;
    CRecordVector<UInt64> VolumeSizes;
    UInt32 Level = UInt32(-1);
    UString Method;
    UInt64 Dict64 = UInt64(-1);
    bool OrderMode = false;
    UInt32 Order = UInt32(-1);
    UString Options;
    UInt32 NumThreads = UInt32(-1);
    bool EncryptHeadersIsAllowed = false;
    bool EncryptHeaders = false;
    UInt32 TimePrec = UInt32(-1);
    CBoolPair MTime;
    CBoolPair CTime;
    CBoolPair ATime;
    CBoolPair SetArcMTime;
    CBoolPair SymLinks;
    CBoolPair HardLinks;
    CBoolPair AltStreams;
    CBoolPair NtSecurity;
    CBoolPair PreserveATime;
    bool OpenShareForWrite = false;
    bool DeleteAfterCompressing = false;
    NUpdateMode::EEnum UpdateMode = NUpdateMode::kAdd;
    NWildcard::ECensorPathMode PathMode = NWildcard::k_RelatPath;
    bool SolidIsSpecified = false;
    UInt64 SolidBlockSize = 0;
    NCompression::CMemUse MemUsage;
    QString EncryptionMethod;
};
} // namespace NCompressDialog
