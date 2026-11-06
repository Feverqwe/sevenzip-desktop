// Qt presentation services for the original COptionsDialog functional bodies.
#pragma once
#include "Common/MyString.h"
#include <QHash>
#include <QWidget>

class QComboBox;
class CQtCompressCombo {
    QComboBox *view_ = nullptr;

  public:
    void Attach(QWidget *widget);
    int GetCount() const;
    quint32 GetItemData_of_CurSel() const;
    quint32 GetItemData(int index) const;
    void SetItemText(int index, const UString &text);
    int AddString_SetItemData(const UString &text, qintptr value);
    void ResetContent();
    void SetCurSel(int index);
};

class CQtCompressOptionsPanel : public QWidget {
    QHash<unsigned, QWidget *> controls_;
    bool initialized_ = false;
    void buildControls();
    void changeEvent(QEvent *event) override;

  protected:
#ifndef CBN_SELCHANGE
    static constexpr unsigned CBN_SELCHANGE = 1;
#endif
    QWidget *GetItem(unsigned id) const;
    void CheckButton(unsigned id, bool value);
    bool IsButtonCheckedBool(unsigned id) const;
    void ShowItem_Bool(unsigned id, bool visible);
    void EnableItem(unsigned id, bool enabled);
    void SetItemText(unsigned id, const UString &text);
    virtual bool OnInit() { return true; }
    virtual void OnLanguageChange() {}
    virtual bool OnCommand(unsigned, unsigned, qintptr) { return false; }
    virtual bool OnButtonClicked(unsigned id, QWidget *);
    virtual void OnOK() {}
    virtual void OnHelp() {}

  public:
    explicit CQtCompressOptionsPanel(QWidget *parent = nullptr);
    void initialize();
    void apply() { OnOK(); }
    void showHelp() { OnHelp(); }
};
