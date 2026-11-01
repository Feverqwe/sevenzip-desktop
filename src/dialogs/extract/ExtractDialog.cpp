// Adapted from 7-Zip CPP/7zip/UI/GUI/ExtractDialog.cpp (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "dialogs/extract/ExtractDialog.h"
#include "7zip/UI/GUI/ExtractDialogRes.h"
#include "7zip/UI/GUI/ExtractRes.h"
#include "Windows/FileName.h"
#include "dialogs/browse/BrowseDialog.h"
#include "filemanager/HelpUtils.h"
#include "fileops/patterns/Wildcard.h"
#include "localization/qtlangbuttonbox.h"
#include "localization/qtlangcontrols.h"
#include "settings/ViewSettings.h"
#include "ui/controls/qtdisclosure.h"
#include "ui/dialogs/qtdialoglayout.h"
#include <QDir>
#include <QEvent>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QShortcut>
#include <QShowEvent>
#include <QStandardPaths>
#include <QToolButton>
#include <QVBoxLayout>

#define kHelpTopic "fm/plugins/7-zip/extract.htm"
void CExtractDialog::OnHelp() { ShowHelpWindow(kHelpTopic, this); }
static const int kPathModeButtonsVals[] = {
    NExtract::NPathMode::kFullPaths, NExtract::NPathMode::kNoPaths, NExtract::NPathMode::kAbsPaths};
static const int kOverwriteButtonsVals[] = {
    NExtract::NOverwriteMode::kAsk, NExtract::NOverwriteMode::kOverwrite,
    NExtract::NOverwriteMode::kSkip, NExtract::NOverwriteMode::kRename,
    NExtract::NOverwriteMode::kRenameExisting};
static const UInt32 kPathMode_IDs[] = {IDS_EXTRACT_PATHS_FULL, IDS_EXTRACT_PATHS_NO,
                                       IDS_EXTRACT_PATHS_ABS};
static const UInt32 kOverwriteMode_IDs[] = {
    IDS_EXTRACT_OVERWRITE_ASK, IDS_EXTRACT_OVERWRITE_WITHOUT_PROMPT,
    IDS_EXTRACT_OVERWRITE_SKIP_EXISTING, IDS_EXTRACT_OVERWRITE_RENAME,
    IDS_EXTRACT_OVERWRITE_RENAME_EXISTING};
static const unsigned kHistorySize = 16;

static void AddComboItems(QComboBox &combo, const UInt32 *langIDs, unsigned numItems,
                          const int *values, int curVal) {
    unsigned curSel = 0;
    for (unsigned i = 0; i < numItems; i++) {
        QString s = QtLangText(langIDs[i]);
        s.remove('&');
        combo.addItem(s, i);
        if (values[i] == curVal)
            curSel = i;
    }
    combo.setCurrentIndex(int(curSel));
}

void AddUniqueString(CObjectVector<QString> &list, const QString &s) {
    const UString source(s.toStdWString().c_str());
    FOR_VECTOR(i, list)
    if (source.IsEqualTo_NoCase(UString(list[i].toStdWString().c_str())))
        return;
    list.Add(s);
}

CExtractDialog::CExtractDialog(const QString &initialPath, const QStringList &history,
                               QWidget *parent)
    : QDialog(parent), DirPath(initialPath) {
    QtUseContextDialogModality(*this);
    ElimDup.Val = true;
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
    _passwordControl = new QLineEdit(passwordGroup);
    _passwordControl->setObjectName("extractPassword");
    QtBindLangText(_passwordControl, IDG_PASSWORD, "Password", true);
    _passwordControl->setEchoMode(QLineEdit::Password);
    passwordLayout->addWidget(_passwordControl);
    _showPassword = new QCheckBox(passwordGroup);
    QtBindResourceText(_showPassword, IDX_PASSWORD_SHOW, "Show Password");
    _showPassword->setObjectName("extractShowPassword");
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
    connect(_showPassword, &QCheckBox::toggled, this, &CExtractDialog::UpdatePasswordControl);
    // Keep the source name row in place. Disabling preserves its caption,
    // pending value and geometry while OnOK still obeys the checkbox.
    connect(_nameEnable, &QCheckBox::toggled, _pathName, &QWidget::setEnabled);
    connect(_pathMode, &QComboBox::currentIndexChanged, this,
            &CExtractDialog::UpdateAdvancedSummary);
    connect(_elimDup, &QCheckBox::toggled, this, &CExtractDialog::UpdateAdvancedSummary);
    connect(_ntSecurity, &QCheckBox::toggled, this, &CExtractDialog::UpdateAdvancedSummary);
    OnInit(history);
    UpdateAdvancedSummary();
    resize(QSize(520, sizeHint().height()).expandedTo(minimumSizeHint()));
    _path->setFocus();
}

// Source bool-pair precedence and changed-value persistence; Qt substitutes
// only the Win32 CheckButton/IsButtonCheckedBool control services.
static bool GetBoolsVal(const CBoolPair &b1, const CBoolPair &b2) {
    if (b1.Def)
        return b1.Val;
    if (b2.Def)
        return b2.Val;
    return b1.Val;
}

void CExtractDialog::CheckButton_TwoBools(QCheckBox *button, const CBoolPair &b1,
                                          const CBoolPair &b2) {
    button->setChecked(GetBoolsVal(b1, b2));
}

void CExtractDialog::GetButton_Bools(QCheckBox *button, CBoolPair &b1, CBoolPair &b2) {
    const bool val = button->isChecked();
    const bool oldVal = GetBoolsVal(b1, b2);
    if (val != oldVal)
        b1.Def = b2.Def = true;
    b1.Val = b2.Val = val;
}

void CExtractDialog::SetMetadataOptions(const CBoolPair &elimDup, const CBoolPair &ntSecurity) {
    ElimDup = elimDup;
    NtSecurity = ntSecurity;
    CheckButton_TwoBools(_elimDup, ElimDup, _info.ElimDup);
    CheckButton_TwoBools(_ntSecurity, NtSecurity, _info.NtSecurity);
}

void CExtractDialog::SetInitialOptions(NExtract::NPathMode::EEnum pathMode,
                                       NExtract::NOverwriteMode::EEnum overwriteMode,
                                       const QString &password) {
    if (!PathMode_Force && _info.PathMode_Force)
        pathMode = _info.PathMode;
    if (!OverwriteMode_Force && _info.OverwriteMode_Force)
        overwriteMode = _info.OverwriteMode;
    PathMode = pathMode;
    OverwriteMode = overwriteMode;
    for (int index = 0; index < _pathMode->count(); ++index)
        if (kPathModeButtonsVals[_pathMode->itemData(index).toUInt()] ==
            (pathMode == NExtract::NPathMode::kCurPaths ? NExtract::NPathMode::kFullPaths
                                                        : pathMode))
            _pathMode->setCurrentIndex(index);
    for (int index = 0; index < _overwriteMode->count(); ++index)
        if (kOverwriteButtonsVals[_overwriteMode->itemData(index).toUInt()] == overwriteMode)
            _overwriteMode->setCurrentIndex(index);
    _passwordControl->setText(password);
}

QString CExtractDialog::settingsPath() {
    const QString overridden = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
    if (!overridden.isEmpty())
        return overridden;
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(directory);
    return QDir(directory).filePath("toolbar.ini");
}

void CExtractDialog::OnInit(const QStringList &history) {
    _info.Load(settingsPath());
    if (!history.isEmpty()) {
        _info.Paths.Clear();
        for (const QString &s : history)
            _info.Paths.Add(s);
    }
    if (_info.PathMode == NExtract::NPathMode::kCurPaths)
        _info.PathMode = NExtract::NPathMode::kFullPaths;
    if (!PathMode_Force && _info.PathMode_Force)
        PathMode = _info.PathMode;
    if (!OverwriteMode_Force && _info.OverwriteMode_Force)
        OverwriteMode = _info.OverwriteMode;
    _showPassword->setChecked(_info.ShowPassword.Val);
    CheckButton_TwoBools(_elimDup, ElimDup, _info.ElimDup);
    CheckButton_TwoBools(_ntSecurity, NtSecurity, _info.NtSecurity);
    UpdatePasswordControl();
    QString pathPrefix = DirPath;
    if (_info.SplitDest.Val) {
        _nameEnable->setChecked(true);
        QString pathName;
        SplitPathToParts_Smart(DirPath, pathPrefix, pathName);
        if (pathPrefix.isEmpty())
            pathPrefix = pathName;
        else
            _pathName->setText(pathName);
    } else
        _pathName->setEnabled(false);
    _path->setEditText(pathPrefix);
    for (unsigned i = 0; i < _info.Paths.Size() && i < kHistorySize; i++)
        _path->addItem(_info.Paths[i]);
    // Qt addItem selects the first item in an editable empty combo. Restore typed text.
    _path->setCurrentIndex(-1);
    _path->setEditText(pathPrefix);
    AddComboItems(*_pathMode, kPathMode_IDs, Z7_ARRAY_SIZE(kPathMode_IDs), kPathModeButtonsVals,
                  PathMode);
    AddComboItems(*_overwriteMode, kOverwriteMode_IDs, Z7_ARRAY_SIZE(kOverwriteMode_IDs),
                  kOverwriteButtonsVals, OverwriteMode);
}

void CExtractDialog::showEvent(QShowEvent *event) {
    // Original OnInit appends ArcPath after its caller configures the dialog.
    setProperty("languageSuffix",
                ArcPath.IsEmpty() ? QString() : " : " + QString::fromWCharArray(ArcPath.Ptr()));
    QtReloadLangTexts(*this);
    QDialog::showEvent(event);
    UpdateAdvancedSummary();
}

void CExtractDialog::changeEvent(QEvent *event) {
    if (event->type() == QEvent::LanguageChange && _pathMode && _overwriteMode) {
        for (auto pair :
             {qMakePair(_pathMode, kPathMode_IDs), qMakePair(_overwriteMode, kOverwriteMode_IDs)})
            for (int index = 0; index < pair.first->count(); ++index)
                pair.first->setItemText(
                    index,
                    QtLangText(pair.second[pair.first->itemData(index).toUInt()]).remove('&'));
    }
    QDialog::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
        UpdateAdvancedSummary();
}

void CExtractDialog::UpdateAdvancedSummary() {
    if (!_advancedSection || _pathMode->currentIndex() < 0)
        return;
    const bool modified =
        kPathModeButtonsVals[_pathMode->currentIndex()] != NExtract::NPathMode::kFullPaths ||
        !_elimDup->isChecked() || (_ntSecurity->isEnabled() && _ntSecurity->isChecked());
    _advancedSection->setSummary(
        modified ? QCoreApplication::translate("ArchiveRedesign", "Modified") : QString());
}

void CExtractDialog::UpdatePasswordControl() {
    _passwordControl->setEchoMode(IsShowPasswordChecked() ? QLineEdit::Normal
                                                          : QLineEdit::Password);
}
void CExtractDialog::OnButtonSetPath() {
    UString path;
    if (MyBrowseForFolder(this, ToUString(QtLangText(IDS_EXTRACT_SET_FOLDER)),
                          ToUString(_path->currentText()), path))
        _path->setEditText(ToQString(path));
}

void CExtractDialog::OnOK() {
    const int pathMode2 = kPathModeButtonsVals[_pathMode->currentData().toUInt()];
    if (PathMode != NExtract::NPathMode::kCurPaths || pathMode2 != NExtract::NPathMode::kFullPaths)
        PathMode = (NExtract::NPathMode::EEnum)pathMode2;
    OverwriteMode = (NExtract::NOverwriteMode::EEnum)
        kOverwriteButtonsVals[_overwriteMode->currentData().toUInt()];
    Password = password();
    GetButton_Bools(_elimDup, ElimDup, _info.ElimDup);
    GetButton_Bools(_ntSecurity, NtSecurity, _info.NtSecurity);
    const bool showPassword = IsShowPasswordChecked();
    if (showPassword != _info.ShowPassword.Val) {
        _info.ShowPassword.Def = true;
        _info.ShowPassword.Val = showPassword;
    }
    if (_info.PathMode != pathMode2) {
        _info.PathMode_Force = true;
        _info.PathMode = (NExtract::NPathMode::EEnum)pathMode2;
    }
    if (!OverwriteMode_Force && _info.OverwriteMode != OverwriteMode)
        _info.OverwriteMode_Force = true;
    _info.OverwriteMode = OverwriteMode;
    int currentItem = _path->currentIndex();
    // A Qt editable combo may retain its index after setEditText: Win32 GetCurSel is CB_ERR.
    if (currentItem >= 0 && _path->currentText() != _path->itemText(currentItem))
        currentItem = -1;
    if (currentItem == -1 && _path->count() >= int(kHistorySize))
        currentItem = _path->count() - 1;
    UString s = DraftDestination();
    NWindows::NFile::NName::NormalizeDirPathPrefix(s);
    const bool splitDest = _nameEnable->isChecked();
    if (splitDest != _info.SplitDest.Val) {
        _info.SplitDest.Def = true;
        _info.SplitDest.Val = splitDest;
    }
    DirPath = QString::fromWCharArray(s.Ptr());
    _info.Paths.Clear();
    AddUniqueString(_info.Paths, DirPath);
    for (int i = 0; i < _path->count(); i++)
        if (i != currentItem) {
            UString sTemp(_path->itemText(i).toStdWString().c_str());
            sTemp.Trim();
            AddUniqueString(_info.Paths, QString::fromWCharArray(sTemp.Ptr()));
        }
    _info.Save(settingsPath());
    QDialog::accept();
}

QString CExtractDialog::destination() const {
    if (result() == QDialog::Accepted)
        return DirPath;
    const UString path = DraftDestination();
    return QString::fromWCharArray(path.Ptr());
}

UString CExtractDialog::DraftDestination() const {
    UString s(_path->currentText().toStdWString().c_str());
    s.Trim();
    if (_nameEnable->isChecked()) {
        NWindows::NFile::NName::NormalizeDirPathPrefix(s);
        UString pathName(_pathName->text().toStdWString().c_str());
        pathName.Trim();
        s += pathName;
    }
    return s;
}

ExtractionOptions CExtractDialog::options() const {
    ExtractionOptions result;
    result.ElimDup = ElimDup;
    result.ElimDup.Val = _elimDup->isEnabled() && _elimDup->isChecked();
    result.NtSecurity = NtSecurity;
    result.NtSecurity.Val = _ntSecurity->isEnabled() && _ntSecurity->isChecked();
    if (kPathModeButtonsVals[_pathMode->currentData().toUInt()] == NExtract::NPathMode::kNoPaths)
        result.pathMode = ExtractionOptions::PathMode::NoPaths;
    else if (kPathModeButtonsVals[_pathMode->currentData().toUInt()] ==
             NExtract::NPathMode::kAbsPaths)
        result.pathMode = ExtractionOptions::PathMode::AbsolutePaths;
    switch (kOverwriteButtonsVals[_overwriteMode->currentData().toUInt()]) {
    case NExtract::NOverwriteMode::kAsk:
        result.overwriteMode = ExtractionOptions::OverwriteMode::Ask;
        break;
    case NExtract::NOverwriteMode::kOverwrite:
        result.overwriteMode = ExtractionOptions::OverwriteMode::OverwriteExisting;
        break;
    case NExtract::NOverwriteMode::kSkip:
        result.overwriteMode = ExtractionOptions::OverwriteMode::SkipExisting;
        break;
    case NExtract::NOverwriteMode::kRename:
        result.overwriteMode = ExtractionOptions::OverwriteMode::RenameExtracted;
        break;
    case NExtract::NOverwriteMode::kRenameExisting:
        result.overwriteMode = ExtractionOptions::OverwriteMode::RenameExisting;
        break;
    default:
        break;
    }
    return result;
}
