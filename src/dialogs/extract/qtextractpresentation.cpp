// Qt presentation for the native Extract dialog; see ExtractDialog.cpp provenance.
#include "7zip/UI/GUI/ExtractDialogRes.h"
#include "7zip/UI/GUI/ExtractRes.h"
#include "dialogs/extract/ExtractDialog.h"
#include "localization/qtlangbuttonbox.h"
#include "localization/qtlangcontrols.h"
#include "ui/controls/qtdisclosure.h"
#include "ui/controls/qtsecretedit.h"
#include "ui/dialogs/qtdialoglayout.h"
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QShortcut>
#include <QToolButton>
#include <QVBoxLayout>

void CExtractDialog::BuildPresentation() {
    setObjectName("extractDialog");
    QtBindResourceText(this, IDD_EXTRACT, "Extract");
    auto *buttons = new QtLangButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Help, this);
    buttons->SetAcceptText("ArchiveRedesign", QT_TRANSLATE_NOOP("ArchiveRedesign", "Extract"));
    auto *layout = QtCreateDialogLayout(*this, *buttons);
    layout->setSpacing(12);
    auto *destinationLabel = new QLabel(this);
    destinationLabel->setObjectName("extractDestinationLabel");
    QtBindResourceText(destinationLabel, IDT_EXTRACT_EXTRACT_TO, "E&xtract to:");
    layout->addWidget(destinationLabel);
    auto *pathRow = new QHBoxLayout;
    _path = new QComboBox(this);
    _path->setObjectName("extractDestination");
    QtBindLangText(_path, IDT_EXTRACT_EXTRACT_TO, "Extract to:", true);
    destinationLabel->setBuddy(_path);
    _path->setEditable(true);
    _path->setInsertPolicy(QComboBox::NoInsert);

    pathRow->addWidget(_path, 1);
    auto *browse = new QPushButton("…", this);
    browse->setObjectName("extractBrowseButton");
    QtBindLangText(browse, IDS_EXTRACT_SET_FOLDER, {}, true);
    pathRow->addWidget(browse);
    layout->addLayout(pathRow);

    auto *modeColumn = new QVBoxLayout;
    auto *advancedBody = new QWidget(this);
    advancedBody->setLayout(modeColumn);
    modeColumn->setContentsMargins(0, 0, 0, 0);
    auto *nameRow = new QVBoxLayout;
    _nameEnable = new QCheckBox(this);
    _nameEnable->setObjectName("extractNameEnable");
    QtBindLocalText(_nameEnable, "ArchiveRedesign",
                    QT_TRANSLATE_NOOP("ArchiveRedesign", "Create subfolder"));
    _pathName = new QLineEdit(this);
    _pathName->setObjectName("extractPathName");
    _pathName->setProperty("languagePlaceholder", true);
    QtBindLocalText(_pathName, "ExtractDialog",
                    QT_TRANSLATE_NOOP("ExtractDialog", "Destination folder name"), true);
    nameRow->addWidget(_nameEnable);
    auto *nameField = new QHBoxLayout;
    nameField->addSpacing(24);
    nameField->addWidget(_pathName, 1);
    nameRow->addLayout(nameField);
    layout->addLayout(nameRow);
    auto *pathLabel = new QLabel(this);
    pathLabel->setObjectName("extractPathModeLabel");
    QtBindResourceText(pathLabel, IDT_EXTRACT_PATH_MODE, "Path mode:");
    modeColumn->addWidget(pathLabel);
    _pathMode = new QComboBox(this);
    _pathMode->setObjectName("extractPathMode");
    QtBindLangText(_pathMode, IDT_EXTRACT_PATH_MODE, "Path mode", true);
    pathLabel->setBuddy(_pathMode);

    modeColumn->addWidget(_pathMode);
    _elimDup = new QCheckBox(this);
    _elimDup->setObjectName("extractElimDup");
    QtBindResourceText(_elimDup, IDX_EXTRACT_ELIM_DUP, "Eliminate duplication of root folder");
    modeColumn->addWidget(_elimDup);
    auto *overwriteLabel = new QLabel(this);
    overwriteLabel->setObjectName("extractOverwriteModeLabel");
    QtBindResourceText(overwriteLabel, IDT_EXTRACT_OVERWRITE_MODE, "Overwrite mode:");
    layout->addWidget(overwriteLabel);
    _overwriteMode = new QComboBox(this);
    _overwriteMode->setObjectName("extractOverwriteMode");
    QtBindLangText(_overwriteMode, IDT_EXTRACT_OVERWRITE_MODE, "Overwrite mode", true);
    overwriteLabel->setBuddy(_overwriteMode);
    layout->addWidget(_overwriteMode);

    auto *passwordGroup = new QGroupBox(this);
    passwordGroup->setFlat(true);
    QtBindLangText(passwordGroup, IDG_PASSWORD, "Password", true);
    _passwordEnable = new QCheckBox(this);
    _passwordEnable->setObjectName("extractPasswordEnabled");
    QtBindLocalText(_passwordEnable, "ArchiveRedesign",
                    QT_TRANSLATE_NOOP("ArchiveRedesign", "Use a password"));
    layout->addWidget(_passwordEnable);
    auto *passwordLayout = new QVBoxLayout(passwordGroup);
    auto *password = new QtSecretEdit(passwordGroup);
    _passwordControl = password;
    _passwordControl->setObjectName("extractPassword");
    QtBindLangText(_passwordControl, IDG_PASSWORD, "Password", true);
    passwordLayout->addWidget(_passwordControl);
    _showPassword = new QCheckBox(passwordGroup);
    QtBindResourceText(_showPassword, IDX_PASSWORD_SHOW, "Show Password");
    _showPassword->setObjectName("extractShowPassword");
    password->BindRevealControl(*_showPassword);
    passwordLayout->addWidget(_showPassword);
    layout->addWidget(passwordGroup);
    passwordGroup->hide();
    passwordGroup->setEnabled(false);
    connect(_passwordEnable, &QCheckBox::toggled, this, [passwordGroup](bool enabled) {
        passwordGroup->setVisible(enabled);
        passwordGroup->setEnabled(enabled);
    });
    connect(_passwordControl, &QLineEdit::textChanged, this, [this](const QString &value) {
        if (!value.isEmpty())
            _passwordEnable->setChecked(true);
    });
    _ntSecurity = new QCheckBox(this);
    _ntSecurity->setObjectName("extractNtSecurity");
    QtBindResourceText(_ntSecurity, IDX_EXTRACT_NT_SECUR, "Restore file security");
#ifndef Q_OS_WIN
    _ntSecurity->setEnabled(false);
    _ntSecurity->hide();
#endif
    modeColumn->addWidget(_ntSecurity);
    _advancedSection = new QtDisclosure(advancedBody, this);
    _advancedSection->setObjectName("extractAdvancedSection");
    _advancedSection->button()->setObjectName("extractAdvancedToggle");
    QtBindLocalText(_advancedSection->button(), "ArchiveRedesign",
                    QT_TRANSLATE_NOOP("ArchiveRedesign", "Advanced settings"));
    layout->addWidget(_advancedSection);
    layout->addStretch();
    connect(buttons, &QDialogButtonBox::helpRequested, this, &CExtractDialog::OnHelp);
    auto *help = new QShortcut(QKeySequence(Qt::Key_F1), this);
    connect(help, &QShortcut::activated, this, &CExtractDialog::OnHelp);
    connect(buttons, &QDialogButtonBox::accepted, this, &CExtractDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(browse, &QPushButton::clicked, this, &CExtractDialog::OnButtonSetPath);
    // Keep the source name row in place. Disabling preserves its caption,
    // pending value and geometry while OnOK still obeys the checkbox.
    connect(_nameEnable, &QCheckBox::toggled, _pathName, &QWidget::setEnabled);
    connect(_pathMode, &QComboBox::currentIndexChanged, this,
            &CExtractDialog::UpdateAdvancedSummary);
    connect(_elimDup, &QCheckBox::toggled, this, &CExtractDialog::UpdateAdvancedSummary);
    connect(_ntSecurity, &QCheckBox::toggled, this, &CExtractDialog::UpdateAdvancedSummary);
}

void CExtractDialog::UpdateAdvancedSummary() {
    if (!_advancedSection || _pathMode->currentIndex() < 0)
        return;
    const bool modified = _pathMode->currentData().toInt() != NExtract::NPathMode::kFullPaths ||
                          !_elimDup->isChecked() ||
                          (_ntSecurity->isEnabled() && _ntSecurity->isChecked());
    _advancedSection->setSummary(
        modified ? QCoreApplication::translate("ArchiveRedesign", "Modified") : QString());
}
