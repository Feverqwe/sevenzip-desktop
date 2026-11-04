// Adapted from 7-Zip CPP/7zip/UI/GUI/CompressDialog.cpp (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Qt presentation adapters retain upstream format, naming and SFX decisions.
#include "dialogs/compress/CompressDialog.h"
#include "7zip/PropID.h"
#include "7zip/UI/FileManager/resourceGui.h"
#include "7zip/UI/GUI/CompressDialogRes.h"
#include "7zip/UI/GUI/ExtractRes.h"
#include "Common/MyString.h"
#include "Common/StringConvert.h"
#include "Windows/FileName.h"
#include "archive/callbacks/qtguimessages.h"
#include "archive/compression/MethodProps.h"
#include "dialogs/compress/qtcompressoptionsdialog.h"
#include "filemanager/HelpUtils.h"
#include "filemanager/PropertyName.h"
#include "filemanager/SplitUtils.h"
#include "fileops/native/FileDir.h"
#include "localization/LangUtils.h"
#include "ui/diagnostics/qtdiagnostictext.h"
#include <QCollator>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>

static const unsigned kHistorySize = 20;

// Original PrintMemUsage body: Qt text return replaces SetItemText; the caller
// supplies the parsed memory spec and the source system-information fields.
QString CCompressDialog::PrintMemUsage(UInt32 res, UInt64 value, const NCompression::CMemUse &mu,
                                       const NCompressDialog::CSystemInfo &system) {
    if (value == (UInt64)(Int64)-1)
        return QStringLiteral("?");
    QString s = NCompressDialog::AddMemUsage(value);
    if (res == IDT_COMPRESS_MEMORY_VALUE) {
        if (mu.IsDefined) {
            s += " / ";
            s += NCompressDialog::AddMemUsage(mu.GetBytes(system.RamSizeReduced));
        } else if (system.RamSizeDefined) {
            s += " / ";
            s += NCompressDialog::AddMemUsage(system.RamUsageAuto);
        }
        if (system.RamSizeDefined) {
            s += " / ";
            s += NCompressDialog::AddMemUsage(system.RamSize);
        }
    }
    return s;
}

// Original CompressDialog.cpp FormatChanged/OnOK bool-pair bodies. Qt uses
// explicit load/save entry points for the modal Options transaction.
static void Combine_Two_BoolPairs(const CBoolPair &b1, const CBoolPair &b2, CBool1 &res) {
    if (!b1.Def && b2.Def)
        res.Val = b2.Val;
    else
        res.Val = b1.Val;
}
static void Set_Final_BoolPairs(const CBool1 &gui, CBoolPair &cmd, CBoolPair &reg) {
    if (!cmd.Def) {
        reg.Val = gui.Val;
        reg.Def = gui.Val;
    }
    if (gui.Supported) {
        cmd.Val = gui.Val;
        cmd.Def = gui.Val;
    } else
        cmd.Init();
}
void CCompressDialog::LoadMetadataOptions(const NCompression::CInfo &registry) {
    Combine_Two_BoolPairs(Info.SymLinks, registry.SymLinks, SymLinks);
    Combine_Two_BoolPairs(Info.HardLinks, registry.HardLinks, HardLinks);
    Combine_Two_BoolPairs(Info.AltStreams, registry.AltStreams, AltStreams);
    Combine_Two_BoolPairs(Info.NtSecurity, registry.NtSecurity, NtSecurity);
    SymLinks.Supported = ArcInfo.Flags_SymLinks();
    HardLinks.Supported = ArcInfo.Flags_HardLinks();
#ifdef Q_OS_WIN
    AltStreams.Supported = ArcInfo.Flags_AltStreams();
    NtSecurity.Supported = ArcInfo.Flags_NtSecurity();
#endif
}
void CCompressDialog::SaveMetadataOptions(NCompression::CInfo &registry) {
    Set_Final_BoolPairs(SymLinks, Info.SymLinks, registry.SymLinks);
    Set_Final_BoolPairs(HardLinks, Info.HardLinks, registry.HardLinks);
    Set_Final_BoolPairs(AltStreams, Info.AltStreams, registry.AltStreams);
    Set_Final_BoolPairs(NtSecurity, Info.NtSecurity, registry.NtSecurity);
}
// Common/Update.cpp uses an empty SFX extension on Unix. The native console
// module is an executable for that host; Windows retains GUI .exe naming.
static const wchar_t *const kExeExt =
#ifdef _WIN32
    L".exe";
#else
    L"";
#endif
static const char *const k_Auto_Prefix = "*  ";

static void Modify_Auto(AString &s) { s.Insert(0, k_Auto_Prefix); }

static void AddSize_MB(UString &s, UInt64 size) {
    s.Add_LF();
    const UInt64 v2 = size + ((UInt32)1 << 20) - 1;
    if (size < v2)
        size = v2;
    s += UString(QCoreApplication::translate("MemoryUnits", "%1 MB : ")
                     .arg(QLocale().toString(qulonglong(size >> 20)))
                     .toStdWString()
                     .c_str());
}

static void AddSize_MB_id(UString &s, UInt64 size, UInt32 id) {
    AddSize_MB(s, size);
    AddLangString(s, id);
}

void SetErrorMessage_MemUsage(UString &s, UInt64 reqSize, UInt64 ramSize, UInt64 ramLimit,
                              const UString &usageString) {
    AddLangString(s, IDS_MEM_OPERATION_BLOCKED);
    s.Add_LF();
    AddLangString(s, IDS_MEM_REQUIRES_BIG_MEM);
    s.Add_LF();
    AddSize_MB(s, reqSize);
    s += usageString;
    AddSize_MB_id(s, ramSize, IDS_MEM_RAM_SIZE);
    // if (ramLimit != 0)
    {
        AddSize_MB_id(s, ramLimit, IDS_MEM_USAGE_LIMIT_SET_BY_7ZIP);
    }
    s.Add_LF();
    s.Add_LF();
    AddLangString(s, IDS_MEM_ERROR);
}

static bool IsAsciiString(const UString &s) {
    for (unsigned i = 0; i < s.Len(); i++) {
        const wchar_t c = s[i];
        if (c < 0x20 || c > 0x7F)
            return false;
    }
    return true;
}

CCompressDialog::EPasswordError CCompressDialog::CheckPassword(const QString &password,
                                                               const QString &confirmation,
                                                               bool showPassword,
                                                               QStringView format,
                                                               const QString &encryptionMethod) {
    const UString Password(password.toStdWString().c_str());
    if (format == u"zip") {
        if (!IsAsciiString(Password))
            return kPasswordUseAscii;
        UString method(encryptionMethod.toStdWString().c_str());
        if (method.IsPrefixedBy_Ascii_NoCase("aes")) {
            if (Password.Len() > 99)
                return kPasswordTooLong;
        }
    }
    if (!showPassword) {
        UString password2(confirmation.toStdWString().c_str());
        if (password2 != Password)
            return kPasswordNotMatch;
    }
    return kPasswordOK;
}

void CCompressDialog::SetEncryptionMethod(const NCompression::CInfo &registry) {
    _encryptionMethod.clear();
    _default_encryptionMethod_Index = -1;
    _encryptionMethod_Index = -1;
    if (Format == "7z") {
        _encryptionMethod.append("AES-256");
        _encryptionMethod_Index = 0;
        _default_encryptionMethod_Index = 0;
    } else if (Format == "zip") {
        const int index = FindRegistryFormat(registry, Format);
        UString encryptionMethod;
        if (index >= 0) {
            const NCompression::CFormatOptions &fo = registry.Formats[unsigned(index)];
            encryptionMethod = fo.EncryptionMethod.toStdWString().c_str();
        }
        int sel = 0;
        {
            _encryptionMethod.append("ZipCrypto");
            sel = encryptionMethod.IsPrefixedBy_Ascii_NoCase("aes") ? 1 : 0;
            _default_encryptionMethod_Index = 0;
        }
        _encryptionMethod.append("AES-256");
        _encryptionMethod_Index = sel;
    }
}

int CCompressDialog::FindRegistryFormat(const NCompression::CInfo &registry, const QString &name) {
    const UString sourceName(name.toStdWString().c_str());
    FOR_VECTOR(i, registry.Formats) {
        const NCompression::CFormatOptions &fo = registry.Formats[i];
        if (sourceName.IsEqualTo_NoCase(UString(fo.FormatID.toStdWString().c_str())))
            return (int)i;
    }
    return -1;
}

unsigned CCompressDialog::FindRegistryFormat_Always(NCompression::CInfo &registry,
                                                    const QString &name) {
    const int index = FindRegistryFormat(registry, name);
    if (index >= 0)
        return (unsigned)index;
    {
        NCompression::CFormatOptions fo;
        fo.FormatID = name;
        return registry.Formats.Add(fo);
    }
}

// Original SaveOptionsInMem body. Qt supplies native spec values from controls,
// retaining the previous format during a format-change notification.
void CCompressDialog::SaveOptionsInMem(NCompression::CInfo &registry, const QString &format,
                                       NCompressDialog::CInfo &Info, UInt32 blockLogSize,
                                       const QString &memUse, bool saveParameters) {
    const unsigned index = FindRegistryFormat_Always(registry, format);
    NCompression::CFormatOptions &fo = registry.Formats[index];
    // Qt constructs controls in phases; a missing Params control cannot replace
    // a loaded value before its presentation has been constructed.
    if (saveParameters) {
        Info.Options.Trim();
        fo.Options = Info.Options;
    }
    fo.Level = Info.Level;
    {
        const UInt64 dict64 = Info.Dict64;
        UInt32 dict32;
        if (dict64 == (UInt64)(Int64)-1)
            dict32 = (UInt32)(Int32)-1;
        else {
            dict32 = (UInt32)dict64;
            if (dict64 != dict32)
                dict32 = (UInt32)(Int32)-2;
        }
        fo.Dictionary = dict32;
    }
    fo.Order = Info.Order;
    fo.Method = QString::fromWCharArray(Info.Method.Ptr());
    fo.EncryptionMethod = Info.EncryptionMethod;
    fo.NumThreads = Info.NumThreads;
    fo.BlockLogSize = blockLogSize;
    fo.MemUse = memUse;
}

// Original OnOK memory refusal. Native calculation/system fields are supplied
// by the existing control binding; Qt displays the returned source message.
bool CCompressDialog::OnOK(UInt64 memUsage, const NCompressDialog::CSystemInfo &system,
                           const NCompression::CMemUse &memUse, UString &message) {
    if (memUsage != (UInt64)(Int64)-1) {
        const UInt64 limit = NCompressDialog::Get_MemUse_Bytes(memUse, system);
        if (memUsage > limit) {
            UString usage;
            LangString_OnlyFromLangFile(IDS_MEM_REQUIRED_MEM_SIZE, usage);
            if (usage.IsEmpty()) {
                usage = LangString(IDT_COMPRESS_MEMORY);
                usage.RemoveChar(L':');
            }
            SetErrorMessage_MemUsage(message, memUsage, system.RamSize, limit, usage);
            return false;
        }
    }
    return true;
}

QString CCompressDialog::GetEncryptionMethodSpec() const {
    UString s;
    if (!_encryptionMethod.isEmpty() && _encryptionMethod_Index >= 0 &&
        _encryptionMethod_Index < _encryptionMethod.size() &&
        _encryptionMethod_Index != _default_encryptionMethod_Index) {
        s = _encryptionMethod[_encryptionMethod_Index].toStdWString().c_str();
        s.RemoveChar(L'-');
    }
    return QString::fromWCharArray(s.Ptr());
}

static int GetExtDotPos(const UString &s) {
    const int dotPos = s.ReverseFind_Dot();
    if (dotPos > s.ReverseFind_PathSepar() + 1)
        return dotPos;
    return -1;
}

static void AddFilter(CObjectVector<CBrowseFilterInfo> &filters, const UString &description,
                      const UString &ext) {
    CBrowseFilterInfo &f = filters.AddNew();
    UString mask("*.");
    mask += ext;
    f.Masks.Add(mask);
    f.Description = description;
    f.Description += " (";
    f.Description += mask;
    f.Description += ")";
}

static const char *const k_DontSave_Exts = "xpi odt ods docx xlsx ";

// Original OnButtonSetArchive control flow. Qt supplies the save dialog and
// model fields instead of Win32 controls; the caller applies FormatChanged.
bool CCompressDialog::OnButtonSetArchive(QWidget *parent, const BrowseFileHandler &browse) {
    QString qtPath;
    if (!GetFinalPath_Smart(qtPath)) {
        ShowErrorMessageQt(parent, QtDiagnosticText(QT_TRANSLATE_NOOP("SevenZipDiagnostics",
                                                                      "Incorrect archive path")));
        return false;
    }
    UString path(qtPath.toStdWString().c_str());
    int filterIndex = -1;
    CObjectVector<CBrowseFilterInfo> filters;
    unsigned numFormats = 0;
    const bool isSFX = IsSFX();
    if (isSFX) {
        filterIndex = 0;
        const UString ext("exe");
        AddFilter(filters, ext, ext);
    } else {
        numFormats = ArcFormats ? ArcIndices.Size() : 0;
        UString desc;
        UStringVector masks;
        CStringFinder finder;
        for (unsigned i = 0; i < numFormats; i++) {
            if (ArcIndices[i] == unsigned(Info.FormatIndex))
                filterIndex = int(i);
            const CArcInfoEx &ai = (*ArcFormats)[ArcIndices[i]];
            CBrowseFilterInfo &f = filters.AddNew();
            f.Description = ai.Name;
            f.Description += " (";
            bool needSpace_desc = false;
            FOR_VECTOR(k, ai.Exts) {
                const UString &ext = ai.Exts[k].Ext;
                UString mask("*.");
                mask += ext;
                if (finder.FindWord_In_LowCaseAsciiList_NoCase(k_DontSave_Exts, ext))
                    continue;
                f.Masks.Add(mask);
                masks.Add(mask);
                if (needSpace_desc)
                    f.Description.Add_Space();
                needSpace_desc = true;
                f.Description += ext;
            }
            f.Description += ")";
            if (i != 0)
                desc.Add_Space();
            desc += ai.GetMainExt();
        }
        CBrowseFilterInfo &f = filters.AddNew();
        f.Description = LangString(IDT_COMPRESS_ARCHIVE);
        if (f.Description.IsEmpty())
            f.Description = "Archive:";
        f.Description.RemoveChar(L'&');
        f.Description += " (";
        f.Description += desc;
        f.Description += ")";
        f.Masks = masks;
    }
    AddFilter(filters, LangString(IDS_OPEN_TYPE_ALL_FILES), UString("*"));
    if (filterIndex < 0)
        filterIndex = int(filters.Size()) - 1;
    const UString title = LangString(IDS_COMPRESS_SET_ARCHIVE_BROWSE);
    CBrowseInfo bi;
    bi.lpstrTitle = title;
    bi.SaveMode = true;
    bi.FilterIndex = filterIndex;
    bi.hwndOwner = parent;
    bi.FilePath = path;
    if (!(browse ? browse(bi, filters) : bi.BrowseForFile(filters)))
        return false;
    path = bi.FilePath;
    if (isSFX) {
        const int dotPos = GetExtDotPos(path);
        if (dotPos >= 0)
            path.DeleteFrom(unsigned(dotPos));
        path += kExeExt;
    } else if (unsigned(bi.FilterIndex) < numFormats) {
        bool needAddExt = true;
        const CArcInfoEx &ai = (*ArcFormats)[ArcIndices[unsigned(bi.FilterIndex)]];
        const int dotPos = GetExtDotPos(path);
        if (dotPos >= 0) {
            const UString ext = path.Ptr(unsigned(dotPos + 1));
            if (ai.FindExtension(ext) >= 0)
                needAddExt = false;
        }
        if (needAddExt) {
            if (path.IsEmpty() || path.Back() != '.')
                path.Add_Dot();
            path += ai.GetMainExt();
        }
    }
    SetArcPathFields(QString::fromWCharArray(path.Ptr()));
    if (!isSFX && unsigned(bi.FilterIndex) < numFormats && bi.FilterIndex != filterIndex) {
        Info.FormatIndex = int(ArcIndices[unsigned(bi.FilterIndex)]);
        Format = QString::fromWCharArray(Get_ArcInfoEx().Name.Ptr()).toLower();
        return true;
    }
    ArcPath_WasChanged(QString::fromWCharArray(path.Ptr()));
    return true;
}

void CCompressDialog::SetArchiveName2(bool prevWasSFX) {
    UString fileName(m_ArchivePath.toStdWString().c_str());
    if (ArcFormats && m_PrevFormat >= 0) {
        const CArcInfoEx &prevArchiverInfo = (*ArcFormats)[unsigned(m_PrevFormat)];
        if (prevArchiverInfo.Flags_KeepName() || Info.KeepName) {
            UString prevExtension;
            if (prevWasSFX)
                prevExtension = kExeExt;
            else {
                prevExtension.Add_Dot();
                prevExtension += prevArchiverInfo.GetMainExt();
            }
            const unsigned prevExtensionLen = prevExtension.Len();
            if (fileName.Len() >= prevExtensionLen)
                if (StringsAreEqualNoCase(fileName.RightPtr(prevExtensionLen), prevExtension))
                    fileName.DeleteFrom(fileName.Len() - prevExtensionLen);
        }
    }
    SetArchiveName(QString::fromWCharArray(fileName.Ptr()));
}

void CCompressDialog::SetArchiveName(const QString &name) {
    UString fileName(name.toStdWString().c_str());
    Info.FormatIndex = (int)GetFormatIndex();
    const CArcInfoEx &ai = Get_ArcInfoEx();
    m_PrevFormat = Info.FormatIndex;
    if (ai.Flags_KeepName())
        fileName = OriginalFileName;
    else if (!Info.KeepName) {
        const int dotPos = GetExtDotPos(fileName);
        if (dotPos >= 0)
            fileName.DeleteFrom(unsigned(dotPos));
    }
    if (IsSFX())
        fileName += kExeExt;
    else {
        fileName.Add_Dot();
        UString ext = ai.GetMainExt();
        // Legacy standalone callers have no catalog. The application supplies
        // ArcFormats; this compatibility path is not native codec discovery.
        if (!ArcFormats && ext.IsEmpty())
            ext = Format.toStdWString().c_str();
        if (ai.Flags_HashHandler()) {
            UString estimatedName;
            GetMethodSpec(estimatedName);
            if (!estimatedName.IsEmpty()) {
                ext = estimatedName;
                ext.MakeLower_Ascii();
            }
        }
        fileName += ext;
    }
    m_ArchivePath = QString::fromWCharArray(fileName.Ptr());
}

void CCompressDialog::ArcPath_WasChanged(const QString &path) {
    const UString name(path.toStdWString().c_str());
    const int dotPos = GetExtDotPos(name);
    if (dotPos < 0)
        return;
    const UString ext = name.Ptr(unsigned(dotPos + 1));
    if (ArcFormats) {
        const CArcInfoEx &ai = Get_ArcInfoEx();
        if (ai.FindExtension(ext) >= 0)
            return;
        const unsigned count = ArcIndices.Size();
        for (unsigned i = 0; i < count; i++) {
            const CArcInfoEx &candidate = (*ArcFormats)[ArcIndices[i]];
            if (candidate.FindExtension(ext) >= 0) {
                Info.FormatIndex = int(ArcIndices[i]);
                Format = QString::fromWCharArray(candidate.Name.Ptr()).toLower();
                // The Qt combo binding saves options and invokes FormatChanged.
                return;
            }
        }
        return;
    }
    if (ext.IsEqualTo_NoCase(Format.toStdWString().c_str()))
        return;
    const QStringList formats = {QStringLiteral("7z"), QStringLiteral("zip")};
    for (int i = 0; i < formats.size(); i++) {
        if (ext.IsEqualTo_NoCase(formats[i].toStdWString().c_str())) {
            Format = formats[i];
            return;
        }
    }
}

bool CCompressDialog::GetFinalPath_Smart(QString &resPath) const {
    resPath.clear();
    // Qt text can carry embedded NUL, unlike the original Win32 control text.
    if (m_ArchivePath.contains(QChar::Null))
        return false;
    UString name(m_ArchivePath.toStdWString().c_str());
    name.Trim();
    FString fullPath;
    UString dirPrefx(DirPrefix.toStdWString().c_str());
    if (dirPrefx.IsEmpty())
        dirPrefx = StartDirPrefix.toStdWString().c_str();
    const bool res =
        !dirPrefx.IsEmpty()
            ? NWindows::NFile::NName::GetFullPath(us2fs(dirPrefx), us2fs(name), fullPath)
            : NWindows::NFile::NName::GetFullPath(us2fs(name), fullPath);
    if (res)
        resPath = QString::fromWCharArray(fs2us(fullPath).Ptr());
    return res;
}

bool CCompressDialog::SetArcPathFields(const QString &path) {
    QString name;
    return SetArcPathFields(path, name, true);
}

bool CCompressDialog::SetArcPathFields(const QString &path, QString &name, bool always) {
    FString resDirPrefix;
    FString resFileName;
    const UString sourcePath(path.toStdWString().c_str());
    const bool res =
        !path.contains(QChar::Null) &&
        NWindows::NFile::NDir::GetFullPathAndSplit(us2fs(sourcePath), resDirPrefix, resFileName);
    if (res) {
        DirPrefix = QString::fromWCharArray(fs2us(resDirPrefix).Ptr());
        name = QString::fromWCharArray(fs2us(resFileName).Ptr());
    } else {
        if (!always)
            return false;
        DirPrefix.clear();
        name = path;
    }
    m_ArchivePath = name;
    return res;
}

CCompressDialog::EVolumeResult CCompressDialog::OnOK(const QString &volumeText,
                                                     NCompressDialog::CInfo &Info) {
    // Original OnOK volume block; the Qt caller presents errors/confirmation.
    UString volumeString(volumeText.toStdWString().c_str());
    volumeString.Trim();
    Info.VolumeSizes.Clear();
    if (!volumeString.IsEmpty()) {
        if (!ParseVolumeSizes(volumeString, Info.VolumeSizes))
            return kIncorrectVolumeSize;
        if (!Info.VolumeSizes.IsEmpty()) {
            const UInt64 volumeSize = Info.VolumeSizes.Back();
            if (volumeSize < (100 << 10))
                return kConfirmSmallVolume;
        }
    }
    return kVolumeOK;
}

// Original shared helper is owned by ExtractDialog.cpp.
extern void AddUniqueString(CObjectVector<QString> &strings, const QString &srcString);

void CCompressDialog::OnOK(NCompression::CInfo &registry) const {
    CObjectVector<QString> arcPaths;
    QString s;
    if (!GetFinalPath_Smart(s))
        return;
    AddUniqueString(arcPaths, s);
    registry.EncryptHeaders = EncryptHeaders;
    registry.ArcType = Format;
    registry.ShowPassword = ShowPassword;
    FOR_VECTOR(i, registry.ArcPaths) {
        if (arcPaths.Size() >= kHistorySize)
            break;
        AddUniqueString(arcPaths, registry.ArcPaths[i]);
    }
    registry.ArcPaths = arcPaths;
}

bool CCompressDialog::CheckSFXControlsEnable() {
    const NCompressDialog::CFormatInfo &fi = NCompressDialog::GetFormatInfo(Format);
    bool enable = fi.SFX_();
    if (enable) {
        const int methodID = MethodID;
        enable = (methodID == -1 || NCompressDialog::IsMethodSupportedBySfx(methodID));
    }
    if (!enable)
        Info.SFXMode = false;
    return enable;
}

void CCompressDialog::OnButtonSFX() {
    UString fileName(m_ArchivePath.toStdWString().c_str());
    const int dotPos = GetExtDotPos(fileName);
    if (IsSFX()) {
        if (dotPos >= 0)
            fileName.DeleteFrom(unsigned(dotPos));
        fileName += kExeExt;
        m_ArchivePath = QString::fromWCharArray(fileName.Ptr());
    } else {
        if (dotPos >= 0) {
            const UString ext = fileName.Ptr(unsigned(dotPos));
            if (ext.IsEqualTo_NoCase(kExeExt)) {
                fileName.DeleteFrom(unsigned(dotPos));
                m_ArchivePath = QString::fromWCharArray(fileName.Ptr());
            }
        }
        SetArchiveName2(false);
    }
}

namespace NCompressDialog {
QString Modify_Auto(const QString &text) {
    const QByteArray ascii = text.toLatin1();
    AString s(ascii.constData());
    ::Modify_Auto(s);
    return QString::fromLatin1(s.Ptr());
}
static const char *const kMethodsNames[] = {"Copy",    "LZMA",      "LZMA2", "PPMd",   "BZip2",
                                            "Deflate", "Deflate64", "ZSTD",  "SHA256", "SHA1",
                                            "CRC32",   "CRC64",     "GNU",   "POSIX"};

static const EMethodID g_7zMethods[] = {kLZMA2,
                                        kLZMA,
                                        kPPMd,
                                        kBZip2,
                                        kDeflate,
                                        kDeflate64
                                        // , kZSTD
                                        ,
                                        kCopy};

static const EMethodID g_7zSfxMethods[] = {kCopy, kLZMA, kLZMA2, kPPMd};

bool IsMethodSupportedBySfx(int methodID) {
    for (unsigned i = 0; i < Z7_ARRAY_SIZE(g_7zSfxMethods); i++)
        if (methodID == g_7zSfxMethods[i])
            return true;
    return false;
}

static const EMethodID g_ZipMethods[] = {kDeflate, kDeflate64, kBZip2, kLZMA, kZSTD};

static const EMethodID g_GZipMethods[] = {kDeflate};

static const EMethodID g_BZip2Methods[] = {kBZip2};

static const EMethodID g_XzMethods[] = {kLZMA2};

/*
static const EMethodID g_ZstdMethods[] =
{
  kZSTD
};
*/

/*
static const EMethodID g_SwfcMethods[] =
{
  kDeflate
  // kLZMA
};
*/

static const EMethodID g_TarMethods[] = {kGnu, kPosix};

static const EMethodID g_HashMethods[] = {
    kSha256, kSha1
    // , kCrc32
    // , kCrc64
};

#define METHODS_PAIR(x) Z7_ARRAY_SIZE(x), x

static const CFormatInfo g_Formats[] = {
    {"",
     // (1 << 0) | (1 << 1) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9),
     ((UInt32)1 << 10) - 1,
     // (UInt32)(Int32)-1,
     0, NULL, kFF_MultiThread | kFF_MemUse},
    {
        "7z",
        // (1 << 0) | (1 << 1) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9),
        (1 << 10) - 1, METHODS_PAIR(g_7zMethods),
        kFF_Filter | kFF_Solid | kFF_MultiThread | kFF_Encrypt | kFF_EncryptFileNames | kFF_MemUse |
            kFF_SFX
        // | kFF_Time_Win
    },
    {
        "Zip", (1 << 0) | (1 << 1) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9),
        METHODS_PAIR(g_ZipMethods), kFF_MultiThread | kFF_Encrypt | kFF_MemUse
        // | kFF_Time_Win | kFF_Time_Unix | kFF_Time_DOS
    },
    {
        "GZip", (1 << 1) | (1 << 5) | (1 << 7) | (1 << 9), METHODS_PAIR(g_GZipMethods), kFF_MemUse
        // | kFF_Time_Unix
    },
    {"BZip2", (1 << 1) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9), METHODS_PAIR(g_BZip2Methods),
     kFF_MultiThread | kFF_MemUse},
    {"xz",
     // (1 << 1) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9),
     (1 << 10) - 1 - (1 << 0), // store (1 << 0) is not supported
     METHODS_PAIR(g_XzMethods), kFF_Solid | kFF_MultiThread | kFF_MemUse},
    /*
    {
      "zstd",
      // (1 << (MY_ZSTD_LEVEL_MAX + 1)) - 1,
      (1 << (9 + 1)) - 1,
      METHODS_PAIR(g_ZstdMethods),
      // kFF_Solid |
      kFF_MultiThread
      | kFF_MemUse
    },
    */
    /*
      {
        "Swfc",
        (1 << 1) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9),
        METHODS_PAIR(g_SwfcMethods),
        0
      },
    */
    {
        "Tar", (1 << 0), METHODS_PAIR(g_TarMethods), 0
        // kFF_Time_Unix | kFF_Time_Win // | kFF_Time_1ns
    },
    {
        "wim", (1 << 0), 0, NULL, 0
        // | kFF_Time_Win
    },
    {"Hash", (0 << 0), METHODS_PAIR(g_HashMethods), 0}};

static unsigned GetStaticFormatIndex(QStringView name) {
    for (unsigned i = 0; i < Z7_ARRAY_SIZE(g_Formats); i++)
        if (name.compare(QLatin1StringView(g_Formats[i].Name), Qt::CaseInsensitive) == 0)
            return i;
    return 0;
}

const CFormatInfo &GetFormatInfo(QStringView name) { return g_Formats[GetStaticFormatIndex(name)]; }

QStringList GetMethodNames(QStringView name, bool isSFX) {
    const CFormatInfo &fi = GetFormatInfo(name);
    const bool is7z = name.compare(QLatin1StringView("7z"), Qt::CaseInsensitive) == 0;
    QStringList methods;
    for (unsigned m = 0; m < fi.NumMethods; m++) {
        const int methodID = fi.MethodIDs[m];
        if (isSFX && !IsMethodSupportedBySfx(methodID))
            continue;
        const char *method = kMethodsNames[methodID];
        if (is7z)
            if (methodID == kCopy || methodID == kDeflate || methodID == kDeflate64)
                continue;
        methods.append(QString::fromLatin1(method));
    }
    return methods;
}

int GetMethodID(QStringView name, QStringView format) {
    const auto &fi = GetFormatInfo(format);
    for (unsigned i = 0; i < fi.NumMethods; i++)
        if (name == QLatin1StringView(kMethodsNames[fi.MethodIDs[i]]))
            return fi.MethodIDs[i];
    return -1;
}

} // namespace NCompressDialog

void CCompressDialog::SetMethods(const CObjectVector<CCodecInfoUser> &userCodecs) {
    using namespace NCompressDialog;
    ExternalMethods.Clear();
    {
        FOR_VECTOR(i, userCodecs) {
            const CCodecInfoUser &c = userCodecs[i];
            if (!c.EncoderIsAssigned || !c.IsFilter_Assigned || c.IsFilter || c.NumStreams != 1)
                continue;
            unsigned k;
            for (k = 0; k < Z7_ARRAY_SIZE(g_7zMethods); k++)
                if (c.Name.IsEqualTo_Ascii_NoCase(kMethodsNames[g_7zMethods[k]]))
                    break;
            if (k != Z7_ARRAY_SIZE(g_7zMethods))
                continue;
            ExternalMethods.Add(c.Name);
        }
    }
}

using namespace NCompressDialog;

// CB_ADDSTRING/CBS_SORT is a Win32 presentation service, with no portable
// upstream implementation. Preserve its selected item and returned insertion
// index; the original OnInit below owns fallback/registry/explicit selection.
static int AddSortedFormat(QComboBox &control, const UString &name, unsigned arcIndex) {
    const QString text = QString::fromWCharArray(name.Ptr()).toLower();
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    int index = 0;
    while (index < control.count() && collator.compare(control.itemText(index), text) <= 0)
        ++index;
    const bool hadSelection = control.currentIndex() >= 0;
    control.insertItem(index, text, arcIndex);
    if (!hadSelection)
        control.setCurrentIndex(-1);
    return index;
}

// Format population/selection block from original OnInit. Other initialization
// phases bind Qt controls in CreateArchiveDialog. ArcIndices is then projected
// from combo item data for source Browse/history consumers that no longer own
// an HWND; fallback still uses the first original native index before sorting.
bool CCompressDialog::OnInit(QComboBox &formatControl, const NCompression::CInfo &registry) {
    formatControl.clear();
    const bool needSetMain = (Info.FormatIndex < 0);
    FOR_VECTOR(i, ArcIndices) {
        const unsigned arcIndex = ArcIndices[i];
        const CArcInfoEx &ai = (*ArcFormats)[arcIndex];
        const int index = AddSortedFormat(formatControl, ai.Name, arcIndex);
        if (!needSetMain) {
            if (Info.FormatIndex == (int)arcIndex)
                formatControl.setCurrentIndex(index);
            continue;
        }
        if (i == 0 || ai.Name.IsEqualTo_NoCase(UString(registry.ArcType.toStdWString().c_str()))) {
            formatControl.setCurrentIndex(index);
            Info.FormatIndex = (int)arcIndex;
        }
    }
    ArcIndices.Clear();
    for (int i = 0; i < formatControl.count(); ++i)
        ArcIndices.Add(formatControl.itemData(i).toUInt());
    return formatControl.currentIndex() >= 0;
}

UInt32 CCompressDialog::GetComboValue(const QComboBox &control, int defMax) {
    if (control.count() <= defMax)
        return (UInt32)(Int32)-1;
    // Native GetItemData_of_CurSel returns CB_ERR when GetCurSel is absent.
    if (control.currentIndex() < 0)
        return (UInt32)(Int32)-1;
    return control.currentData().toUInt();
}

// Original SetMethod2/GetMethodID/GetMethodSpec bodies. Qt supplies the control,
// current level/registry and MethodChanged presentation callback; method IDs,
// auto sentinel, external-method ordering and selection branches remain source.
void CCompressDialog::SetMethod2(QComboBox &control, UInt32 level,
                                 const NCompression::CInfo &registry,
                                 const std::function<void()> &methodChanged, int keepMethodId) {
    m_Method = &control;
    m_Method->clear();
    _auto_MethodId = -1;
    const CFormatInfo &fi = GetFormatInfo(Format);
    const CArcInfoEx &ai = Get_ArcInfoEx();
    if (level == 0 && !ai.Flags_HashHandler()) {
        if (!ai.Is_Tar() && !ai.Is_Zstd()) {
            methodChanged();
            return;
        }
    }
    UString defaultMethod;
    {
        const int index = FindRegistryFormat(registry, QString::fromWCharArray(ai.Name.Ptr()));
        if (index >= 0) {
            const NCompression::CFormatOptions &fo = registry.Formats[index];
            defaultMethod = fo.Method.toStdWString().c_str();
        }
    }
    const bool isSfx = IsSFX();
    bool weUseSameMethod = false;
    const bool is7z = ai.Is_7z();
    for (unsigned m = 0;; m++) {
        int methodID;
        const char *method;
        if (m < fi.NumMethods) {
            methodID = fi.MethodIDs[m];
            method = kMethodsNames[methodID];
            if (is7z)
                if (methodID == kCopy || methodID == kDeflate || methodID == kDeflate64)
                    continue;
        } else {
            if (!is7z)
                break;
            const unsigned extIndex = m - fi.NumMethods;
            if (extIndex >= ExternalMethods.Size())
                break;
            methodID = (int)(Z7_ARRAY_SIZE(kMethodsNames) + extIndex);
            method = ExternalMethods[extIndex].Ptr();
        }
        if (isSfx)
            if (!IsMethodSupportedBySfx(methodID))
                continue;
        AString s(method);
        int writtenMethodId = methodID;
        if (m == 0) {
            _auto_MethodId = methodID;
            writtenMethodId = -1;
            s.Insert(0, "*  ");
        }
        const bool hadSelection = m_Method->currentIndex() >= 0;
        m_Method->addItem(QString::fromLatin1(s.Ptr()), writtenMethodId);
        // CComboBox::AddString does not select an item; QComboBox does.
        // Keep selection solely under the original branches below.
        if (!hadSelection)
            m_Method->setCurrentIndex(-1);
        const int itemIndex = m_Method->count() - 1;
        if (keepMethodId == methodID) {
            m_Method->setCurrentIndex(itemIndex);
            weUseSameMethod = true;
            continue;
        }
        if ((defaultMethod.IsEqualTo_Ascii_NoCase(method) || m == 0) && !weUseSameMethod)
            m_Method->setCurrentIndex(itemIndex);
    }
    if (!weUseSameMethod)
        methodChanged();
}

int CCompressDialog::GetMethodID_RAW() const {
    if (!m_Method)
        return MethodID;
    if (m_Method->count() <= 0)
        return -1;
    if (m_Method->currentIndex() < 0)
        return -1; // Original GetItemData_of_CurSel returns CB_ERR.
    return (int)(Int32)(UInt32)m_Method->currentData().toUInt();
}

int CCompressDialog::GetMethodID() const {
    if (!m_Method)
        return MethodID;
    int raw = GetMethodID_RAW();
    if (raw < 0)
        return _auto_MethodId;
    return raw;
}

UString CCompressDialog::GetMethodSpec(UString &estimatedName) const {
    estimatedName.Empty();
    if (!m_Method) {
        estimatedName = HashMethodSpec;
        return estimatedName;
    }
    if (m_Method->count() < 1)
        return estimatedName;
    const int methodIdRaw = GetMethodID_RAW();
    int methodId = methodIdRaw;
    if (methodIdRaw < 0)
        methodId = _auto_MethodId;
    UString s;
    if (methodId >= 0) {
        if ((unsigned)methodId < Z7_ARRAY_SIZE(kMethodsNames))
            estimatedName = kMethodsNames[methodId];
        else
            estimatedName =
                ExternalMethods[(unsigned)methodId - (unsigned)Z7_ARRAY_SIZE(kMethodsNames)];
        if (methodIdRaw >= 0)
            s = estimatedName;
    }
    return s;
}

UString CCompressDialog::GetMethodSpec() const {
    UString estimatedName;
    UString s = GetMethodSpec(estimatedName);
    return s;
}

bool CCompressDialog::IsMethodEqualTo(const UString &s) const {
    UString estimatedName;
    const UString shortName = GetMethodSpec(estimatedName);
    if (s.IsEmpty())
        return shortName.IsEmpty();
    return s.IsEqualTo_NoCase(estimatedName);
}

static void AddText_from_BoolPair(AString &s, const char *name, const CBoolPair &bp) {
    if (bp.Def) {
        s.Add_OptSpaced(name);
        if (!bp.Val)
            s += "-";
    }
    /*
    else if (bp.Val)
    {
      s.Add_OptSpaced("[");
      s += name;
      s += "]";
    }
    */
}

static void AddText_from_Bool1(AString &s, const char *name, const CBool1 &b) {
    if (b.Supported && b.Val)
        s.Add_OptSpaced(name);
}

void CCompressDialog::ShowOptionsString() {
    NCompression::CFormatOptions &fo = Get_FormatOptions();

    AString s;
    if (fo.IsSet_TimePrec()) {
        s.Add_OptSpaced("tp");
        s.Add_UInt32(fo.TimePrec);
    }
    AddText_from_BoolPair(s, "tm", fo.MTime);
    AddText_from_BoolPair(s, "tc", fo.CTime);
    AddText_from_BoolPair(s, "ta", fo.ATime);
    AddText_from_BoolPair(s, "-stl", fo.SetArcMTime);

    // const CArcInfoEx &ai = Get_ArcInfoEx();
    AddText_from_Bool1(s, "SL", SymLinks);
    AddText_from_Bool1(s, "HL", HardLinks);
    AddText_from_Bool1(s, "AS", AltStreams);
    AddText_from_Bool1(s, "Sec", NtSecurity);

    // AddText_from_Bool1(s, "Preserve", PreserveATime);

    OptionsString = GetUnicodeString(s);
}

void COptionsDialog::CheckButton_Bool1(UINT id, const CBool1 &b1) { CheckButton(id, b1.Val); }

void COptionsDialog::GetButton_Bool1(UINT id, CBool1 &b1) { b1.Val = IsButtonCheckedBool(id); }

void COptionsDialog::CheckButton_BoolBox(bool supported, const CBoolPair &b2, CBoolBox &bb) {
    const bool isSet = b2.Def;
    const bool val = isSet ? b2.Val : bb.DefaultVal;

    bb.IsSupported = supported;

    CheckButton(bb.Set_Id, isSet);
    ShowItem_Bool(bb.Set_Id, supported);
    CheckButton(bb.Id, val);
    EnableItem(bb.Id, isSet);
    ShowItem_Bool(bb.Id, supported);
}

void COptionsDialog::GetButton_BoolBox(CBoolBox &bb) {
    // we save value for invisible buttons too
    bb.BoolPair.Val = IsButtonCheckedBool(bb.Id);
    bb.BoolPair.Def = IsButtonCheckedBool(bb.Set_Id);
}

void COptionsDialog::Store_TimeBoxes() {
    TimePrec = GetPrecSpec();
    GetButton_BoolBox(MTime);
    GetButton_BoolBox(CTime);
    GetButton_BoolBox(ATime);
    GetButton_BoolBox(ZTime);
}

UInt32 COptionsDialog::GetComboValue(CQtCompressCombo &c, int defMax) {
    if (c.GetCount() <= defMax)
        return (UInt32)(Int32)-1;
    return (UInt32)c.GetItemData_of_CurSel();
}

static const unsigned kTimePrec_Win = 0;
static const unsigned kTimePrec_Unix = 1;
static const unsigned kTimePrec_DOS = 2;
static const unsigned kTimePrec_1ns = 3;

static void AddTimeOption(UString &s, UInt32 val, const UString &unit, const char *sys = NULL) {
    // s += " : ";
    s.Add_UInt32(val);
    s.Add_Space();
    s += unit;
    if (sys) {
        s += " : ";
        s += sys;
    }
}

int COptionsDialog::AddPrec(unsigned prec, bool isDefault, UString *textOnly) {
    UString s;
    UInt32 writePrec = prec;
    if (isDefault) {
        // s += "* ";
        // writePrec = (UInt32)(Int32)-1;
    }
    if (prec == kTimePrec_Win)
        AddTimeOption(s, 100, NsString, "Windows");
    else if (prec == kTimePrec_Unix)
        AddTimeOption(s, 1, SecString, "Unix");
    else if (prec == kTimePrec_DOS)
        AddTimeOption(s, 2, SecString, "DOS");
    else if (prec == kTimePrec_1ns)
        AddTimeOption(s, 1, NsString, "Linux");
    else if (prec == k_PropVar_TimePrec_Base)
        AddTimeOption(s, 1, SecString);
    else if (prec >= k_PropVar_TimePrec_Base) {
        UInt32 d = 1;
        for (unsigned i = prec; i < k_PropVar_TimePrec_Base + 9; i++)
            d *= 10;
        AddTimeOption(s, d, NsString);
    } else
        s.Add_UInt32(prec);
    if (textOnly) {
        *textOnly = s;
        return 0;
    }
    return (int)m_Prec.AddString_SetItemData(s, (qintptr)writePrec);
}

void COptionsDialog::OnLanguageChange() {
    LangString(IDS_COMPRESS_SEC, SecString);
    if (SecString.IsEmpty())
        SecString = "sec";
    LangString(IDS_COMPRESS_NS, NsString);
    if (NsString.IsEmpty())
        NsString = "ns";
    for (int i = 0; i < m_Prec.GetCount(); ++i) {
        UString text;
        AddPrec(m_Prec.GetItemData(i), false, &text);
        m_Prec.SetItemText(i, text);
    }
    // The existing format/method suffix is data, not translated UI text.
    auto *info = static_cast<QLabel *>(GetItem(IDT_COMPRESS_TIME_INFO));
    const QString suffix = info->text().mid(info->text().indexOf(':'));
    SetItemText(IDT_COMPRESS_TIME_INFO,
                GetNameOfProperty(kpidType, L"type") + UString(suffix.toStdWString().c_str()));
}

void COptionsDialog::SetPrec() {
    // const CFormatInfo &fi = g_Formats[cd->GetStaticFormatIndex()];
    const CArcInfoEx &ai = cd->Get_ArcInfoEx();

    // UInt32 flags = fi.Flags;

    UInt32 flags = ai.Get_TimePrecFlags();
    UInt32 defaultPrec = ai.Get_DefaultTimePrec();
    if (defaultPrec != 0)
        flags |= ((UInt32)1 << defaultPrec);

    // const NCompression::CFormatOptions &fo = cd->Get_FormatOptions();

    // unsigned defaultPrec = kTimePrec_Win;

    if (ai.Is_GZip())
        defaultPrec = kTimePrec_Unix;

    {
        UString s;
        s += GetNameOfProperty(kpidType, L"type");
        s += ": ";
        s += ai.Name;
        if (ai.Is_Tar()) {
            const int methodID = cd->GetMethodID();

            // for debug
            // defaultPrec = kTimePrec_Unix;
            // flags = (UInt32)1 << kTimePrec_Unix;

            s.Add_Colon();
            if (methodID >= 0 && (unsigned)methodID < Z7_ARRAY_SIZE(kMethodsNames))
                s += kMethodsNames[methodID];
            if (methodID == kPosix) {
                // for debug
                // flags |= (UInt32)1 << kTimePrec_Win;
                // flags |= (UInt32)1 << kTimePrec_1ns;
            }
        } else {
            // if (is_for_MethodChanging) return;
        }

        SetItemText(IDT_COMPRESS_TIME_INFO, s);
    }

    m_Prec.ResetContent();
    _auto_Prec = defaultPrec;

    unsigned selectedPrec = defaultPrec;
    {
        // if (TimePrec >= kTimePrec_Win && TimePrec <= kTimePrec_DOS)
        if ((Int32)TimePrec >= 0)
            selectedPrec = TimePrec;
    }

    int curSel = -1;
    int defaultPrecIndex = -1;
    for (unsigned prec = 0;
         // prec <= k_PropVar_TimePrec_HighPrec;
         prec <= k_PropVar_TimePrec_1ns; prec++) {
        if (((flags >> prec) & 1) == 0)
            continue;
        const bool isDefault = (defaultPrec == prec);
        const int index = AddPrec(prec, isDefault);
        if (isDefault)
            defaultPrecIndex = index;
        if (selectedPrec == prec)
            curSel = index;
    }

    if (curSel < 0 && selectedPrec > kTimePrec_DOS)
        curSel = AddPrec(selectedPrec, false); // isDefault
    if (curSel < 0)
        curSel = defaultPrecIndex;
    if (curSel >= 0)
        m_Prec.SetCurSel(curSel);

    {
        const bool isSet = IsSet_TimePrec();
        const int count = m_Prec.GetCount();
        const bool showPrec = (count != 0);
        ShowItem_Bool(IDC_COMPRESS_TIME_PREC, showPrec);
        ShowItem_Bool(IDT_COMPRESS_TIME_PREC, showPrec);
        EnableItem(IDC_COMPRESS_TIME_PREC, isSet && (count > 1));

        CheckButton(IDX_COMPRESS_PREC_SET, isSet);
        const bool setIsSupported = isSet || (count > 1);
        EnableItem(IDX_COMPRESS_PREC_SET, setIsSupported);
        ShowItem_Bool(IDX_COMPRESS_PREC_SET, setIsSupported);
    }

    SetTimeMAC();
}

void COptionsDialog::SetTimeMAC() {
    const CArcInfoEx &ai = cd->Get_ArcInfoEx();

    const bool m_allow = ai.Flags_MTime();
    bool c_allow = ai.Flags_CTime();
    bool a_allow = ai.Flags_ATime();

    if (ai.Is_Tar()) {
        const int methodID = cd->GetMethodID();
        c_allow = false;
        a_allow = false;
        if (methodID == kPosix) {
            // c_allow = true; // do we need it as change time ?
            a_allow = true;
        }
    }

    if (ai.Is_Zip()) {
        // const int methodID = GetMethodID();
        UInt32 prec = GetPrec();
        if (prec == (UInt32)(Int32)-1)
            prec = _auto_Prec;
        if (prec != kTimePrec_Win) {
            c_allow = false;
            a_allow = false;
        }
    }

    /*
    MTime.DefaultVal = true;
    CTime.DefaultVal = false;
    ATime.DefaultVal = false;
    */

    MTime.DefaultVal = ai.Flags_MTime_Default();
    CTime.DefaultVal = ai.Flags_CTime_Default();
    ATime.DefaultVal = ai.Flags_ATime_Default();

    ZTime.DefaultVal = false;

    const NCompression::CFormatOptions &fo = cd->Get_FormatOptions();

    CheckButton_BoolBox(m_allow, fo.MTime, MTime);
    CheckButton_BoolBox(c_allow, fo.CTime, CTime);
    CheckButton_BoolBox(a_allow, fo.ATime, ATime);
    CheckButton_BoolBox(true, fo.SetArcMTime, ZTime);

    if (m_allow && !fo.MTime.Def) {
        const bool isSingleFile = ai.Flags_KeepName();
        if (!isSingleFile) {
            // we can hide changing checkboxes for MTime here:
            ShowItem_Bool(MTime.Set_Id, false);
            EnableItem(MTime.Id, false);
        }
    }
    // On_CheckBoxSet_Prec_Clicked();
    // const bool isSingleFile = ai.Flags_KeepName();
    // mtime for Gz can be
}

void COptionsDialog::On_CheckBoxSet_Prec_Clicked() {
    const bool isSet = IsButtonCheckedBool(IDX_COMPRESS_PREC_SET);
    if (!isSet) {
        // We save current MAC boxes to memory before SetPrec()
        Store_TimeBoxes();
        Reset_TimePrec();
        SetPrec();
    }
    EnableItem(IDC_COMPRESS_TIME_PREC, isSet);
}

void COptionsDialog::On_CheckBoxSet_Clicked(const CBoolBox &bb) {
    const bool isSet = IsButtonCheckedBool(bb.Set_Id);
    if (!isSet)
        CheckButton(bb.Id, bb.DefaultVal);
    EnableItem(bb.Id, isSet);
}

#ifdef Z7_LANG
static const UInt32 kLangIDs_Options[] = {IDX_COMPRESS_NT_SYM_LINKS,   IDX_COMPRESS_NT_HARD_LINKS,
                                          IDX_COMPRESS_NT_ALT_STREAMS, IDX_COMPRESS_NT_SECUR,

                                          IDG_COMPRESS_TIME,           IDT_COMPRESS_TIME_PREC,
                                          IDX_COMPRESS_MTIME,          IDX_COMPRESS_CTIME,
                                          IDX_COMPRESS_ATIME,          IDX_COMPRESS_ZTIME,
                                          IDX_COMPRESS_PRESERVE_ATIME};
#endif

bool COptionsDialog::OnInit() {
#ifdef Z7_LANG
    LangSetWindowText(*this, IDB_COMPRESS_OPTIONS); // IDS_OPTIONS
    LangSetDlgItems(*this, kLangIDs_Options, Z7_ARRAY_SIZE(kLangIDs_Options));
// LangSetDlgItemText(*this, IDB_COMPRESS_TIME_DEFAULT, IDB_COMPRESS_TIME_DEFAULT);
// LangSetDlgItemText(*this, IDX_COMPRESS_TIME_DEFAULT, IDX_COMPRESS_TIME_DEFAULT);
#endif

    LangString(IDS_COMPRESS_SEC, SecString);
    if (SecString.IsEmpty())
        SecString = "sec";
    LangString(IDS_COMPRESS_NS, NsString);
    if (NsString.IsEmpty())
        NsString = "ns";

    {
        // const CArcInfoEx &ai = cd->Get_ArcInfoEx();

        ShowItem_Bool(IDX_COMPRESS_NT_SYM_LINKS, cd->SymLinks.Supported);
        ShowItem_Bool(IDX_COMPRESS_NT_HARD_LINKS, cd->HardLinks.Supported);
        ShowItem_Bool(IDX_COMPRESS_NT_ALT_STREAMS, cd->AltStreams.Supported);
        ShowItem_Bool(IDX_COMPRESS_NT_SECUR, cd->NtSecurity.Supported);

        ShowItem_Bool(IDG_COMPRESS_NTFS, cd->SymLinks.Supported || cd->HardLinks.Supported ||
                                             cd->AltStreams.Supported || cd->NtSecurity.Supported);
    }

    /* we read property from two sources:
        1) command line  : (Info)
        2) registry      : (m_RegistryInfo)
      (Info) has priority, if both are no defined */

    CheckButton_Bool1(IDX_COMPRESS_NT_SYM_LINKS, cd->SymLinks);
    CheckButton_Bool1(IDX_COMPRESS_NT_HARD_LINKS, cd->HardLinks);
    CheckButton_Bool1(IDX_COMPRESS_NT_ALT_STREAMS, cd->AltStreams);
    CheckButton_Bool1(IDX_COMPRESS_NT_SECUR, cd->NtSecurity);

    CheckButton_Bool1(IDX_COMPRESS_PRESERVE_ATIME, cd->PreserveATime);

    m_Prec.Attach(GetItem(IDC_COMPRESS_TIME_PREC));

    MTime.SetIDs(IDX_COMPRESS_MTIME, IDX_COMPRESS_MTIME_SET);
    CTime.SetIDs(IDX_COMPRESS_CTIME, IDX_COMPRESS_CTIME_SET);
    ATime.SetIDs(IDX_COMPRESS_ATIME, IDX_COMPRESS_ATIME_SET);
    ZTime.SetIDs(IDX_COMPRESS_ZTIME, IDX_COMPRESS_ZTIME_SET);

    {
        const NCompression::CFormatOptions &fo = cd->Get_FormatOptions();
        TimePrec = fo.TimePrec;
        MTime.BoolPair = fo.MTime;
        CTime.BoolPair = fo.CTime;
        ATime.BoolPair = fo.ATime;
        ZTime.BoolPair = fo.SetArcMTime;
    }

    SetPrec();

    NormalizePosition();

    return CQtCompressOptionsDialog::OnInit();
}

bool COptionsDialog::OnCommand(unsigned code, unsigned itemID, qintptr lParam) {
    if (code == CBN_SELCHANGE) {
        switch (itemID) {
        case IDC_COMPRESS_TIME_PREC: {
            Store_TimeBoxes();
            SetTimeMAC(); // for zip/tar
            return true;
        }
        }
    }
    return CQtCompressOptionsDialog::OnCommand(code, itemID, lParam);
}

bool COptionsDialog::OnButtonClicked(unsigned buttonID, QWidget *buttonHWND) {
    switch (buttonID) {
    case IDX_COMPRESS_PREC_SET: {
        On_CheckBoxSet_Prec_Clicked();
        return true;
    }
    case IDX_COMPRESS_MTIME_SET: {
        On_CheckBoxSet_Clicked(MTime);
        return true;
    }
    case IDX_COMPRESS_CTIME_SET: {
        On_CheckBoxSet_Clicked(CTime);
        return true;
    }
    case IDX_COMPRESS_ATIME_SET: {
        On_CheckBoxSet_Clicked(ATime);
        return true;
    }
    case IDX_COMPRESS_ZTIME_SET: {
        On_CheckBoxSet_Clicked(ZTime);
        return true;
    }
    }
    return CQtCompressOptionsDialog::OnButtonClicked(buttonID, buttonHWND);
}

void COptionsDialog::OnOK() {
    GetButton_Bool1(IDX_COMPRESS_NT_SYM_LINKS, cd->SymLinks);
    GetButton_Bool1(IDX_COMPRESS_NT_HARD_LINKS, cd->HardLinks);
    GetButton_Bool1(IDX_COMPRESS_NT_ALT_STREAMS, cd->AltStreams);
    GetButton_Bool1(IDX_COMPRESS_NT_SECUR, cd->NtSecurity);
    GetButton_Bool1(IDX_COMPRESS_PRESERVE_ATIME, cd->PreserveATime);

    Store_TimeBoxes();
    {
        NCompression::CFormatOptions &fo = cd->Get_FormatOptions();
        fo.TimePrec = TimePrec;
        fo.MTime = MTime.BoolPair;
        fo.CTime = CTime.BoolPair;
        fo.ATime = ATime.BoolPair;
        fo.SetArcMTime = ZTime.BoolPair;
    }

    CQtCompressOptionsDialog::OnOK();
}

#define kHelpTopic_Options "fm/plugins/7-zip/add.htm#options"
#define kHelpTopic "fm/plugins/7-zip/add.htm"
void CCompressDialog::OnHelp(QWidget *parent) { ShowHelpWindow(kHelpTopic, parent); }

void COptionsDialog::OnHelp() { ShowHelpWindow(kHelpTopic_Options, this); }
