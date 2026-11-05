// Adapted from 7-Zip CPP/7zip/UI/FileManager/PasswordDialog.cpp/.rc (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Qt controls replace Win32 attachment/messages; functional handler flow stays.
#include "dialogs/password/PasswordDialog.h"
#include "7zip/UI/FileManager/PasswordDialogRes.h"
#include "localization/qtlangbuttonbox.h"
#include "localization/qtlangcontrols.h"
#include "ui/dialogs/qtdialoglayout.h"
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

CPasswordDialog::CPasswordDialog(QWidget *parent) : QDialog(parent) {}

void CPasswordDialog::ReadControls() {
    Password = _passwordEdit->text();
    ShowPassword = _showPassword->isChecked();
}

void CPasswordDialog::SetTextSpec() {
    _passwordEdit->setEchoMode(ShowPassword ? QLineEdit::Normal : QLineEdit::Password);
    _passwordEdit->setText(QString::fromWCharArray(Password.Ptr(), Password.Len()));
}

bool CPasswordDialog::OnInit() {
    QtUseContextDialogModality(*this);
    setObjectName("passwordDialog");
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    QtBindResourceText(this, IDD_PASSWORD, "Enter password");
    auto *buttons = new QtLangButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->setObjectName("passwordButtons");
    buttons->SetAcceptText("DialogRedesign", QT_TRANSLATE_NOOP("DialogRedesign", "Continue"));
    auto *layout = QtCreateDialogLayout(*this, *buttons, QtDialogContent::Scrollable);
    auto *label = new QtWrappingLabel(this);
    label->setWordWrap(true);
    QtBindResourceText(label, IDT_PASSWORD_ENTER, "&Enter password:");
    label->setObjectName("passwordPrompt");
    layout->addWidget(label);
    _passwordEdit = new QLineEdit(this);
    _passwordEdit->setObjectName("passwordEdit");
    QtBindLangText(_passwordEdit, IDT_PASSWORD_ENTER, "Enter password:", true);
    label->setBuddy(_passwordEdit);
    layout->addWidget(_passwordEdit);
    layout->addSpacing(4);
    _showPassword = new QCheckBox(this);
    QtBindResourceText(_showPassword, IDX_PASSWORD_SHOW, "&Show password");
    _showPassword->setObjectName("passwordShow");
    layout->addWidget(_showPassword);
    _showPassword->setChecked(ShowPassword);
    SetTextSpec();
    connect(_showPassword, &QCheckBox::clicked, this, &CPasswordDialog::OnButtonClicked);
    connect(buttons, &QDialogButtonBox::accepted, this, &CPasswordDialog::OnOK);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    resize(QSize(360, sizeHint().height()).expandedTo(minimumSizeHint()));
    _passwordEdit->setFocus();
    return true;
}

void CPasswordDialog::OnButtonClicked() {
    ReadControls();
    SetTextSpec();
}

void CPasswordDialog::OnOK() {
    ReadControls();
    QDialog::accept();
}

void CPasswordDialog::accept() { OnOK(); }
