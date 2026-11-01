// Adapted from 7-Zip UI/GUI/CompressDialog.cpp (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#pragma once

#include "Common/MyTypes.h"
#include "archive/codecs/LoadCodecs.h"
#include "archive/operations/ZipRegistry.h"
#include "dialogs/browse/BrowseDialog.h"
#include "dialogs/compress/CompressInfo.h"
#include "fileops/patterns/Wildcard.h"
#include <QStringList>
#include <QStringView>
#include <functional>
class QWidget;
class QComboBox;

void SetErrorMessage_MemUsage(UString &s, UInt64 reqSize, UInt64 ramSize, UInt64 ramLimit,
                              const UString &usageString);

namespace NCompressDialog {
QString Modify_Auto(const QString &text);

enum EMethodID {
    kCopy,
    kLZMA,
    kLZMA2,
    kPPMd,
    kBZip2,
    kDeflate,
    kDeflate64,
    kZSTD,
    kSha256,
    kSha1,
    kCrc32,
    kCrc64,
    kGnu,
    kPosix
};

inline constexpr UInt32 kFF_Filter = 1 << 0;
inline constexpr UInt32 kFF_Solid = 1 << 1;
inline constexpr UInt32 kFF_MultiThread = 1 << 2;
inline constexpr UInt32 kFF_Encrypt = 1 << 3;
inline constexpr UInt32 kFF_EncryptFileNames = 1 << 4;
inline constexpr UInt32 kFF_MemUse = 1 << 5;
inline constexpr UInt32 kFF_SFX = 1 << 6;

/*
inline constexpr UInt32 kFF_Time_Win  = 1 << 10;
inline constexpr UInt32 kFF_Time_Unix = 1 << 11;
inline constexpr UInt32 kFF_Time_DOS  = 1 << 12;
inline constexpr UInt32 kFF_Time_1ns  = 1 << 13;
*/

struct CFormatInfo {
    const char *Name;
    UInt32 LevelsMask;
    unsigned NumMethods;
    const EMethodID *MethodIDs;

    UInt32 Flags;

    bool Filter_() const { return (Flags & kFF_Filter) != 0; }
    bool Solid_() const { return (Flags & kFF_Solid) != 0; }
    bool MultiThread_() const { return (Flags & kFF_MultiThread) != 0; }
    bool Encrypt_() const { return (Flags & kFF_Encrypt) != 0; }
    bool EncryptFileNames_() const { return (Flags & kFF_EncryptFileNames) != 0; }
    bool MemUse_() const { return (Flags & kFF_MemUse) != 0; }
    bool SFX_() const { return (Flags & kFF_SFX) != 0; }
};

struct CDictionaryOptions {
    UInt32 AutoDict = UInt32(-1);
    bool HasAuto = false;
    int Selected = 0;
    QList<quint64> Values;
};
CDictionaryOptions SetDictionary2(int methodID, UInt32 level, UInt32 defaultDict = UInt32(-1));
QString Combo_AddDict2(quint64 sizeShow);

struct COrderOptions {
    UInt32 AutoOrder = 1;
    int Selected = 0;
    QList<int> Values;
};
int GetMethodID(QStringView name, QStringView format);
bool GetOrderMode(int methodID);
COrderOptions SetOrder2(int methodID, UInt32 level, UInt32 defaultOrder = UInt32(-1));
struct CSolidOptions {
    UInt64 AutoSolid = 1 << 20;
    int Selected = 0;
    QList<UInt32> Values;
};
CSolidOptions SetSolidBlockSize2(QStringView format, UInt32 level, int methodId, UInt64 dict,
                                 UInt32 defaultBlockSize = UInt32(-1));
QString Add_Size(UInt64 val);
void OnOK(UInt32 solidLogSize, CInfo &Info);

struct CSystemInfo {
    UInt32 NumCPUs = 1;
    UInt32 NumHardwareThreads = 1;
    bool RamSizeDefined = false;
    UInt64 RamSize = 0;
    UInt64 RamUsageAuto = 0;
    UInt64 RamSizeReduced = 1 << 26;
};
struct CThreadOptions {
    UInt32 AutoNumThreads = 1;
    bool HasAuto = false;
    QList<UInt32> Values;
};
CSystemInfo GetSystemInfo();
CThreadOptions SetNumThreads2(QStringView format, int methodID, UInt32 level, UInt64 dict64,
                              UInt32 blockSizeLog, const CSystemInfo &system,
                              const NCompression::CMemUse &memUse = {});
UInt64 GetMemoryUsage_Threads_Dict_DecompMem(QStringView format, int methodId, UInt32 level,
                                             UInt32 numThreads, UInt64 dict64, UInt32 blockSizeLog,
                                             UInt64 &decompressMemory);
QString AddMemUsage(UInt64 v);
struct CMemComboItem {
    QString User;
    QString Spec;
};
QList<CMemComboItem> SetMemUseCombo(const QString &spec = {}, int *selected = nullptr);
UInt64 Get_MemUse_Bytes(const NCompression::CMemUse &mu, const CSystemInfo &system);

const CFormatInfo &GetFormatInfo(QStringView name);
QStringList GetMethodNames(QStringView name, bool isSFX = false);
bool IsMethodSupportedBySfx(int methodID);
} // namespace NCompressDialog

// Source-ported archive-path and encryption portions of CCompressDialog. Qt presentation
// supplies editable path text; source format/SFX naming is retained. Runtime
// module discovery and modal control presentation belong to narrow adapters.
struct CBool1 {
    bool Val;
    bool Supported;
    CBool1() : Val(false), Supported(false) {}
    void Init() {
        Val = false;
        Supported = false;
    }
    void SetTrueTrue() {
        Val = true;
        Supported = true;
    }
    void SetVal_as_Supported(bool val) {
        Val = val;
        Supported = true;
    }
};

class CCompressDialog {
  public:
    AStringVector ExternalMethods;
    void SetMethods(const CObjectVector<CCodecInfoUser> &userCodecs);
    bool OnInit(QComboBox &formatControl, const NCompression::CInfo &registry);
    static UInt32 GetComboValue(const QComboBox &control, int defMax = 0);
    const CObjectVector<CArcInfoEx> *ArcFormats = nullptr;
    CUIntVector ArcIndices;
    NCompressDialog::CInfo Info;
    UString OriginalFileName;
    int m_PrevFormat = -1;
    UString HashMethodSpec;
    bool IsSFX() const { return Info.SFXMode; }
    UString GetMethodSpec(UString &estimatedName) const;
    UString GetMethodSpec() const;
    bool IsMethodEqualTo(const UString &s) const;
    void SetMethod2(QComboBox &control, UInt32 level, const NCompression::CInfo &registry,
                    const std::function<void()> &methodChanged, int keepMethodId = -1);
    void OnHelp(QWidget *parent = nullptr);
    CBool1 SymLinks;
    CBool1 HardLinks;
    CBool1 AltStreams;
    CBool1 NtSecurity;
    CBool1 PreserveATime;
    void LoadMetadataOptions(const NCompression::CInfo &registry);
    void SaveMetadataOptions(NCompression::CInfo &registry);
    CArcInfoEx ArcInfo;
    NCompression::CFormatOptions *FormatOptions = nullptr;
    UString OptionsString;
    void ShowOptionsString();
    int MethodID = NCompressDialog::kLZMA2;
    const CArcInfoEx &Get_ArcInfoEx() const {
        return ArcFormats && Info.FormatIndex >= 0 ? (*ArcFormats)[unsigned(Info.FormatIndex)]
                                                   : ArcInfo;
    }
    unsigned GetFormatIndex() const { return unsigned(Info.FormatIndex); }
    static QString PrintMemUsage(UInt32 res, UInt64 value, const NCompression::CMemUse &mu,
                                 const NCompressDialog::CSystemInfo &system);
    NCompression::CFormatOptions &Get_FormatOptions() { return *FormatOptions; }
    // Qt control binding replaces the original CComboBox member. Non-modal
    // source callers keep their explicit MethodID/HashMethodSpec adapter.
    QComboBox *m_Method = nullptr;
    int _auto_MethodId = -1;
    UInt32 _auto_NumThreads = 1;
    // Qt supplies the source combo spec; Auto uses the cached selection phase.
    UInt32 GetNumThreads2(UInt32 num) const {
        if (num == (UInt32)(Int32)-1)
            num = _auto_NumThreads;
        return num;
    }
    int GetMethodID_RAW() const;
    int GetMethodID() const;
    enum EVolumeResult { kVolumeOK, kIncorrectVolumeSize, kConfirmSmallVolume };
    static EVolumeResult OnOK(const QString &volumeText, NCompressDialog::CInfo &Info);
    QString DirPrefix;
    QString StartDirPrefix;
    QString m_ArchivePath;
    QString Format = QStringLiteral("7z");
    bool ShowPassword = false;
    bool EncryptHeaders = false;
    QStringList _encryptionMethod;
    int _encryptionMethod_Index = -1;
    int _default_encryptionMethod_Index = -1;
    static int FindRegistryFormat(const NCompression::CInfo &registry, const QString &name);
    static unsigned FindRegistryFormat_Always(NCompression::CInfo &registry, const QString &name);
    static void SaveOptionsInMem(NCompression::CInfo &registry, const QString &format,
                                 NCompressDialog::CInfo &Info, UInt32 blockLogSize,
                                 const QString &memUse, bool saveParameters);
    static bool OnOK(UInt64 memUsage, const NCompressDialog::CSystemInfo &system,
                     const NCompression::CMemUse &memUse, UString &message);
    void SetEncryptionMethod(const NCompression::CInfo &registry);
    QString GetEncryptionMethodSpec() const;
    enum EPasswordError { kPasswordOK, kPasswordUseAscii, kPasswordTooLong, kPasswordNotMatch };
    static EPasswordError CheckPassword(const QString &password, const QString &confirmation,
                                        bool showPassword, QStringView format,
                                        const QString &encryptionMethod);
    void SetArchiveName(const QString &name);
    void SetArchiveName2(bool prevWasSFX = false);
    bool CheckSFXControlsEnable();
    void OnButtonSFX();
    void ArcPath_WasChanged(const QString &path);
    bool SetArcPathFields(const QString &path);
    bool SetArcPathFields(const QString &path, QString &name, bool always);
    bool GetFinalPath_Smart(QString &resPath) const;
    using BrowseFileHandler =
        std::function<bool(CBrowseInfo &, const CObjectVector<CBrowseFilterInfo> &)>;
    bool OnButtonSetArchive(QWidget *parent, const BrowseFileHandler &browse = {});
    void OnOK(NCompression::CInfo &registry) const;
};
