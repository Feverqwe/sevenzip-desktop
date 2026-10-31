// Adapted from 7-Zip CPP/7zip/UI/FileManager/ComboDialog.h (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#pragma once
#include "Common/MyString.h"
#include <QDialog>
#include <QStringList>

class QComboBox;

class CComboDialog : public QDialog {
    Q_OBJECT
  public:
    CComboDialog(const QString &title, const QString &prompt, const QString &initial,
                 QWidget *parent = nullptr, const QStringList &strings = {});
    UString Value;
    void BindLanguage(UInt32 titleID, UInt32 promptID);
    void accept() override;

  private:
    QComboBox *_comboBox = nullptr;
    bool OnInit(const QString &title, const QString &prompt, const QStringList &strings);
    void OnOK();
};
