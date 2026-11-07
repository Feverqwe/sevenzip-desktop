// Adapted from 7-Zip CPP/7zip/UI/FileManager/ComboDialog.cpp (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Qt layouts replace Win32 OnSize/control attachment; original portable state
// converts only at widget boundaries. OnOK keeps the raw value.
#include "dialogs/combo/ComboDialog.h"
#include "7zip/UI/FileManager/resource.h"
#include "Windows/ErrorMsg.h"
#include "localization/qtlangbuttonbox.h"
#include "localization/qtlangcontrols.h"
#include "settings/ViewSettings.h"
#include "ui/dialogs/qtdialoglayout.h"
#include "ui/dialogs/qtdialogscreenservices.h"
#include "ui/dialogs/qtdialogunits.h"
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QVBoxLayout>

CComboDialog::CComboDialog(const QString &title, const QString &prompt, const QString &initial,
                           QWidget *parent, const QStringList &strings)
    : QDialog(parent), Value(ToUString(initial)) {
    OnInit(title, prompt, strings);
}

bool CComboDialog::OnInit(const QString &title, const QString &prompt, const QStringList &strings) {
    QtUseContextDialogModality(*this);
    setWindowFlag(Qt::WindowMinMaxButtonsHint);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setObjectName("comboDialog");
    setWindowTitle(title);
    auto *buttons = new QtLangButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->setObjectName("comboButtons");
    auto *layout = QtCreateDialogLayout(*this, *buttons);
    auto *staticControl = new QtWrappingLabel(prompt, this);
    staticControl->setObjectName("comboPrompt");
    staticControl->setTextFormat(Qt::PlainText);
    staticControl->setWordWrap(true);
    layout->addWidget(staticControl);
    _comboBox = new QComboBox(this);
    _comboBox->setObjectName("comboValue");
    _comboBox->setAccessibleName(prompt);
    _comboBox->setEditable(true);
    _comboBox->setInsertPolicy(QComboBox::NoInsert);
    _comboBox->setMinimumContentsLength(12);
    _comboBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    staticControl->setBuddy(_comboBox);
    // Native AddString leaves the edit text unchanged. Qt's first insertion
    // selects item 0, so restore the initial Value after populating choices.
    _comboBox->setEditText(ToQString(Value));
    _comboBox->addItems(strings);
    _comboBox->setCurrentIndex(-1);
    _comboBox->setEditText(ToQString(Value));
    layout->addWidget(_comboBox);
    layout->addStretch(1);
    connect(buttons, &QDialogButtonBox::accepted, this, &CComboDialog::OnOK);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    _comboBox->setFocus();
    _comboBox->lineEdit()->selectAll();
    resize(QSize(QtDialogUnits(font()).pixels(QSize(256, 0)).width(), minimumSizeHint().height())
               .expandedTo(minimumSizeHint()));
    QtNormalizeDialogSize(*this);
    return true;
}

void CComboDialog::OnOK() {
    const QString text = _comboBox->currentText();
    // Native text controls cannot return an embedded terminator. Reject it at
    // the Qt boundary instead of passing a silently shortened value to callers.
    if (text.contains(QChar::Null)) {
        QMessageBox::critical(this, windowTitle(),
                              ToQString(NWindows::NError::MyFormatMessage(E_INVALIDARG)));
        _comboBox->setFocus();
        return;
    }
    Value = ToUString(text);
    QDialog::accept();
}

void CComboDialog::accept() { OnOK(); }

void CComboDialog::BindLanguage(UInt32 titleID, UInt32 promptID) {
    // A caller-built item title has no resource ID. Lang ID 0 names 7-Zip,
    // so binding it would replace the source "item : Comment" title.
    if (titleID)
        QtBindLangText(this, titleID, windowTitle());
    auto *prompt = findChild<QLabel *>("comboPrompt");
    QtBindLangText(prompt, promptID, prompt->text());
    QtBindLangText(_comboBox, promptID, _comboBox->accessibleName(), true);
    auto *buttons = static_cast<QtLangButtonBox *>(findChild<QDialogButtonBox *>("comboButtons"));
    // Callers identify the source operation with resource IDs; translated
    // window captions and caller-built comment titles are not command keys.
    if (titleID == IDS_CREATE_FOLDER)
        buttons->SetAcceptText("DialogRedesign", QT_TRANSLATE_NOOP("DialogRedesign", "Create"));
}
