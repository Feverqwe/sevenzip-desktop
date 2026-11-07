// Adapted from 7-Zip CPP/7zip/UI/Common/ZipRegistry (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#pragma once
#include "7zip/UI/Common/ExtractMode.h"
#include "Common/MyString.h"
#include "archive/compression/MethodProps.h"
#include "common/collections/MyVector.h"
#include <QString>

// Shared native history deduplication for archive creation and extraction.
void AddUniqueString(CObjectVector<QString> &list, const QString &s);

struct CContextMenuInfo {
    CBoolPair Cascaded;
    CBoolPair MenuIcons;
    CBoolPair ElimDup;
    bool Flags_Def;
    UInt32 Flags;
    UInt32 WriteZone;
    void Save(const QString &path) const;
    void Load(const QString &path);
};
namespace NCompression {
struct CMemUse {
    // UString Str;
    bool IsDefined;
    bool IsPercent;
    UInt64 Val;

    CMemUse() : IsDefined(false), IsPercent(false), Val(0) {}

    void Clear() {
        // Str.Empty();
        IsDefined = false;
        IsPercent = false;
        Val = 0;
    }

    UInt64 GetBytes(UInt64 ramSize) const {
        if (!IsPercent)
            return Val;
        return Calc_From_Val_Percents(ramSize, Val);
    }
    void Parse(const QString &s);
};

// Source format profile fields; QString fields are Qt persistence adapters.
struct CFormatOptions {
    UInt32 Level;
    UInt32 Dictionary;
    UInt32 Order;
    UInt32 BlockLogSize;
    UInt32 NumThreads;
    UInt32 TimePrec = UInt32(-1);
    CBoolPair MTime;
    CBoolPair ATime;
    CBoolPair CTime;
    CBoolPair SetArcMTime;
    QString FormatID;
    QString Method;
    QString MemUse;
    UString Options;
    QString EncryptionMethod;

    void Reset_BlockLogSize() { BlockLogSize = (UInt32)(Int32)-1; }
    void Reset_TimePrec() { TimePrec = (UInt32)(Int32)-1; }
    bool IsSet_TimePrec() const { return TimePrec != (UInt32)(Int32)-1; }
    void ResetForLevelChange() {
        BlockLogSize = NumThreads = Level = Dictionary = Order = (UInt32)(Int32)-1;
        Method.clear();
    }
    CFormatOptions() { ResetForLevelChange(); }
};
struct CInfo {
    UInt32 Level = 5;
    QString ArcType = QStringLiteral("7z");
    bool ShowPassword = false;
    bool EncryptHeaders = false;
    CBoolPair NtSecurity;
    CBoolPair AltStreams;
    CBoolPair HardLinks;
    CBoolPair SymLinks;
    CBoolPair PreserveATime;
    CObjectVector<QString> ArcPaths;
    CObjectVector<CFormatOptions> Formats;
    void Save(const QString &path) const;
    void Load(const QString &path);
};
} // namespace NCompression

namespace NExtract {
UInt32 Read_LimitGB(const QString &path);
void Save_LimitGB(const QString &path, UInt32 limit_GB);
bool Read_ShowPassword(const QString &path);
void Save_ShowPassword(const QString &path, bool showPassword);
struct CInfo {
    NPathMode::EEnum PathMode;
    NOverwriteMode::EEnum OverwriteMode;
    bool PathMode_Force;
    bool OverwriteMode_Force;
    CBoolPair SplitDest;
    CBoolPair ShowPassword;
    CObjectVector<QString> Paths;
    CBoolPair ElimDup;
    CBoolPair NtSecurity;
    void Save(const QString &path) const;
    void Load(const QString &path);
};
} // namespace NExtract

namespace NWorkDir {
namespace NMode {
enum EEnum { kSystem = 0, kSpecified = 2 };
}
struct CInfo {
    NMode::EEnum Mode;
    FString Path;
    void SetDefault() {
        Mode = NMode::kSystem;
        Path.Empty();
    }
    void Save(const QString &path) const;
    void Load(const QString &path);
};
} // namespace NWorkDir
