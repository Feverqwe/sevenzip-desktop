// Adapted from 7-Zip CPP/7zip/UI/FileManager/BrowseDialog.cpp (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Source custom-browser navigation, filtering, sorting and acceptance remain.
// Qt replaces Win32 controls and ordinary native picker services.
#include "7zip/UI/FileManager/BrowseDialogRes.h"
#include "7zip/UI/FileManager/PropertyNameRes.h"
#include "7zip/UI/FileManager/resource.h"
#include "Common/Defs.h"
#include "Common/IntToString.h"
#include "Common/StringConvert.h"
#include "Windows/FileFind.h"
#include "Windows/FileName.h"
#include "Windows/PropVariantConv.h"
#include "Windows/TimeUtils.h"
#include "dialogs/browse/qtsourcebrowsedialog_p.h"
#include "dialogs/combo/ComboDialog.h"
#include "filemanager/FormatUtils.h"
#include "fileops/native/FileDir.h"
#include "fileops/native/correctfspath.h"
#include "fileops/patterns/Wildcard.h"
#include "localization/qtlangcontrols.h"
#include "settings/RegistryUtils.h"
#include "settings/ViewSettings.h"
#include "ui/dialogs/qtdialoglayout.h"
#include "ui/dialogs/qtdialogscreenservices.h"
#include "ui/lifecycle/qttaskpresentation.h"
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>

using namespace NWindows;
using namespace NFile;
using namespace NName;
using namespace NFind;

extern UString HResultToMessage(HRESULT errorCode);

namespace NSourceBrowse {
namespace {
#ifdef _WIN32
const HRESULT kInvalidNameError = ERROR_INVALID_NAME;
#else
const HRESULT kInvalidNameError = E_INVALIDARG;
#endif
// The reference native picker uses Win32 MAX_PATH, independently of the host
// filesystem limit. Qt exposes no CommDlgExtendedError for its native picker;
// select the source fallback before opening an unsupported long initial path.
bool NeedsSourceBrowser(const UString &path) {
#ifdef _WIN32
    if (IsSuperOrDevicePath(path))
        return true;
#endif
    return path.Len() >= 260;
}

void MessageBox_HResError(QWidget *wnd, HRESULT errorCode, const UString &name) {
    UString s = HResultToMessage(errorCode);
    if (!name.IsEmpty()) {
        s.Add_LF();
        s += name;
    }
    QMessageBox::critical(wnd, "7-Zip", ToQString(s));
}

bool BrowseNext(CEnumerator &enumerator, CFileInfo &fi, bool &found) {
#ifdef _WIN32
    return enumerator.Next(fi, found);
#else
    CDirEntry entry;
    if (!enumerator.Next(entry, found))
        return false;
    // A general selector follows directory links, as the Windows picker does.
    if (!found)
        return true;
    // Keep broken links visible too; upstream's no-follow conversion retains
    // their own metadata when a target cannot be read.
    return enumerator.Fill_FileInfo(entry, fi, true) || enumerator.Fill_FileInfo(entry, fi, false);
#endif
}

} // namespace

void CBrowseDialog::OnInit() {
    const auto controls = CreateControls();
    ConnectControls(controls);
    CFmSettings st;
    st.Load(FmSettingsPath());
    _showDots = st.ShowDots;

    unsigned rootSize = GetRootPrefixSize(FilePath);
#ifdef _WIN32
    if (IsDrivePath(FilePath))
        rootSize = 0;
    else if (IsSuperPath(FilePath) && IsDrivePath(FilePath.Ptr(kSuperPathPrefixSize)))
        rootSize = kSuperPathPrefixSize;
#endif
    _topDirPrefix.SetFrom(FilePath, rootSize);
    UString name;
    if (StartInDirectory)
        DirPrefix = FilePath;
    else if (!GetParentPath(FilePath, DirPrefix, name))
        DirPrefix = _topDirPrefix;
    for (;;) {
        if (Reload(DirPrefix, name) == S_OK)
            break;
        name.Empty();
        if (DirPrefix.IsEmpty())
            break;
        UString parentPath, name2;
        GetParentPath(DirPrefix, parentPath, name2);
        DirPrefix = parentPath;
    }
    if (name.IsEmpty())
        name = FilePath;
    if (FolderMode)
        NormalizeDirPathPrefix(name);
    _pathEdit->setText(ToQString(name));
    CompletePresentation(controls);
}

bool CBrowseDialog::eventFilter(QObject *watched, QEvent *event) {
    if (watched == _folder &&
        (event->type() == QEvent::Resize || event->type() == QEvent::FontChange))
        SetFolderText();
    if (watched == _list && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            OnItemEnter();
            return true;
        }
        if (OnKeyDown(key))
            return true;
    }
    return QDialog::eventFilter(watched, event);
}

bool CBrowseDialog::OnKeyDown(QKeyEvent *keyDownInfo) {
    const bool ctrl = keyDownInfo->modifiers() & Qt::ControlModifier;
    switch (keyDownInfo->key()) {
    case Qt::Key_Backspace:
        OpenParentFolder();
        return true;
    case Qt::Key_R:
        if (ctrl) {
            Reload();
            return true;
        }
        return false;
    case Qt::Key_F7:
        OnCreateDir();
        return true;
    }
    return false;
}

bool CBrowseDialog::OnButtonClicked(unsigned buttonID) {
    switch (buttonID) {
    case IDB_BROWSE_PARENT:
        OpenParentFolder();
        break;
    case IDB_BROWSE_CREATE_DIR:
        OnCreateDir();
        break;
    default:
        return false;
    }
    _list->setFocus();
    return true;
}

void CBrowseDialog::OnOK() {
    if (_list->hasFocus()) {
        OnItemEnter();
        return;
    }
    FinishOnOK();
}

bool CBrowseDialog::GetParentPath(const UString &path, UString &parentPrefix, UString &name) {
    parentPrefix.Empty();
    name.Empty();
    if (path.IsEmpty() || _topDirPrefix == path)
        return false;
    UString s = path;
    if (IS_PATH_SEPAR(s.Back()))
        s.DeleteBack();
    if (s.IsEmpty() || IS_PATH_SEPAR(s.Back()))
        return false;
    const unsigned pos1 = (unsigned)(s.ReverseFind_PathSepar() + 1);
    parentPrefix.SetFrom(s, pos1);
    name = s.Ptr(pos1);
    return true;
}

int CBrowseDialog::CompareItems(int lParam1, int lParam2) const {
    if (lParam1 == lParam2)
        return 0;
    if (lParam1 == kParentIndex)
        return -1;
    if (lParam2 == kParentIndex)
        return 1;
    const CFileInfo &f1 = _files[lParam1];
    const CFileInfo &f2 = _files[lParam2];
    const bool isDir2 = f2.IsDir();
    if (f1.IsDir()) {
        if (!isDir2)
            return -1;
    } else if (isDir2)
        return 1;
    int res = 0;
    switch (_sortIndex) {
    case 0:
        res = CompareFileNames(fs2us(f1.Name), fs2us(f2.Name));
        break;
    case 1: {
        FILETIME t1, t2;
        FiTime_To_FILETIME(f1.MTime, t1);
        FiTime_To_FILETIME(f2.MTime, t2);
        res = CompareFileTime(&t1, &t2);
        break;
    }
    case 2:
        res = MyCompare(f1.Size, f2.Size);
        break;
    }
    return _ascending ? res : -res;
}

HRESULT CBrowseDialog::Reload(UString pathPrefix, const UString &selectedName) {
    CObjectVector<CFileInfo> files;
#ifdef _WIN32
    if (pathPrefix.IsEmpty() || pathPrefix.IsEqualTo(kSuperPathPrefix)) {
        FStringVector drives;
        if (!MyGetLogicalDriveStrings(drives))
            return GetLastError_noZero_HRESULT();
        FOR_VECTOR(i, drives) {
            const FString &d = drives[i];
            if (d.Len() < 2 || d.Back() != '\\')
                return E_FAIL;
            CFileInfo &fi = files.AddNew();
            fi.SetAsDir();
            fi.Name = d;
            fi.Name.DeleteBack();
        }
    } else
#endif
    {
        const UStringVector *masks = nullptr;
        if (!Filters.IsEmpty() && _filterCombo->count() > 0) {
            const int selected = _filterCombo->currentIndex();
            if ((unsigned)selected < Filters.Size()) {
                const UStringVector &m = Filters[selected].Masks;
                if (m.Size() > 1 ||
                    (m.Size() == 1 && !m[0].IsEqualTo("*.*") && !m[0].IsEqualTo("*")))
                    masks = &m;
            }
        }
        CEnumerator enumerator;
        enumerator.SetDirPrefix(us2fs(pathPrefix));
        CFileInfo fi;
        for (;;) {
            bool found;
            if (!BrowseNext(enumerator, fi, found))
                return GetLastError_noZero_HRESULT();
            if (!found)
                break;
            if (!fi.IsDir()) {
                if (FolderMode)
                    continue;
                if (masks) {
                    unsigned i;
                    for (i = 0; i < masks->Size(); i++)
                        if (DoesWildcardMatchName((*masks)[i], fs2us(fi.Name)))
                            break;
                    if (i == masks->Size())
                        continue;
                }
            }
            files.Add(fi);
        }
    }
    DirPrefix = pathPrefix;
    _files.Swap(files);
    PopulateRows(selectedName);
    return S_OK;
}

HRESULT CBrowseDialog::Reload() {
    UString selected;
    if (auto *item = SelectedItem()) {
        const int fileIndex = GetRealItemIndex(item);
        if (fileIndex != kParentIndex)
            selected = fs2us(_files[fileIndex].Name);
    }
    return Reload(DirPrefix, selected);
}

void CBrowseDialog::OpenParentFolder() {
    UString parent, selected;
    if (GetParentPath(DirPrefix, parent, selected)) {
        Reload(parent, selected);
        SetPathEditText();
    }
}

void CBrowseDialog::SetPathEditText() {
    auto *item = SelectedItem();
    if (!item) {
        if (FolderMode)
            _pathEdit->setText(ToQString(DirPrefix));
        return;
    }
    const int fileIndex = GetRealItemIndex(item);
    if (fileIndex == kParentIndex) {
        if (FolderMode)
            _pathEdit->setText(ToQString(UString(L".." WSTRING_PATH_SEPARATOR)));
        return;
    }
    const CFileInfo &file = _files[fileIndex];
    if (file.IsDir()) {
        if (!FolderMode)
            return;
        _pathEdit->setText(ToQString(fs2us(file.Name) + WCHAR_PATH_SEPARATOR));
    } else
        _pathEdit->setText(ToQString(fs2us(file.Name)));
}

void CBrowseDialog::OnCreateDir() {
    UString name;
    {
        UString enteredName;
        Dlg_CreateFolder(this, enteredName);
        if (enteredName.IsEmpty())
            return;
        if (!CorrectFsPath(DirPrefix, enteredName, name)) {
            MessageBox_HResError(this, kInvalidNameError, name);
            return;
        }
    }
    if (name.IsEmpty())
        return;
    FString destPath;
    if (GetFullPath(us2fs(DirPrefix), us2fs(name), destPath)) {
        if (!NDir::CreateComplexDir(destPath))
            MessageBox_HResError(this, GetLastError_noZero_HRESULT(), fs2us(destPath));
        else {
            Reload(DirPrefix, name);
            SetPathEditText();
        }
        _list->setFocus();
    }
}

void CBrowseDialog::OnItemEnter() {
    auto *item = SelectedItem();
    if (!item)
        return;
    const int fileIndex = GetRealItemIndex(item);
    if (fileIndex == kParentIndex)
        OpenParentFolder();
    else {
        const CFileInfo &file = _files[fileIndex];
        if (!file.IsDir()) {
            if (!FolderMode)
                FinishOnOK();
            return;
        }
        UString s = DirPrefix;
        s += fs2us(file.Name);
        s.Add_PathSepar();
        const HRESULT res = Reload(s, UString());
        if (res != S_OK)
            MessageBox_HResError(this, res, s);
        SetPathEditText();
    }
}

void CBrowseDialog::FinishOnOK() {
    const QString text = _pathEdit->text();
    if (text.contains(QChar::Null)) {
        MessageBox_HResError(this, kInvalidNameError, UString());
        _pathEdit->setFocus();
        return;
    }
    const UString s = ToUString(text);
    FString destPath;
    if (!GetFullPath(us2fs(DirPrefix), us2fs(s), destPath)) {
        MessageBox_HResError(this, kInvalidNameError, s);
        return;
    }
    FilePath = fs2us(destPath);
    if (FolderMode)
        NormalizeDirPathPrefix(FilePath);
    FilterIndex = _filterCombo->currentIndex();
    QDialog::accept();
}
} // namespace NSourceBrowse

using NSourceBrowse::CBrowseDialog;
using NSourceBrowse::NeedsSourceBrowser;

bool Dlg_CreateFolder(QWidget *wnd, UString &destName) {
    destName.Empty();
    CComboDialog dlg(QtLangText(IDS_CREATE_FOLDER), QtLangText(IDS_CREATE_FOLDER_NAME),
                     QtLangText(IDS_CREATE_FOLDER_DEFAULT_NAME), wnd);
    dlg.BindLanguage(IDS_CREATE_FOLDER, IDS_CREATE_FOLDER_NAME);
    if (dlg.exec() != QDialog::Accepted)
        return false;
    destName = dlg.Value;
    return true;
}

bool MyBrowseForFolder(QWidget *owner, const UString &title, const UString &path,
                       UString &resultPath) {
    const QtTaskPresentation::QuestionScope question;
    resultPath.Empty();
    if (NeedsSourceBrowser(path)) {
        CBrowseDialog dialog(owner);
        dialog.FolderMode = true;
        dialog.Title = title;
        dialog.FilePath = path;
        if (dialog.Create() != QDialog::Accepted)
            return false;
        resultPath = dialog.FilePath;
        return true;
    }
    QFileDialog dialog(owner, ToQString(title), ToQString(path));
    dialog.setFileMode(QFileDialog::Directory);
    dialog.setOption(QFileDialog::ShowDirsOnly);
    QtUseContextDialogModality(dialog);
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
        return false;
    const QString selected = dialog.selectedFiles().front();
    resultPath = ToUString(QDir::cleanPath(selected));
    return true;
}

QString BrowseForExistingFile(QWidget *owner, const QString &title, const QString &directory,
                              const QString &nameFilters) {
    const QtTaskPresentation::QuestionScope question;
    UString path = ToUString(directory);
    if (!NeedsSourceBrowser(path))
        return QFileDialog::getOpenFileName(owner, title, directory, nameFilters);

    CBrowseDialog dialog(owner);
    dialog.Title = ToUString(title);
    dialog.StartInDirectory = true;
    NormalizeDirPathPrefix(path);
    dialog.FilePath = path;
    dialog.FilterIndex = 0;
    for (const auto &nameFilter : nameFilters.split(";;", Qt::SkipEmptyParts)) {
        // Qt name filters end with a parenthesized, space-separated pattern list.
        const qsizetype patterns = nameFilter.lastIndexOf('(');
        if (patterns < 0 || !nameFilter.endsWith(')'))
            continue;
        auto &filter = dialog.Filters.AddNew();
        filter.Description = ToUString(nameFilter.left(patterns).trimmed());
        const auto masks = nameFilter.sliced(patterns + 1, nameFilter.size() - patterns - 2);
        for (const auto &mask : masks.split(' ', Qt::SkipEmptyParts))
            filter.Masks.Add(ToUString(mask));
    }
    if (dialog.Create() != QDialog::Accepted)
        return {};
    return ToQString(dialog.FilePath);
}

bool CBrowseInfo::BrowseForFile(const CObjectVector<CBrowseFilterInfo> &filters) {
    const QtTaskPresentation::QuestionScope question;
    if (NeedsSourceBrowser(FilePath)) {
        CBrowseDialog dialog(hwndOwner);
        dialog.FolderMode = false;
        dialog.SaveMode = SaveMode;
        dialog.FilterIndex = FilterIndex;
        dialog.Filters = filters;
        if (lpstrTitle)
            dialog.Title = lpstrTitle;
        dialog.FilePath = FilePath;
        if (dialog.Create() != QDialog::Accepted)
            return false;
        FilePath = dialog.FilePath;
        FilterIndex = dialog.FilterIndex;
        return true;
    }
    UStringVector filters2;
    FOR_VECTOR(i, filters) {
        const CBrowseFilterInfo &fi = filters[i];
        filters2.Add(fi.Description);
        UString s;
        FOR_VECTOR(k, fi.Masks) {
            if (k != 0)
                s += ";";
            s += fi.Masks[k];
        }
        filters2.Add(s);
    }
    NWindows::CCommonDialogInfo request = *this;
    UString dirPrefix;
    FString prefix, name;
    if (NDir::GetFullPathAndSplit(us2fs(FilePath), prefix, name)) {
        dirPrefix = fs2us(prefix);
        request.FilePath = fs2us(name);
    }
    if (!request.CommonDlg_BrowseForFile(!dirPrefix.IsEmpty() ? dirPrefix.Ptr() : nullptr,
                                         filters2))
        return false;
    FilePath = request.FilePath;
    FilterIndex = request.FilterIndex;
    return true;
}
