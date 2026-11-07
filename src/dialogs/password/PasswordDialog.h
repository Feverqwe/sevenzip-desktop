// Adapted from 7-Zip CPP/7zip/UI/FileManager/PasswordDialog.h (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#pragma once
#include "Common/MyString.h"
#include "archive/password/archivepassword.h"
#include <QDialog>

class QLineEdit;
class QCheckBox;

class CPasswordDialog : public QDialog {
  public:
    explicit CPasswordDialog(QWidget *parent = nullptr);
    NativePassword Password;
    bool ShowPassword = false;
    bool OnInit(const QString &archive = {}, bool opening = false);
    void accept() override;

  private:
    void OnOK();
    void OnButtonClicked();
    void ReadControls();
    QLineEdit *_passwordEdit = nullptr;
    QCheckBox *_showPassword = nullptr;
};
