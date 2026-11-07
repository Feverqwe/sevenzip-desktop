// Adapted from 7-Zip CPP/7zip/UI/FileManager/PasswordDialog.cpp/.rc (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Qt controls replace Win32 attachment/messages; functional handler flow stays.
#include "dialogs/password/PasswordDialog.h"
#include "7zip/UI/FileManager/PasswordDialogRes.h"
#include "localization/qtlangbuttonbox.h"
#include "localization/qtlangcontrols.h"
#include "ui/controls/qtsecretedit.h"
#include "ui/controls/qtsourcesummary.h"
#include "ui/dialogs/qtdialoglayout.h"
#include "ui/lifecycle/qttaskpresentation.h"
#include "ui/theme/qtcommandicons.h"
#include <QCheckBox>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

CPasswordDialog::CPasswordDialog(QWidget *parent) : QDialog(parent) {
    QtTaskPresentation::Watch(*this, QtOperationPhase::Question);
}

void CPasswordDialog::ReadControls() {
    Password = _passwordEdit->text();
    ShowPassword = _showPassword->isChecked();
}

bool CPasswordDialog::OnInit(const QString &archive, bool opening) {
    QtUseContextDialogModality(*this);
    setObjectName("passwordDialog");
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    QtBindResourceText(this, IDD_PASSWORD, "Enter password");
    auto *buttons = new QtLangButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->setObjectName("passwordButtons");
    buttons->SetAcceptText("DialogRedesign", opening
                                                 ? QT_TRANSLATE_NOOP("DialogRedesign", "Open")
                                                 : QT_TRANSLATE_NOOP("DialogRedesign", "Continue"));
    auto *layout = QtCreateDialogLayout(*this, *buttons, QtDialogContent::Scrollable);
    if (!archive.isEmpty()) {
        const QFileInfo source(archive);
        auto *summary = new QtSourceSummary(QtCommandSymbol(QtCommandIcon::Archive),
                                            source.fileName(), archive, this);
        summary->setObjectName("passwordArchiveSource");
        if (source.path() != ".")
            summary->SetDescription(source.path());
        layout->addWidget(summary);
    }
    auto *label = new QtWrappingLabel(this);
    label->setWordWrap(true);
    QtBindResourceText(label, IDT_PASSWORD_ENTER, "&Enter password:");
    label->setObjectName("passwordPrompt");
    layout->addWidget(label);
    auto *password = new QtSecretEdit(this);
    _passwordEdit = password;
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
    password->BindRevealControl(*_showPassword);
    _passwordEdit->setText(QString::fromWCharArray(Password.Ptr(), Password.Len()));
    connect(_showPassword, &QCheckBox::clicked, this, &CPasswordDialog::OnButtonClicked);
    connect(buttons, &QDialogButtonBox::accepted, this, &CPasswordDialog::OnOK);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    resize(QSize(360, sizeHint().height()).expandedTo(minimumSizeHint()));
    if (!archive.isEmpty())
        QtFitScrollableDialog(*this);
    _passwordEdit->setFocus();
    return true;
}

void CPasswordDialog::OnButtonClicked() { ReadControls(); }

void CPasswordDialog::OnOK() {
    ReadControls();
    QDialog::accept();
}

void CPasswordDialog::accept() { OnOK(); }
