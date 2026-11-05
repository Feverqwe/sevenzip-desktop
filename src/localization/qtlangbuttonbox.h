#pragma once
#include "localization/qtlangdialogcontrols.h"
#include "ui/dialogs/qtmodalresult.h"
#include <QByteArray>
#include <QDialogButtonBox>

class QtLangButtonBox final : public QDialogButtonBox, private IQtLangDialogControls {
  public:
    QtLangButtonBox(StandardButtons buttons, QWidget *parent, UInt32 acceptID = IDOK);
    void SetAcceptText(const char *context, const char *source);
    void UseCompactHelp();

  protected:
    void changeEvent(QEvent *event) override;

  private:
    QPushButton *Control(UInt32 controlID) const;
    bool HasItem(UInt32 controlID) const override;
    void SetItemText(UInt32 controlID, const wchar_t *text) override;
    void ReloadText();
    void UpdateButtonWidths();
    UInt32 acceptID_;
    QByteArray acceptContext_;
    QByteArray acceptSource_;
    bool compactHelp_ = false;
};
