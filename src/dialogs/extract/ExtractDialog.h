// Adapted from 7-Zip CPP/7zip/UI/GUI/ExtractDialog.h (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#pragma once
#include "archive/operations/ZipRegistry.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QLineEdit>
#include <QStringList>

class QtDisclosure;

class CExtractDialog : public QDialog {
    Q_OBJECT
    QComboBox *_path = nullptr;
    QLineEdit *_pathName = nullptr;
    QCheckBox *_nameEnable = nullptr;
    QLineEdit *_passwordControl = nullptr;
    QComboBox *_pathMode = nullptr;
    QComboBox *_overwriteMode = nullptr;
    QCheckBox *_showPassword = nullptr;
    QCheckBox *_elimDup = nullptr;
    QCheckBox *_ntSecurity = nullptr;
    QCheckBox *_passwordEnable = nullptr;
    QtDisclosure *_advancedSection = nullptr;
    QSize _initialPresentationSize;
    bool _initialPresentation = true;
    void BuildPresentation();
    void UpdateAdvancedSummary();
    NExtract::CInfo _info;
    void OnInit(const QStringList &history);
    void OnOK();
    UString DraftDestination() const;
    void OnButtonSetPath();
    void changeEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    static QString settingsPath();
    bool IsShowPasswordChecked() const { return _showPassword->isChecked(); }
    void CheckButton_TwoBools(QCheckBox *button, const CBoolPair &b1, const CBoolPair &b2);
    void GetButton_Bools(QCheckBox *button, CBoolPair &b1, CBoolPair &b2);

  public:
    CExtractDialog(const QString &initialPath, const QStringList &history,
                   QWidget *parent = nullptr);
    QString DirPath;
    UString ArcPath;
    QString Password;
    CBoolPair ElimDup;
    CBoolPair NtSecurity;
    bool PathMode_Force = false;
    bool OverwriteMode_Force = false;
    NExtract::NPathMode::EEnum PathMode = NExtract::NPathMode::kFullPaths;
    NExtract::NOverwriteMode::EEnum OverwriteMode = NExtract::NOverwriteMode::kAsk;
    QString destination() const;
    QString password() const {
        return _passwordEnable->isChecked() ? _passwordControl->text() : QString();
    }
    void OnHelp();
    void SetInitialOptions(NExtract::NPathMode::EEnum pathMode,
                           NExtract::NOverwriteMode::EEnum overwriteMode, const QString &password);
    void SetMetadataOptions(const CBoolPair &elimDup, const CBoolPair &ntSecurity);
  public slots:
    void accept() override { OnOK(); }
};
