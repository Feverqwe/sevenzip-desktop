// Adapted from 7-Zip CPP/7zip/UI/FileManager/CopyDialog.h (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#pragma once
#include "Common/MyString.h"

#include <QDialog>
#include <QStringList>

class QComboBox;

const int kCopyDialog_NumInfoLines = 11;

class CCopyDialog : public QDialog {
    Q_OBJECT

  public:
    CCopyDialog(const QString &title, const QString &label, const QString &path,
                const QString &info, QWidget *parent = nullptr);
    CCopyDialog(bool move, const QString &initialPath, const QStringList &sources,
                const QStringList &history, QWidget *parent = nullptr,
                const QString &info = QString());
    UString Title;
    UString Static;
    UString Value;
    UString Info;
    UStringVector Strings;

    QString destination() const;
    void BindLanguage(UInt32 titleID, UInt32 promptID, const QString &titleSuffix = {});
    void accept() override;

  private:
    QComboBox *_path = nullptr;
    bool OnInit();
    void OnButtonSetPath();
    void OnOK();
    bool ReadPath(UString &path);
};
