// Adapted from 7-Zip CPP/7zip/UI/GUI/UpdateGUI.cpp at 0766b73.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "gui/UpdateGUI.h"
#include "platform/temp/WorkDir.h"

#include "7zip/UI/FileManager/resourceGui.h"
#include "Common/StringConvert.h"
#include "Common/StringToInt.h"
#include "Windows/FileFind.h"
#include "archive/callbacks/qtguimessages.h"
#include "archive/compression/MethodProps.h"
#include "dialogs/compress/CompressDialog.h"
#include "gui/UpdateCallbackGUI.h"
#include "gui/qtupdateguiservices.h"
#include "gui/resource2.h"
#include "localization/LangUtils.h"
#include "platform/loader/DLL.h"
#include "ui/diagnostics/qtdiagnostictext.h"

static const char *const kDefaultSfxModule = "7z.sfx";

class CThreadUpdating : public CProgressThreadVirt {
    HRESULT ProcessVirt() Z7_override;

  public:
    CCodecs *codecs;
    const CObjectVector<COpenType> *formatIndices;
    const UString *cmdArcPath;
    CUpdateCallbackGUI *UpdateCallbackGUI;
    NWildcard::CCensor *WildcardCensor;
    CUpdateOptions *Options;
    bool needSetPath;
    IQtUpdateGUIServices *Services;
};

HRESULT CThreadUpdating::ProcessVirt() {
    CUpdateErrorInfo ei;
    HRESULT res =
        Services->UpdateArchive(codecs, *formatIndices, *cmdArcPath, *WildcardCensor, *Options, ei,
                                UpdateCallbackGUI, UpdateCallbackGUI, needSetPath);
    FinalMessage.ErrorMessage.Message = QtDiagnosticLines(ei.Message);
    ErrorPaths = ei.FileNames;
    if (res != S_OK)
        return res;
    return HRESULT_FROM_WIN32(ei.SystemError);
}

HRESULT UpdateGUI(CCodecs *codecs, const CObjectVector<COpenType> &formatIndices,
                  const UString &cmdArcPath, NWildcard::CCensor &censor, CUpdateOptions &options,
                  bool showDialog, bool &messageWasDisplayed, CUpdateCallbackGUI *callback,
                  QWidget *hwndParent, IQtUpdateGUIServices &services,
                  const std::function<void()> &outputReady) {
    messageWasDisplayed = false;
    bool needSetPath = true;
    if (showDialog) {
        RINOK(ShowDialog(codecs, censor.CensorPaths, options, callback, hwndParent, services))
        needSetPath = false;
    }
    if (options.SfxMode && options.SfxModule.IsEmpty()) {
        // The retained runtime supplies its validated platform-specific asset.
        // Keep the source adjacent-module fallback for other service callers.
        options.SfxModule = services.GetSfxModulePath();
        if (options.SfxModule.IsEmpty()) {
            options.SfxModule = NWindows::NDLL::GetModuleDirPrefix();
            options.SfxModule += kDefaultSfxModule;
        }
    }
    CThreadUpdating tu;
    tu.needSetPath = needSetPath;
    tu.codecs = codecs;
    tu.formatIndices = &formatIndices;
    tu.cmdArcPath = &cmdArcPath;
    tu.Services = &services;
    tu.UpdateCallbackGUI = callback;
    tu.UpdateCallbackGUI->ProgressDialog = &tu;
    tu.UpdateCallbackGUI->Init();
    UString title = LangString(IDS_PROGRESS_COMPRESSING);
    if (!formatIndices.IsEmpty()) {
        const int fin = formatIndices[0].FormatIndex;
        if (fin >= 0)
            if (codecs->Formats[fin].Flags_HashHandler())
                title = LangString(IDS_CHECKSUM_CALCULATING);
    }
    tu.WildcardCensor = &censor;
    tu.Options = &options;
    tu.IconID = IDI_ICON;
    if (outputReady)
        outputReady();
    RINOK(tu.Create(title, hwndParent))
    messageWasDisplayed = tu.ThreadFinishedOK && tu.MessagesDisplayed;
    return tu.Result;
}

HRESULT
NCompressDialog::PrepareFormatSelection(CCompressDialog &dialog, const CCodecs &codecs,
                                        const CObjectVector<NWildcard::CCensorPath> &censor) {
    bool oneFile = false;
    NWindows::NFile::NFind::CFileInfo fileInfo;
    UString name;
    if (censor.Size() > 0) {
        const NWildcard::CCensorPath &cp = censor[0];
        if (cp.Include) {
            {
                if (fileInfo.Find(us2fs(cp.Path))) {
                    if (censor.Size() == 1)
                        oneFile = !fileInfo.IsDir();
                }
            }
        }
    }
    NCompressDialog::CInfo &di = dialog.Info;
    dialog.ArcFormats = &codecs.Formats;
    // Qt's source picker can change selection without reconstructing the modal.
    dialog.ArcIndices.Clear();
    FOR_VECTOR(i, codecs.Formats) {
        const CArcInfoEx &ai = codecs.Formats[i];
        if (!ai.UpdateEnabled)
            continue;
        if (!oneFile && ai.Flags_KeepName())
            continue;
        if ((int)i != di.FormatIndex) {
            if (ai.Flags_HashHandler())
                continue;
            if (ai.Name.IsEqualTo_Ascii_NoCase("swfc"))
                if (!oneFile || name.Len() < 4 ||
                    !StringsAreEqualNoCase_Ascii(name.RightPtr(4), ".swf"))
                    continue;
        }
        dialog.ArcIndices.Add(i);
    }
    if (dialog.ArcIndices.IsEmpty())
        return E_FAIL; // Qt presents the original No Update Engines result.
    dialog.OriginalFileName = fs2us(fileInfo.Name);
    di.KeepName = !oneFile;
    return S_OK;
}

// Source ShowDialog working-directory block; return the field instead of the
// unavailable in-process CUpdateOptions owner. The engine consumes it via -w.
FString NCompressDialog::GetCompressionWorkingDir(const UString &arcPath,
                                                  const QString &settingsPath) {
    NWorkDir::CInfo workDirInfo;
    workDirInfo.Load(settingsPath);
    FString workingDir;
    if (workDirInfo.Mode != NWorkDir::NMode::kCurrent) {
        FString fullPath;
        NWindows::NFile::NDir::MyGetFullPathName(us2fs(arcPath), fullPath);
        FString namePart;
        workingDir = GetWorkDir(workDirInfo, fullPath, namePart);
        NWindows::NFile::NDir::CreateComplexDir(workingDir);
    }
    return workingDir;
}

static void AddProp_UString(CObjectVector<CProperty> &properties, const char *name,
                            const UString &value) {
    CProperty prop;
    prop.Name = name;
    prop.Value = value;
    properties.Add(prop);
}

// Existing Qt encryption controls supply QString; all source properties use UString.
static void AddProp_UString(CObjectVector<CProperty> &properties, const char *name,
                            const QString &value) {
    AddProp_UString(properties, name, UString(value.toStdWString().c_str()));
}

static void AddProp_UInt32(CObjectVector<CProperty> &properties, const char *name, UInt32 value) {
    UString s;
    s.Add_UInt32(value);
    AddProp_UString(properties, name, s);
}

static void AddProp_bool(CObjectVector<CProperty> &properties, const char *name, bool value) {
    AddProp_UString(properties, name, UString(value ? "on" : "off"));
}

static void AddProp_BoolPair(CObjectVector<CProperty> &properties, const char *name,
                             const CBoolPair &bp) {
    if (bp.Def)
        AddProp_bool(properties, name, bp.Val);
}

static void SplitOptionsToStrings(const UString &src, UStringVector &strings) {
    SplitString(src, strings);
    FOR_VECTOR(i, strings) {
        UString &s = strings[i];
        if (s.Len() > 2 && s[0] == '-' && MyCharLower_Ascii(s[1]) == 'm')
            s.DeleteFrontal(2);
    }
}

static bool IsThereMethodOverride(bool is7z, const UStringVector &strings) {
    FOR_VECTOR(i, strings) {
        const UString &s = strings[i];
        if (is7z) {
            const wchar_t *end;
            UInt64 n = ConvertStringToUInt64(s, &end);
            if (n == 0 && *end == L'=')
                return true;
        } else {
            if (s.Len() > 0)
                if (s[0] == L'm' && s[1] == L'=')
                    return true;
        }
    }
    return false;
}

static void ParseAndAddPropertires(CObjectVector<CProperty> &properties,
                                   const UStringVector &strings) {
    FOR_VECTOR(i, strings) {
        const UString &s = strings[i];
        CProperty property;
        const int index = s.Find(L'=');
        if (index < 0)
            property.Name = s;
        else {
            property.Name.SetFrom(s, (unsigned)index);
            property.Value = s.Ptr(index + 1);
        }
        properties.Add(property);
    }
}

static void AddProp_Size(CObjectVector<CProperty> &properties, const char *name,
                         const UInt64 size) {
    UString s;
    s.Add_UInt64(size);
    s.Add_Char('b');
    AddProp_UString(properties, name, s);
}

static void SetOutProperties(CObjectVector<CProperty> &properties, const NCompressDialog::CInfo &di,
                             bool is7z, bool setMethod) {
    if (di.Level != (UInt32)(Int32)-1)
        AddProp_UInt32(properties, "x", (UInt32)di.Level);
    if (setMethod) {
        if (!di.Method.IsEmpty())
            AddProp_UString(properties, is7z ? "0" : "m", di.Method);
        if (di.Dict64 != (UInt64)(Int64)-1) {
            AString name;
            if (is7z)
                name = "0";
            name += (di.OrderMode ? "mem" : "d");
            AddProp_Size(properties, name, di.Dict64);
        }
        /*
        if (di.Dict64_Chain != (UInt64)(Int64)-1)
        {
          AString name;
          if (is7z)
            name = "0";
          name += "dc";
          AddProp_Size(properties, name, di.Dict64_Chain);
        }
        */
        if (di.Order != (UInt32)(Int32)-1) {
            AString name;
            if (is7z)
                name = "0";
            name += (di.OrderMode ? "o" : "fb");
            AddProp_UInt32(properties, name, (UInt32)di.Order);
        }
    }

    if (!di.EncryptionMethod.isEmpty())
        AddProp_UString(properties, "em", di.EncryptionMethod);

    if (di.EncryptHeadersIsAllowed)
        AddProp_bool(properties, "he", di.EncryptHeaders);

    if (di.SolidIsSpecified)
        AddProp_Size(properties, "s", di.SolidBlockSize);

    if (
        // di.MultiThreadIsAllowed &&
        di.NumThreads != (UInt32)(Int32)-1)
        AddProp_UInt32(properties, "mt", di.NumThreads);

    const NCompression::CMemUse &memUse = di.MemUsage;
    if (memUse.IsDefined) {
        const char *kMemUse = "memuse";
        if (memUse.IsPercent) {
            UString s;
            // s += 'p'; // for debug: alternate percent method
            s.Add_UInt64(memUse.Val);
            s.Add_Char('%');
            AddProp_UString(properties, kMemUse, s);
        } else
            AddProp_Size(properties, kMemUse, memUse.Val);
    }

    AddProp_BoolPair(properties, "tm", di.MTime);
    AddProp_BoolPair(properties, "tc", di.CTime);
    AddProp_BoolPair(properties, "ta", di.ATime);

    if (di.TimePrec != (UInt32)(Int32)-1)
        AddProp_UInt32(properties, "tp", di.TimePrec);
}

// Native ShowDialog property orchestration, with a returned property vector
// instead of the not-yet-ported CUpdateOptions encoder owner.
CObjectVector<CProperty> NCompressDialog::GetCompressionProperties(const CInfo &info, bool is7z) {
    CObjectVector<CProperty> properties;
    UStringVector optionStrings;
    SplitOptionsToStrings(info.Options, optionStrings);
    const bool methodOverride = IsThereMethodOverride(is7z, optionStrings);
    SetOutProperties(properties, info, is7z, !methodOverride);
    ParseAndAddPropertires(properties, optionStrings);
    return properties;
}

// UpdateGUI.cpp::g_UpdateMode_Pairs. The Win32 dialog's transport is replaced
// by CreateArchiveDialog; the original action-set mapping remains here.
struct C_UpdateMode_ToAction_Pair {
    NCompressDialog::NUpdateMode::EEnum UpdateMode;
    const NUpdateArchive::CActionSet *ActionSet;
};

static const C_UpdateMode_ToAction_Pair g_UpdateMode_Pairs[] = {
    {NCompressDialog::NUpdateMode::kAdd, &NUpdateArchive::k_ActionSet_Add},
    {NCompressDialog::NUpdateMode::kUpdate, &NUpdateArchive::k_ActionSet_Update},
    {NCompressDialog::NUpdateMode::kFresh, &NUpdateArchive::k_ActionSet_Fresh},
    {NCompressDialog::NUpdateMode::kSync, &NUpdateArchive::k_ActionSet_Sync}};

static int FindUpdateMode(NCompressDialog::NUpdateMode::EEnum mode) {
    for (unsigned i = 0; i < sizeof(g_UpdateMode_Pairs) / sizeof(g_UpdateMode_Pairs[0]); i++)
        if (mode == g_UpdateMode_Pairs[i].UpdateMode)
            return int(i);
    return -1;
}

static int FindActionSet(const NUpdateArchive::CActionSet &actionSet) {
    for (unsigned i = 0; i < Z7_ARRAY_SIZE(g_UpdateMode_Pairs); i++)
        if (actionSet.IsEqualTo(*g_UpdateMode_Pairs[i].ActionSet))
            return (int)i;
    return -1;
}

static bool ParseProp_Time_BoolPair(const CProperty &prop, const char *name, CBoolPair &bp) {
    if (!prop.Name.IsPrefixedBy_Ascii_NoCase(name))
        return false;
    const UString rem = prop.Name.Ptr((unsigned)strlen(name));
    UString val = prop.Value;
    if (!rem.IsEmpty()) {
        if (!val.IsEmpty())
            return true;
        val = rem;
    }
    bool res;
    if (StringToBool(val, res)) {
        bp.Val = res;
        bp.Def = true;
    }
    return true;
}

static void ParseProp(const CProperty &prop, NCompressDialog::CInfo &di) {
    if (ParseProp_Time_BoolPair(prop, "tm", di.MTime))
        return;
    if (ParseProp_Time_BoolPair(prop, "tc", di.CTime))
        return;
    if (ParseProp_Time_BoolPair(prop, "ta", di.ATime))
        return;
}

static void ParseProperties(const CObjectVector<CProperty> &properties,
                            NCompressDialog::CInfo &di) {
    FOR_VECTOR(i, properties) { ParseProp(properties[i], di); }
}

HRESULT ShowDialog(CCodecs *codecs, const CObjectVector<NWildcard::CCensorPath> &censor,
                   CUpdateOptions &options, CUpdateCallbackGUI *callback, QWidget *hwndParent,
                   IQtUpdateGUIServices &services) {
    if (options.Commands.Size() != 1)
        throw QT_TRANSLATE_NOOP("SevenZipDiagnostics", "It must be one command");
    CCompressDialog dialog;
    NCompressDialog::CInfo &di = dialog.Info;
    dialog.ArcFormats = &codecs->Formats;
    {
        CObjectVector<CCodecInfoUser> userCodecs;
        services.GetUserCodecs(*codecs, userCodecs);
        dialog.SetMethods(userCodecs);
    }
    if (options.MethodMode.Type_Defined)
        di.FormatIndex = options.MethodMode.Type.FormatIndex;
    // This is the already-portioned source oneFile/format/name block.
    if (NCompressDialog::PrepareFormatSelection(dialog, *codecs, censor) != S_OK) {
        ShowErrorMessageQt(hwndParent, QtDiagnosticText(QT_TRANSLATE_NOOP("SevenZipDiagnostics",
                                                                          "No Update Engines")));
        return E_FAIL;
    }
    di.ArcPath = options.ArchivePath.GetPathWithoutExt();
    di.PathMode = options.PathMode;
    di.SFXMode = options.SfxMode;
    di.OpenShareForWrite = options.OpenShareForWrite;
    di.DeleteAfterCompressing = options.DeleteAfterCompressing;
    di.SymLinks = options.SymLinks;
    di.HardLinks = options.HardLinks;
    di.AltStreams = options.AltStreams;
    di.NtSecurity = options.NtSecurity;
    if (options.SetArcMTime)
        di.SetArcMTime.SetTrueTrue();
    if (options.PreserveATime)
        di.PreserveATime.SetTrueTrue();
    if (callback->PasswordIsDefined)
        di.Password = QString::fromWCharArray(callback->Password.Ptr());
    NUpdateArchive::CActionSet &actionSet = options.Commands.Front().ActionSet;
    {
        int index = FindActionSet(actionSet);
        if (index < 0)
            return E_NOTIMPL;
        di.UpdateMode = g_UpdateMode_Pairs[(unsigned)index].UpdateMode;
    }
    ParseProperties(options.MethodMode.Properties, di);
    RINOK(services.PresentCompressDialog(dialog, censor, hwndParent))
    options.DeleteAfterCompressing = di.DeleteAfterCompressing;
    options.SymLinks = di.SymLinks;
    options.HardLinks = di.HardLinks;
    options.AltStreams = di.AltStreams;
    options.NtSecurity = di.NtSecurity;
    options.SetArcMTime = di.SetArcMTime.Val;
    if (di.PreserveATime.Def)
        options.PreserveATime = di.PreserveATime.Val;
    options.VolumesSizes = di.VolumeSizes;
    {
        int index = FindUpdateMode(di.UpdateMode);
        if (index < 0)
            return E_FAIL;
        actionSet = *g_UpdateMode_Pairs[index].ActionSet;
    }
    options.PathMode = di.PathMode;
    const CArcInfoEx &archiverInfo = codecs->Formats[di.FormatIndex];
    callback->PasswordIsDefined = !di.Password.isEmpty();
    if (callback->PasswordIsDefined)
        callback->Password = di.Password.toStdWString().c_str();
    options.MethodMode.Properties =
        NCompressDialog::GetCompressionProperties(di, archiverInfo.Is_7z());
    options.OpenShareForWrite = di.OpenShareForWrite;
    if (di.SFXMode)
        options.SfxMode = true;
    options.MethodMode.Type = COpenType();
    options.MethodMode.Type_Defined = true;
    options.MethodMode.Type.FormatIndex = di.FormatIndex;
    options.ArchivePath.VolExtension = archiverInfo.GetMainExt();
    if (di.SFXMode)
#ifdef _WIN32
        options.ArchivePath.BaseExtension = "exe";
#else
        options.ArchivePath.BaseExtension.Empty();
#endif
    else
        options.ArchivePath.BaseExtension = options.ArchivePath.VolExtension;
    options.ArchivePath.ParseFromPath(di.ArcPath, k_ArcNameMode_Smart);
    options.WorkingDir =
        NCompressDialog::GetCompressionWorkingDir(di.ArcPath, services.CompressionSettingsPath());
    return S_OK;
}

const NUpdateArchive::CActionSet *NCompressDialog::FindUpdateActionSet(NUpdateMode::EEnum mode) {
    const int index = FindUpdateMode(mode);
    return index < 0 ? nullptr : g_UpdateMode_Pairs[index].ActionSet;
}
