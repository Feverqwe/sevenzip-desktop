// Adapted from 7-Zip CPP/7zip/UI/GUI/CompressDialog.cpp (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Qt presentation adapters retain upstream format, naming and SFX decisions.
#include "dialogs/compress/CompressDialog.h"
#include "7zip/UI/FileManager/resourceGui.h"
#include "7zip/UI/GUI/CompressDialogRes.h"
#include "7zip/UI/GUI/ExtractRes.h"
#include "Common/MyString.h"
#include "Common/StringConvert.h"
#include "Windows/FileName.h"
#include "archive/callbacks/qtguimessages.h"
#include "archive/compression/MethodProps.h"
#include "archive/password/archivepassword.h"
#include "filemanager/SplitUtils.h"
#include "fileops/native/FileDir.h"
#include "localization/LangUtils.h"
#include "ui/diagnostics/qtdiagnostictext.h"
#include <QDir>
#include <QFileInfo>
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
    const NativePassword Password(password);
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
        NativePassword password2(confirmation);
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
    const unsigned numFormats = ArcFormats ? ArcIndices.Size() : 0;
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
    if (unsigned(bi.FilterIndex) < numFormats) {
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
    if (unsigned(bi.FilterIndex) < numFormats && bi.FilterIndex != filterIndex) {
        Info.FormatIndex = int(ArcIndices[unsigned(bi.FilterIndex)]);
        Format = QString::fromWCharArray(Get_ArcInfoEx().Name.Ptr()).toLower();
        return true;
    }
    ArcPath_WasChanged(QString::fromWCharArray(path.Ptr()));
    return true;
}

void CCompressDialog::SetArchiveName2() {
    UString fileName(m_ArchivePath.toStdWString().c_str());
    UString draft = fileName;
    draft.Trim();
    const UString leaf(draft.Ptr(unsigned(draft.ReverseFind_PathSepar() + 1)));
    if (m_ArchivePath.contains(QChar::Null) || leaf.IsEmpty() || leaf.IsEqualTo(".") ||
        leaf.IsEqualTo("..")) {
        m_PrevFormat = Info.FormatIndex;
        return;
    }
    if (ArcFormats && m_PrevFormat >= 0) {
        const CArcInfoEx &prevArchiverInfo = (*ArcFormats)[unsigned(m_PrevFormat)];
        if (prevArchiverInfo.Flags_KeepName() || Info.KeepName) {
            UString prevExtension(".");
            prevExtension += prevArchiverInfo.GetMainExt();
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
    if (ai.Flags_KeepName()) {
        fileName.DeleteFrom(unsigned(fileName.ReverseFind_PathSepar() + 1));
        fileName += OriginalFileName;
    } else if (!Info.KeepName) {
        const int dotPos = GetExtDotPos(fileName);
        if (dotPos >= 0)
            fileName.DeleteFrom(unsigned(dotPos));
    }
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
    if (!ParseVolumeSizesText(volumeText, Info.VolumeSizes))
        return kIncorrectVolumeSize;
    if (!Info.VolumeSizes.IsEmpty()) {
        const UInt64 volumeSize = Info.VolumeSizes.Back();
        if (volumeSize < (100 << 10))
            return kConfirmSmallVolume;
    }
    return kVolumeOK;
}

void CCompressDialog::OnOK(NCompression::CInfo &registry) const {
    CObjectVector<QString> arcPaths;
    QString s;
    if (!GetFinalPath_Smart(s))
        return;
    AddUniqueArchivePath(arcPaths, s);
    registry.EncryptHeaders = EncryptHeaders;
    registry.ArcType = Format;
    registry.ShowPassword = ShowPassword;
    FOR_VECTOR(i, registry.ArcPaths) {
        if (arcPaths.Size() >= kHistorySize)
            break;
        AddUniqueArchivePath(arcPaths, registry.ArcPaths[i]);
    }
    registry.ArcPaths = arcPaths;
}

namespace NCompressDialog {
QString Modify_Auto(const QString &text) {
    const QByteArray ascii = text.toLatin1();
    AString s(ascii.constData());
    ::Modify_Auto(s);
    return QString::fromLatin1(s.Ptr());
}
} // namespace NCompressDialog

using namespace NCompressDialog;

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
