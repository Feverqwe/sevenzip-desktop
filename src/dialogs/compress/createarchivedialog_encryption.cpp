// Qt presentation for CompressDialog.cpp password/encryption controls (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "dialogs/compress/createarchivedialog.h"
#include "localization/qtlangcontrols.h"
#include "ui/controls/qtsecretedit.h"
#include "ui/dialogs/qtdialoglayout.h"

void CreateArchiveDialog::createEncryptionOptions(QVBoxLayout *form) {
    passwordEnabled_ = new QCheckBox(form->parentWidget());
    passwordEnabled_->setObjectName("archivePasswordEnabled");
    QtBindLocalText(passwordEnabled_, "ArchiveRedesign",
                    QT_TRANSLATE_NOOP("ArchiveRedesign", "Protect with a password"));
    form->addWidget(passwordEnabled_);
    protectionWarning_ = new QtWrappingLabel(form->parentWidget());
    protectionWarning_->setObjectName("archiveProtectionWarning");
    protectionWarning_->setTextFormat(Qt::PlainText);
    protectionWarning_->setWordWrap(true);
    protectionWarning_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    QtBindLocalText(
        protectionWarning_, "ArchiveRedesign",
        QT_TRANSLATE_NOOP("ArchiveRedesign",
                          "This format does not support password protection. Choose another "
                          "format or turn off password protection."));
    form->addWidget(protectionWarning_);
    auto *group = new QGroupBox(this);
    group->setObjectName("archiveEncryptionGroup");
    group->setFlat(true);
    QtBindLocalText(group, "ArchiveRedesign",
                    QT_TRANSLATE_NOOP("ArchiveRedesign", "Protect with a password"), true);
    encryptionForm_ = new QFormLayout(group);
    encryptionForm_->setContentsMargins(24, 0, 0, 4);
    encryptionForm_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    encryptionForm_->setRowWrapPolicy(QFormLayout::WrapLongRows);
    auto *password = new QtSecretEdit(group);
    password_ = password;
    password_->setObjectName("archivePassword");
    encryptionForm_->addRow("Password", password_);
    auto *confirmation = new QtSecretEdit(group);
    passwordConfirmation_ = confirmation;
    passwordConfirmation_->setObjectName("archivePasswordConfirmation");
    encryptionForm_->addRow("Reenter password:", passwordConfirmation_);
    showPassword_ = new QCheckBox("&Show password", group);
    showPassword_->setObjectName("archiveShowPassword");
    showPassword_->setChecked(m_RegistryInfo.ShowPassword);
    password->BindRevealControl(*showPassword_);
    confirmation->BindRevealControl(*showPassword_);
    encryptionForm_->addRow(showPassword_);
    encryption_ = new QComboBox(group);
    encryption_->setObjectName("archiveEncryption");
    encryptionForm_->addRow("Encryption method:", encryption_);
    auto *methodLabel = qobject_cast<QLabel *>(encryptionForm_->labelForField(encryption_));
    methodLabel->setObjectName("archiveEncryptionMethodLabel");
    encryptHeaders_ = new QCheckBox("Encrypt file &names", group);
    encryptHeaders_->setObjectName("archiveEncryptHeaders");
    encryptHeaders_->setChecked(m_RegistryInfo.EncryptHeaders);
    encryptionForm_->addRow(encryptHeaders_);
    form->addWidget(group);
    group->hide();
    connect(passwordEnabled_, &QCheckBox::toggled, this, [this] { updateProtectionControls(); });
    // A source-supplied password or programmatic restoration must never stay
    // active in an invisible section. Unchecking keeps the draft, but excludes it.
    connect(password_, &QLineEdit::textChanged, this, [this](const QString &value) {
        if (!value.isEmpty())
            passwordEnabled_->setChecked(true);
    });
    connect(showPassword_, &QCheckBox::toggled, this, &CreateArchiveDialog::UpdatePasswordControl);
    SetEncryptionMethod();
    UpdatePasswordControl();
}

void CreateArchiveDialog::SetEncryptionMethod() {
    CCompressDialog dialog;
    dialog.Format = format_->currentText();
    dialog.SetEncryptionMethod(m_RegistryInfo);
    encryption_->clear();
    encryption_->addItems(dialog._encryptionMethod);
    encryption_->setCurrentIndex(dialog._encryptionMethod_Index);
    const auto &fi = NCompressDialog::GetFormatInfo(format_->currentText());
    encryptionForm_->setRowVisible(encryption_, encryption_->count() > 1);
    encryptHeaders_->setEnabled(fi.EncryptFileNames_());
    encryptHeaders_->setVisible(fi.EncryptFileNames_());
    updateProtectionControls();
}

bool CreateArchiveDialog::updateProtectionControls() {
    const bool supported = NCompressDialog::GetFormatInfo(format_->currentText()).Encrypt_();
    const bool requested = passwordEnabled_->isChecked();
    const bool unavailable = requested && !supported;
    // Preserve the user's choice across format changes. Only an explicit
    // uncheck may remove protection from a previously protected draft.
    passwordEnabled_->setVisible(supported || requested);
    password_->parentWidget()->setEnabled(supported && requested);
    password_->parentWidget()->setVisible(supported && requested);
    protectionWarning_->setVisible(unavailable);
    if (buttons_)
        buttons_->button(QDialogButtonBox::Ok)
            ->setEnabled(format_->currentIndex() >= 0 && !unavailable);
    return !unavailable;
}

QString CreateArchiveDialog::GetEncryptionMethodSpec() const {
    CCompressDialog dialog;
    dialog.Format = registryFormat_;
    dialog.SetEncryptionMethod(m_RegistryInfo);
    dialog._encryptionMethod_Index = encryption_->currentIndex();
    return dialog.GetEncryptionMethodSpec();
}

void CreateArchiveDialog::UpdatePasswordControl() {
    const bool showPassword = showPassword_->isChecked();
    encryptionForm_->setRowVisible(passwordConfirmation_, !showPassword);
}
