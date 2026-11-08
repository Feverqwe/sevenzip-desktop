// Adapted from 7-Zip CPP/7zip/UI/GUI/ExtractDialog.cpp (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "dialogs/extract/ExtractDialog.h"
#include "7zip/UI/GUI/ExtractDialogRes.h"
#include "7zip/UI/GUI/ExtractRes.h"
#include "Windows/FileDir.h"
#include "Windows/FileName.h"
#include "dialogs/browse/BrowseDialog.h"
#include "fileops/patterns/Wildcard.h"
#include "localization/qtlangcontrols.h"
#include "platform/temp/qtownedtempidentity.h"
#include "settings/ViewSettings.h"
#include "ui/dialogs/qtdialoglayout.h"
#include "ui/lifecycle/qttaskpresentation.h"
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QLabel>
#include <QShowEvent>
#include <QStandardPaths>
#include <QTimer>
#include <span>
#include <utility>

namespace {
struct CExtractModeItem {
    int Value;
    UInt32 LangID;
};
static constexpr CExtractModeItem kPathModes[] = {
    {NExtract::NPathMode::kFullPaths, IDS_EXTRACT_PATHS_FULL},
    {NExtract::NPathMode::kNoPaths, IDS_EXTRACT_PATHS_NO}};
static constexpr CExtractModeItem kOverwriteModes[] = {
    {NExtract::NOverwriteMode::kAsk, IDS_EXTRACT_OVERWRITE_ASK},
    {NExtract::NOverwriteMode::kOverwrite, IDS_EXTRACT_OVERWRITE_WITHOUT_PROMPT},
    {NExtract::NOverwriteMode::kSkip, IDS_EXTRACT_OVERWRITE_SKIP_EXISTING},
    {NExtract::NOverwriteMode::kRename, IDS_EXTRACT_OVERWRITE_RENAME}};
static constexpr int kModeCaptionRole = Qt::UserRole + 1;
static const unsigned kHistorySize = 16;

QString InvalidOutputDirectory() {
    return QCoreApplication::translate("SevenZipDiagnostics", "Incorrect output directory path");
}

bool ResolveOutputDirectory(const QString &candidate, FString &resolved) {
    return !candidate.isEmpty() && !candidate.contains(QChar::Null) &&
           NWindows::NFile::NDir::MyGetFullPathName(
               us2fs(ToUString(QDir::toNativeSeparators(candidate))), resolved);
}

void AddDestinationToHistory(CObjectVector<QString> &history, const QString &path) {
    const UString name = ToUString(path);
    for (unsigned i = 0; i < history.Size(); ++i) {
        const QString &saved = history[i];
        if (path == saved)
            return;
        if (!name.IsEqualTo_NoCase(ToUString(saved)))
            continue;
        const QFileInfo current(path), previous(saved);
        if ((current.isDir() && !previous.exists()) || (previous.isDir() && !current.exists()))
            continue;
        if (!current.isDir() || !previous.isDir())
            return;
        const QString currentIdentity = QtOwnedTempLegacyIdentity(current.canonicalFilePath());
        const QString previousIdentity = QtOwnedTempLegacyIdentity(previous.canonicalFilePath());
        if (currentIdentity.isEmpty() || previousIdentity.isEmpty() ||
            currentIdentity == previousIdentity)
            return;
    }
    history.Add(path);
}

static void AddComboItems(QComboBox &combo, std::span<const CExtractModeItem> modes, int curVal) {
    for (const auto &mode : modes) {
        QString s = QtLangText(mode.LangID);
        s.remove('&');
        const int index = combo.count();
        combo.addItem(s, mode.Value);
        combo.setItemData(index, mode.LangID, kModeCaptionRole);
    }
    combo.setCurrentIndex(qMax(0, combo.findData(curVal)));
}
} // namespace

CExtractDialog::CExtractDialog(const QString &initialPath, const QStringList &history,
                               QWidget *parent,
                               std::function<QString(const QString &)> destinationErrorForPath)
    : QDialog(parent), _destinationErrorForPath(std::move(destinationErrorForPath)),
      DirPath(initialPath) {
    QtUseContextDialogModality(*this);
    QtTaskPresentation::Watch(*this, QtOperationPhase::Draft);
    ElimDup.Val = true;
    BuildPresentation();
    OnInit(history);
    UpdateAdvancedSummary();
    resize(QSize(520, sizeHint().height()).expandedTo(minimumSizeHint()));
    _initialPresentationSize = size();
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
    const int pathIndex = _pathMode->findData(int(
        pathMode == NExtract::NPathMode::kCurPaths ? NExtract::NPathMode::kFullPaths : pathMode));
    _pathMode->setCurrentIndex(qMax(0, pathIndex));
    const int overwriteIndex = _overwriteMode->findData(int(overwriteMode));
    _overwriteMode->setCurrentIndex(qMax(0, overwriteIndex));
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
    connect(_path, &QComboBox::activated, this, [this](int index) {
        if (!_nameEnable->isChecked())
            return;
        UString saved = ToUString(_path->itemText(index));
        saved.Trim();
        QString parent, name;
        const QString destination = ToQString(saved);
        SplitPathToParts_Smart(destination, parent, name);
        _path->setEditText(parent.isEmpty() ? destination : parent);
        _pathName->setText(parent.isEmpty() ? QString() : name);
    });
    AddComboItems(*_pathMode, kPathModes, PathMode);
    AddComboItems(*_overwriteMode, kOverwriteModes, OverwriteMode);
}

void CExtractDialog::showEvent(QShowEvent *event) {
    // Original OnInit appends ArcPath after its caller configures the dialog.
    setProperty("languageSuffix",
                ArcPath.IsEmpty() ? QString() : " : " + QString::fromWCharArray(ArcPath.Ptr()));
    QtReloadLangTexts(*this);
    QDialog::showEvent(event);
    UpdateAdvancedSummary();
    if (_initialPresentation) {
        _initialPresentation = false;
        QTimer::singleShot(0, this, [this] {
            if (isVisible() && size() == _initialPresentationSize)
                QtFitScrollableDialog(*this);
        });
    }
}

void CExtractDialog::changeEvent(QEvent *event) {
    if (event->type() == QEvent::LanguageChange && _pathMode && _overwriteMode) {
        for (auto *combo : {_pathMode, _overwriteMode})
            for (int index = 0; index < combo->count(); ++index)
                combo->setItemText(
                    index,
                    QtLangText(combo->itemData(index, kModeCaptionRole).toUInt()).remove('&'));
    }
    QDialog::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        UpdateAdvancedSummary();
        RefreshSourceSummary();
    }
}

void CExtractDialog::OnButtonSetPath() {
    const QString draft = _path->currentText().trimmed();
    FString initial;
    if (!draft.isEmpty() && !ResolveOutputDirectory(draft, initial)) {
        _destinationError->setText(InvalidOutputDirectory());
        _destinationError->show();
        _path->setFocus();
        return;
    }
    UString path;
    if (MyBrowseForFolder(this, ToUString(QtLangText(IDS_EXTRACT_SET_FOLDER)), fs2us(initial),
                          path))
        _path->setEditText(ToQString(path));
}

void CExtractDialog::OnOK() {
    UString s = DraftDestination();
    const QString base = _path->currentText().trimmed();
    const QString requested = ToQString(s);
    const auto directoryError = [this](const QString &candidate,
                                       FString *resolvedPath = nullptr) -> QString {
        FString resolved;
        if (!ResolveOutputDirectory(candidate, resolved))
            return InvalidOutputDirectory();
        if (resolvedPath)
            *resolvedPath = resolved;
        QString path = QDir::cleanPath(ToQString(fs2us(resolved)));
        if (_destinationErrorForPath)
            return _destinationErrorForPath(path);
        for (;;) {
            const QFileInfo entry(path);
            if (entry.exists() || entry.isSymLink())
                return entry.isDir() ? QString() : InvalidOutputDirectory();
            const QString parent = entry.absolutePath();
            if (parent == path)
                return {};
            path = parent;
        }
    };
    FString resolvedDestination;
    const QString error = base.isEmpty()
                              ? QCoreApplication::translate("CopyRedesign", "Choose a destination.")
                              : directoryError(requested, &resolvedDestination);
    if (!error.isEmpty()) {
        _destinationError->setText(error);
        _destinationError->show();
        if (_nameEnable->isChecked() && directoryError(base).isEmpty())
            _pathName->setFocus();
        else
            _path->setFocus();
        return;
    }
    const int pathMode2 = _pathMode->currentData().toInt();
    if (PathMode != NExtract::NPathMode::kCurPaths || pathMode2 != NExtract::NPathMode::kFullPaths)
        PathMode = (NExtract::NPathMode::EEnum)pathMode2;
    OverwriteMode = (NExtract::NOverwriteMode::EEnum)_overwriteMode->currentData().toInt();
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
    NWindows::NFile::NName::NormalizeDirPathPrefix(s);
    const bool splitDest = _nameEnable->isChecked();
    if (splitDest != _info.SplitDest.Val) {
        _info.SplitDest.Def = true;
        _info.SplitDest.Val = splitDest;
    }
    DirPath = QString::fromWCharArray(s.Ptr());
    _info.Paths.Clear();
    UString historyPath =
        NWindows::NFile::NName::IsAbsolutePath(s) ? s : fs2us(resolvedDestination);
    NWindows::NFile::NName::NormalizeDirPathPrefix(historyPath);
    AddDestinationToHistory(_info.Paths, ToQString(historyPath));
    for (int i = 0; i < _path->count() && _info.Paths.Size() < kHistorySize; ++i) {
        UString saved = ToUString(_path->itemText(i));
        saved.Trim();
        AddDestinationToHistory(_info.Paths, ToQString(saved));
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
    const QString base = _path->currentText();
    const QString name = _nameEnable->isChecked() ? _pathName->text() : QString();
    if (base.contains(QChar::Null) || name.contains(QChar::Null))
        return {};
    UString s = ToUString(base);
    s.Trim();
    if (_nameEnable->isChecked()) {
        NWindows::NFile::NName::NormalizeDirPathPrefix(s);
        UString pathName = ToUString(name);
        pathName.Trim();
        s += pathName;
    }
    return s;
}
