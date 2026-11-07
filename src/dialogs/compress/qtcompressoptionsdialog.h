// Adapted COptionsDialog declaration from upstream CompressDialog.h (0766b733).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// The Qt dialog declaration is kept out of the engine DTO header so Core-only
// archive consumers do not acquire a Widgets dependency. Functional bodies
// are owned by CompressOptionsDialog.cpp in this module.
#pragma once
#include "dialogs/compress/CompressDialog.h"
#include "dialogs/compress/qtcompressoptionscontrols.h"
#include "ui/dialogs/qtdialoglayout.h"
#include <QDialog>

class COptionsPanel : public CQtCompressOptionsPanel {
    struct CBoolBox {
        bool IsSupported;
        bool DefaultVal;
        CBoolPair BoolPair;

        unsigned Id;
        unsigned Set_Id;

        void SetIDs(unsigned id, unsigned set_Id) {
            Id = id;
            Set_Id = set_Id;
        }

        CBoolBox() : IsSupported(false), DefaultVal(false) {}
    };

    CCompressDialog *cd;

    CQtCompressCombo m_Prec;

    UInt32 _auto_Prec;
    UInt32 TimePrec;

    void Reset_TimePrec() { TimePrec = (UInt32)(Int32)-1; }
    bool IsSet_TimePrec() const { return TimePrec != (UInt32)(Int32)-1; }

    CBoolBox MTime;
    CBoolBox CTime;
    CBoolBox ATime;
    CBoolBox ZTime;

    UString SecString;
    UString NsString;

    void CheckButton_Bool1(UINT id, const CBool1 &b1);
    void GetButton_Bool1(UINT id, CBool1 &b1);
    void CheckButton_BoolBox(bool supported, const CBoolPair &b2, CBoolBox &bb);
    void GetButton_BoolBox(CBoolBox &bb);

    void Store_TimeBoxes();

    UInt32 GetComboValue(CQtCompressCombo &c, int defMax = 0);
    UInt32 GetPrecSpec() {
        UInt32 prec = GetComboValue(m_Prec, 1);
        if (prec == _auto_Prec)
            prec = (UInt32)(Int32)-1;
        return prec;
    }
    UInt32 GetPrec() { return GetComboValue(m_Prec, 0); }

    // void OnButton_TimeDefault();
    int AddPrec(unsigned prec, bool isDefault, UString *textOnly = nullptr);
    void SetPrec();
    void SetTimeMAC();

    void On_CheckBoxSet_Prec_Clicked();
    void On_CheckBoxSet_Clicked(const CBoolBox &bb);

    virtual bool OnInit() override;
    void OnLanguageChange() override;
    virtual bool OnCommand(unsigned code, unsigned itemID, qintptr lParam) override;
    virtual bool OnButtonClicked(unsigned buttonID, QWidget *buttonHWND) override;
    virtual void OnOK() override;

  public:
    COptionsPanel(CCompressDialog *cdLoc Z7_lifetimebound, QWidget *parent = nullptr)
        : CQtCompressOptionsPanel(parent), cd(cdLoc)
    // , TimePrec(0)
    {
        Reset_TimePrec();
    }
    void refreshContext();
};

// Standalone shell; the same source-backed panel also lives in the Expert draft.
class COptionsDialog : public QDialog {
    COptionsPanel *panel_;
    bool initialized_ = false;
    bool focusControl(QWidget *start, bool next, Qt::FocusReason reason);
    bool focusNextPrevChild(bool next) override;

  public:
    explicit COptionsDialog(CCompressDialog *source Z7_lifetimebound);
    int Create(QWidget *parent = nullptr);
    void setVisible(bool visible) override;
    void accept() override;
};
