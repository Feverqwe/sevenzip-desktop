// Adapted COptionsDialog from 7-Zip CPP/7zip/UI/GUI/CompressDialog.cpp.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "7zip/PropID.h"
#include "7zip/UI/GUI/CompressDialogRes.h"
#include "Common/Defs.h"
#include "dialogs/compress/qtcompressoptionsdialog.h"
#include "filemanager/HelpUtils.h"
#include "filemanager/PropertyName.h"
#include "localization/LangUtils.h"
#include <QLabel>

using namespace NCompressDialog;

void COptionsPanel::CheckButton_Bool1(UINT id, const CBool1 &b1) { CheckButton(id, b1.Val); }

void COptionsPanel::GetButton_Bool1(UINT id, CBool1 &b1) { b1.Val = IsButtonCheckedBool(id); }

void COptionsPanel::CheckButton_BoolBox(bool supported, const CBoolPair &b2, CBoolBox &bb) {
    const bool isSet = b2.Def;
    const bool val = isSet ? b2.Val : bb.DefaultVal;

    bb.IsSupported = supported;

    CheckButton(bb.Set_Id, isSet);
    ShowItem_Bool(bb.Set_Id, supported);
    CheckButton(bb.Id, val);
    EnableItem(bb.Id, isSet);
    ShowItem_Bool(bb.Id, supported);
}

void COptionsPanel::GetButton_BoolBox(CBoolBox &bb) {
    // we save value for invisible buttons too
    bb.BoolPair.Val = IsButtonCheckedBool(bb.Id);
    bb.BoolPair.Def = IsButtonCheckedBool(bb.Set_Id);
}

void COptionsPanel::Store_TimeBoxes() {
    TimePrec = GetPrecSpec();
    GetButton_BoolBox(MTime);
    GetButton_BoolBox(CTime);
    GetButton_BoolBox(ATime);
    GetButton_BoolBox(ZTime);
}

UInt32 COptionsPanel::GetComboValue(CQtCompressCombo &c, int defMax) {
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

int COptionsPanel::AddPrec(unsigned prec, bool isDefault, UString *textOnly) {
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

void COptionsPanel::OnLanguageChange() {
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

void COptionsPanel::SetPrec() {
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
            if (const char *methodName = GetMethodName(methodID))
                s += methodName;
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

void COptionsPanel::SetTimeMAC() {
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

void COptionsPanel::On_CheckBoxSet_Prec_Clicked() {
    const bool isSet = IsButtonCheckedBool(IDX_COMPRESS_PREC_SET);
    if (!isSet) {
        // We save current MAC boxes to memory before SetPrec()
        Store_TimeBoxes();
        Reset_TimePrec();
        SetPrec();
    }
    EnableItem(IDC_COMPRESS_TIME_PREC, isSet);
}

void COptionsPanel::On_CheckBoxSet_Clicked(const CBoolBox &bb) {
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

bool COptionsPanel::OnInit() {
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

    return CQtCompressOptionsPanel::OnInit();
}

bool COptionsPanel::OnCommand(unsigned code, unsigned itemID, qintptr lParam) {
    if (code == CBN_SELCHANGE) {
        switch (itemID) {
        case IDC_COMPRESS_TIME_PREC: {
            Store_TimeBoxes();
            SetTimeMAC(); // for zip/tar
            return true;
        }
        }
    }
    return CQtCompressOptionsPanel::OnCommand(code, itemID, lParam);
}

bool COptionsPanel::OnButtonClicked(unsigned buttonID, QWidget *buttonHWND) {
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
    return CQtCompressOptionsPanel::OnButtonClicked(buttonID, buttonHWND);
}

void COptionsPanel::OnOK() {
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

    CQtCompressOptionsPanel::OnOK();
}

#define kHelpTopic_Options "fm/plugins/7-zip/add.htm#options"
void COptionsPanel::OnHelp() { ShowHelpWindow(kHelpTopic_Options, this); }

void COptionsPanel::refreshContext() {
    // Publish current controls into the local profile before source capability
    // recalculation reads it. Hidden overrides retain the source contract.
    apply();
    SetPrec();
}
