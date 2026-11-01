// Adapted from 7-Zip BrowseDialog2.cpp (0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Native enumeration/counts, properties, sorting, navigation and delete workflow.
// Qt controls/services and owned-temp membership replace Win32 presentation/filter.
#include "dialogs/browse/BrowseDialog2.h"
#include "7zip/UI/Common/PropIDUtils.h"
#include "7zip/UI/FileManager/PropertyNameRes.h"
#include "7zip/UI/FileManager/resource.h"
#include "Common/StringConvert.h"
#include "Windows/FileName.h"
#include "Windows/PropVariantConv.h"
#include "Windows/TimeUtils.h"
#include "filemanager/FormatUtils.h"
#include "filemanager/HelpUtils.h"
#include "fileops/native/FileDir.h"
#include "fileops/patterns/Wildcard.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "platform/temp/qtownedtempidentity.h"
#include "ui/dialogs/qtdialogscreenservices.h"
#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QTreeWidget>
#include <QUrl>
#ifdef _WIN32
#include "Common/MyCom.h"
#include "Windows/ProcessUtils.h"
#include "platform/loader/DLL.h"
#include "platform/shell/Shell.h"
#endif

using namespace NWindows;
using namespace NFile;
using namespace NName;
using namespace NFind;
static const int kParentIndex = -1;
static const UInt32 k_EnumerateDirsLimit = 200;
static const UInt32 k_EnumerateFilesLimit = 2000;
extern UString HResultToMessage(HRESULT errorCode);
void AddSizeValue(UString &s, UInt64 value);
wchar_t *Browse_ConvertSizeToString(UInt64 v, wchar_t *s Z7_lifetimebound);

namespace {
QString QtPath(const UString &path) { return QString::fromWCharArray(path.Ptr()); }
void MessageBox_HResError(QWidget *wnd, HRESULT code, const UString &name) {
    UString s = HResultToMessage(code);
    if (!name.IsEmpty()) {
        s.Add_LF();
        s += name;
    }
    QMessageBox::critical(wnd, "7-Zip", QtPath(s));
}
void MessageBox_LastError_path(QWidget *wnd, const FString &path) {
    MessageBox_HResError(wnd, GetLastError_noZero_HRESULT(), fs2us(path));
}
// POSIX upstream enumerators expose dirent then a no-follow FileInfo conversion.
bool BrowseNext(CEnumerator &enumerator, CFileInfo &fi, bool &found) {
#ifdef _WIN32
    return enumerator.Next(fi, found);
#else
    CDirEntry entry;
    if (!enumerator.Next(entry, found))
        return false;
    return !found || enumerator.Fill_FileInfo(entry, fi, false);
#endif
}
// New ownership boundary: upstream's 7z* filter provides no ownership proof.
QString TempPathKey(const QString &path) {
#ifdef Q_OS_MACOS
    // macOS enumeration can decompose a name created through a composed Qt path.
    return path.normalized(QString::NormalizationForm_C);
#else
    return path;
#endif
}
bool OwnedPath(const QtOwnedTempEntry &owner, const QString &path, const QString &temp) {
    if (owner.key.isEmpty() || !QtOwnedTempMatchesIdentity(owner.path, owner.identity) ||
        QFileInfo(owner.path).dir().canonicalPath() != QFileInfo(temp).canonicalFilePath())
        return false;
    const QString root = QFileInfo(owner.path).canonicalFilePath();
    const QString target = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    const QString relative = QDir(TempPathKey(root)).relativeFilePath(TempPathKey(target));
    if (QDir::isAbsolutePath(relative) || relative == ".." || relative.startsWith("../"))
        return false;
    QString current = target;
    for (;;) {
        QFileInfo info(current);
        if (info.isSymLink() || info.isJunction() || !info.exists())
            return false;
        if (TempPathKey(current) == TempPathKey(root))
            return QtOwnedTempMatchesIdentity(current, owner.identity);
        const QString parent = info.absolutePath();
        if (parent == current)
            return false;
        current = parent;
    }
}
} // namespace

struct CBrowseEnumerator {
    FString Path;
    CFileInfo fi, fi_SubFile;
    CBrowseItem bi;
    CBrowseEnumerator() { bi.Construct(); }
    void Enumerate(unsigned level);
    bool NeedInterrupt() const {
        return bi.NumFiles >= k_EnumerateFilesLimit || bi.NumDirs >= k_EnumerateDirsLimit;
    }
};
void CBrowseEnumerator::Enumerate(unsigned level) {
    Path.Add_PathSepar();
    const unsigned len = Path.Len();
    CObjectVector<FString> names;
    {
        CEnumerator enumerator;
        enumerator.SetDirPrefix(Path);
        bool found;
        while (BrowseNext(enumerator, fi, found) && found) {
            if (level == 0) {
                if (bi.NumRootItems == 0)
                    fi_SubFile = fi;
                bi.NumRootItems++;
            }
            if (fi.IsDir()) {
                bi.NumDirs++;
                if (!fi.IsOsSymLink())
                    names.Add(fi.Name);
            } else {
                bi.NumFiles++;
                bi.Size += fi.Size;
            }
            if (level != 0 || bi.NumRootItems > 1)
                if (NeedInterrupt()) {
                    bi.WasInterrupted = true;
                    return;
                }
        }
    }
    FOR_VECTOR(i, names) {
        if (NeedInterrupt()) {
            bi.WasInterrupted = true;
            return;
        }
        Path.DeleteFrom(len);
        Path += names[i];
        Enumerate(level + 1);
    }
}

static void PrintPropsPrefix(UString &s, UInt32 id) {
    s.Add_LF();
    s += "    ";
    AddLangString(s, id);
    s += ": ";
}
static void PrintProps_Size(UString &s, UInt64 size) {
    PrintPropsPrefix(s, IDS_PROP_SIZE);
    AddSizeValue(s, size);
}
static void PrintProps_MTime(UString &s, const CFileInfo &fi) {
    PrintPropsPrefix(s, IDS_PROP_MTIME);
    char t[64];
    FILETIME ft;
    FiTime_To_FILETIME(fi.MTime, ft);
    ConvertUtcFileTimeToString(ft, t);
    s += t;
}
static void PrintProps_Name(UString &s, const CFileInfo &fi) {
    s += fs2us(fi.Name);
    if (fi.IsDir())
        s.Add_PathSepar();
}
static void PrintProps_Attrib(UString &s, const CFileInfo &fi) {
    PrintPropsPrefix(s, IDS_PROP_ATTRIBUTES);
    char props[64];
    ConvertWinAttribToString(props, fi.GetWinAttrib());
    s += props;
}
static void PrintProps(UString &s, const CBrowseItem &bi, const CFileInfo &fi,
                       const CFileInfo *fi2) {
    PrintProps_Name(s, fi);
    PrintProps_Attrib(s, fi);
    if (bi.NumDirs != 0) {
        PrintPropsPrefix(s, IDS_PROP_FOLDERS);
        s.Add_UInt32(bi.NumDirs);
        if (bi.WasInterrupted)
            s += "+";
    }
    if (bi.NumFiles != 0) {
        PrintPropsPrefix(s, IDS_PROP_FILES);
        s.Add_UInt32(bi.NumFiles);
        if (bi.WasInterrupted)
            s += "+";
    }
    {
        PrintProps_Size(s, bi.Size);
        if (bi.WasInterrupted)
            s += "+";
    }
    PrintProps_MTime(s, fi);
    if (fi2) {
        s.Add_LF();
        s += "----------------";
        s.Add_LF();
        PrintProps_Name(s, *fi2);
        PrintProps_Attrib(s, *fi2);
        if (!fi2->IsDir())
            PrintProps_Size(s, fi2->Size);
        PrintProps_MTime(s, *fi2);
    }
}

void CBrowseDialog2::PrintFileProps(UString &s, const CFileInfo &file) {
    _filePropsValid = true;
    CFileInfo file2;
    FString path = us2fs(_snapshot->Prefix);
    path += file.Name;
    if (!file2.Find(path)) {
        _filePropsValid = false;
        MessageBox_LastError_path(this, path);
        Reload_WithErrorMessage();
        return;
    }
    CBrowseEnumerator enumer;
    enumer.bi.Size = file2.Size;
    if (file2.IsDir() && !file2.IsOsSymLink()) {
        enumer.Path = path;
        enumer.Enumerate(0);
    }
    PrintProps(s, enumer.bi, file2, enumer.bi.NumRootItems == 1 ? &enumer.fi_SubFile : NULL);
}
void CBrowseDialog2::Show_FileProps_Window(const CFileInfo &file) {
    UString s;
    PrintFileProps(s, file);
    if (!_filePropsValid)
        return;
    QMessageBox::information(this, QtPath(LangString(IDS_PROPERTIES)), QtPath(s));
}

void CBrowseDialog2::OnDelete() {
    if (!_snapshot->Prefix.IsPrefixedBy(TempFolderPath))
        return;
    CUIntVector indices;
    GetSelected_RealIndexes(indices);
    if (indices.Size() == 0)
        return;
    {
        UInt32 titleID, messageID;
        UString messageParam, s2;
        if (indices.Size() == 1) {
            const unsigned index = indices[0];
            const CBrowseItem &bi = _snapshot->Items[index];
            const CFileInfo &file = _snapshot->Files[bi.MainFileIndex];
            PrintFileProps(s2, file);
            // Source PrintFileProps reloads on error; those row references and
            // selection indices are then invalid. Never confirm a different item.
            if (!_filePropsValid)
                return;
            messageParam = fs2us(file.Name);
            if (file.IsDir()) {
                titleID = IDS_CONFIRM_FOLDER_DELETE;
                messageID = IDS_WANT_TO_DELETE_FOLDER;
            } else {
                titleID = IDS_CONFIRM_FILE_DELETE;
                messageID = IDS_WANT_TO_DELETE_FILE;
            }
        } else {
            titleID = IDS_CONFIRM_ITEMS_DELETE;
            messageID = IDS_WANT_TO_DELETE_ITEMS;
            messageParam = NumberToString(indices.Size());
            for (UInt32 i = 0; i < indices.Size(); i++) {
                if (i >= 10) {
                    s2 += "...";
                    break;
                }
                const CBrowseItem &bi = _snapshot->Items[indices[i]];
                const CFileInfo &fi = _snapshot->Files[bi.MainFileIndex];
                PrintProps_Name(s2, fi);
                s2.Add_LF();
            }
        }
        UString s = indices.Size() == 1 ? MyFormatNew(messageID, messageParam)
                                        : MyFormatNew(messageID, UInt64(indices.Size()));
        if (!s2.IsEmpty()) {
            s.Add_LF();
            s.Add_LF();
            s += s2;
        }
        if (QMessageBox::question(this, QtPath(LangString(titleID)), QtPath(s),
                                  QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
                                  QMessageBox::No) != QMessageBox::Yes)
            return;
    }
    for (UInt32 i = 0; i < indices.Size(); i++) {
        const unsigned index = indices[i];
        bool result = true;
        const CBrowseItem &bi = _snapshot->Items[index];
        const CFileInfo &fi = _snapshot->Files[bi.MainFileIndex];
        if (fi.Name.IsEmpty())
            return;
        const FString fullPath = us2fs(_snapshot->Prefix) + fi.Name;
        auto lease = QtOwnedTempRegistry().ClaimForDeletion(_snapshot->Items[index].Owner);
        if (!lease || !OwnedPath(_snapshot->Items[index].Owner, QtPath(fs2us(fullPath)),
                                 QtPath(TempFolderPath))) {
            QMessageBox::warning(this, "7-Zip",
                                 QCoreApplication::translate(
                                     "BrowseDialog2", "This temporary item is active, protected, "
                                                      "or no longer owned. Refresh the list."));
            return;
        }
        if (fi.IsDir())
            result = NDir::RemoveDirWithSubItems(fullPath);
        else
            result = NDir::DeleteFileAlways(fullPath);
        if (!result) {
            MessageBox_LastError_path(this, fullPath);
            return;
        }
    }
    Reload_WithErrorMessage();
}

bool CBrowseDialog2::IsExactTempFolder(const UString &path) const {
    return CompareFileNames(path, TempFolderPath) == 0;
}
int CBrowseDialog2::CompareItems(int index1, int index2) const {
    if (index1 == index2)
        return 0;
    if (index1 == kParentIndex)
        return -1;
    if (index2 == kParentIndex)
        return 1;
    const CBrowseItem &item1 = _snapshot->Items[index1], &item2 = _snapshot->Items[index2];
    const CFileInfo &f1 = _snapshot->Files[item1.MainFileIndex],
                    &f2 = _snapshot->Files[item2.MainFileIndex];
    const bool isDir2 = f2.IsDir();
    if (f1.IsDir()) {
        if (!isDir2)
            return -1;
    } else if (isDir2)
        return 1;
    const int res2 = MyCompare(index1, index2);
    int res = 0;
    switch (_sortIndex) {
    case 0:
        res = CompareFileNames(fs2us(f1.Name), fs2us(f2.Name));
        break;
    case 1:
        res = Compare_FiTime(&f1.MTime, &f2.MTime);
        break;
    case 2:
        res = MyCompare(item1.Size, item2.Size);
        break;
    case 3:
        res = MyCompare(item1.NumFiles, item2.NumFiles);
        break;
    case 4:
        res = MyCompare(item1.NumDirs, item2.NumDirs);
        break;
    case 5: {
        const int sub1 = item1.SubFileIndex, sub2 = item2.SubFileIndex;
        if (sub1 < 0) {
            if (sub2 >= 0)
                res = -1;
        } else if (sub2 < 0)
            res = 1;
        else
            res = CompareFileNames(fs2us(_snapshot->Files[sub1].Name),
                                   fs2us(_snapshot->Files[sub2].Name));
        break;
    }
    }
    if (res == 0)
        res = res2;
    return _ascending ? res : -res;
}
void CBrowseDialog2::ChangeSorting_and_Reload(int columnIndex) {
    if (columnIndex == _sortIndex)
        _ascending = !_ascending;
    else {
        _ascending = columnIndex == 0 || columnIndex == 5;
        _sortIndex = columnIndex;
    }
    Reload_WithErrorMessage();
}
void CBrowseDialog2::Reload_WithErrorMessage() {
    const HRESULT res = Reload();
    if (res != S_OK)
        MessageBox_HResError(this, res, _snapshot->Prefix);
}
HRESULT CBrowseDialog2::Reload(const UString &pathPrefix, const UString &selectedName) {
    UStringVector selectedVector;
    if (!selectedName.IsEmpty())
        selectedVector.Add(selectedName);
    return Reload(pathPrefix, selectedVector, selectedName);
}
struct CWaitCursor2 {
    bool _waitCursor = false;
    void Set() {
        if (!_waitCursor) {
            QApplication::setOverrideCursor(Qt::WaitCursor);
            _waitCursor = true;
        }
    }
    ~CWaitCursor2() {
        if (_waitCursor)
            QApplication::restoreOverrideCursor();
    }
};
HRESULT CBrowseDialog2::Reload(const UString &pathPrefix, const UStringVector &selectedVector2,
                               const UString &focusedName) {
    UStringVector selectedVector = selectedVector2;
    selectedVector.Sort();
    auto snapshot = std::make_unique<Snapshot>();
    auto &files = snapshot->Files;
    auto &items = snapshot->Items;
    auto &root = snapshot->Root;
    CWaitCursor2 waitCursor;
    const auto entries = QtOwnedTempRegistry().Entries();
    const bool isTempFolder = IsExactTempFolder(pathPrefix);
    QHash<QString, QtOwnedTempEntry> rootOwners;
    if (isTempFolder) {
        const QString temp = QFileInfo(QtPath(TempFolderPath)).canonicalFilePath();
        for (const auto &entry : entries)
            if (QFileInfo(entry.path).dir().canonicalPath() == temp)
                rootOwners.insert(TempPathKey(QFileInfo(entry.path).fileName()), entry);
    } else {
        for (const auto &entry : entries)
            if (OwnedPath(entry, QtPath(pathPrefix), QtPath(TempFolderPath))) {
                root = entry;
                break;
            }
    }
    if (!isTempFolder && !OwnedPath(root, QtPath(pathPrefix), QtPath(TempFolderPath)))
        return HRESULT(0x80070005L); // E_ACCESSDENIED is absent from POSIX MyWindows.h.
    {
        CEnumerator enumerator;
        enumerator.SetDirPrefix(us2fs(pathPrefix));
        CFileInfo fi;
        for (;;) {
            bool found;
            if (!BrowseNext(enumerator, fi, found))
                return GetLastError_noZero_HRESULT();
            if (!found)
                break;
            QtOwnedTempEntry owner = root;
            if (isTempFolder) {
                owner = rootOwners.value(TempPathKey(QtPath(fs2us(fi.Name))));
                const QString fullPath = QtPath(pathPrefix + fs2us(fi.Name));
                if (!OwnedPath(owner, fullPath, QtPath(TempFolderPath)))
                    continue;
            }
            OwnedItem item;
            item.Construct();
            item.Owner = owner;
            item.MainFileIndex = files.Size();
            item.Size = fi.Size;
            files.Add(fi);
            items.Add(item);
        }
    }
    UInt64 cnt = items.Size();
    FOR_VECTOR(i, items) {
        CBrowseItem &item = items[i];
        const CFileInfo &fi = files[item.MainFileIndex];
        if (!fi.IsDir() || fi.IsOsSymLink())
            continue;
        CBrowseEnumerator enumer;
        enumer.bi = item;
        enumer.Path = us2fs(pathPrefix);
        enumer.Path += fi.Name;
        enumer.Enumerate(0);
        item = enumer.bi;
        if (item.NumRootItems == 1) {
            item.SubFileIndex = (int)files.Size();
            files.Add(enumer.fi_SubFile);
        }
        cnt += item.NumDirs;
        cnt += item.NumFiles;
        if (cnt > 1000)
            waitCursor.Set();
    }
    snapshot->Prefix = pathPrefix;
    _snapshot.swap(snapshot);
    PopulateRows(selectedVector, focusedName);
    return S_OK;
}
HRESULT CBrowseDialog2::Reload() {
    UStringVector selected;
    CUIntVector indexes;
    GetSelected_RealIndexes(indexes);
    FOR_VECTOR(i, indexes)
    selected.Add(fs2us(_snapshot->Files[_snapshot->Items[indexes[i]].MainFileIndex].Name));
    UString focusedName;
    if (auto *row = _list->currentItem()) {
        const int realIndex = row->data(0, Qt::UserRole).toInt();
        if (realIndex >= 0)
            focusedName = fs2us(_snapshot->Files[_snapshot->Items[realIndex].MainFileIndex].Name);
    }
    const UString dirPathTemp = _snapshot->Prefix;
    return Reload(dirPathTemp, selected, focusedName);
}
void CBrowseDialog2::OnOK() {
    if (QApplication::focusWidget() == _list) {
        OnItemEnter();
        return;
    }
    // Original Enter on the read-only path must not end the dialog.
}

void CBrowseDialog2::accept() { OnOK(); }

bool CBrowseDialog2::GetParentPath(const UString &path, UString &parentPrefix, UString &name) {
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
void CBrowseDialog2::OpenParentFolder() {
    if (IsExactTempFolder(_snapshot->Prefix))
        return;
    UString parent, selected;
    if (GetParentPath(_snapshot->Prefix, parent, selected))
        Reload(parent, selected);
}
void CBrowseDialog2::OnItemEnter() {
    const auto selected = _list->selectedItems();
    if (selected.size() == 1 && selected.first()->data(0, Qt::UserRole).toInt() == kParentIndex) {
        OpenParentFolder();
        return;
    }
    CUIntVector indices;
    GetSelected_RealIndexes(indices);
    if (indices.Size() != 1)
        return;
    const unsigned realIndex = indices[0];
    const CFileInfo &file = _snapshot->Files[_snapshot->Items[realIndex].MainFileIndex];
    const auto modifiers = QApplication::keyboardModifiers();
    if (modifiers.testFlag(Qt::AltModifier)) {
        Show_FileProps_Window(file);
        return;
    }
    const UString fullPath = _snapshot->Prefix + fs2us(file.Name);
    if (file.IsOsSymLink() ||
        !OwnedPath(_snapshot->Items[realIndex].Owner, QtPath(fullPath), QtPath(TempFolderPath))) {
        QMessageBox::warning(
            this, "7-Zip",
            QCoreApplication::translate("BrowseDialog2", "Link opening was blocked by 7-Zip."));
        return;
    }
    bool needExternal = true;
    if (file.IsDir())
        if (!modifiers.testFlag(Qt::ShiftModifier) || modifiers.testFlag(Qt::ControlModifier))
            needExternal = false;
    if (needExternal) {
        OpenOutside(fullPath, _snapshot->Items[realIndex].Owner, false);
        return;
    }
    UString s = fullPath;
    s.Add_PathSepar();
    const HRESULT res = Reload(s, UString());
    if (res != S_OK)
        MessageBox_HResError(this, res, s);
}
#ifdef _WIN32
HRESULT ShellFolder_ParseDisplayName(IShellFolder *shellFolder, HWND hwnd, const UString &path,
                                     LPITEMIDLIST *ppidl);

HRESULT StartApplication(const UString &dir, const UString &path, QWidget *window,
                         CProcess &process);
HRESULT StartApplication(const UString &dir, const UString &path, QWidget *window,
                         CProcess &process) {
    UString path2 = path;
    UINT32 result;
    {
#ifdef _WIN32
        NShell::CItemIDList pidl;
        // SHELLEXECUTEINFO::pidl is more accurate way than SHELLEXECUTEINFO::lpFile
        {
            CMyComPtr<IShellFolder> desktop;
            if (SHGetDesktopFolder(&desktop) == S_OK && desktop)
                if (ShellFolder_ParseDisplayName(desktop,
                                                 NULL, // HWND : do we need (window) or NULL here?
                                                 path, &pidl) != S_OK)
                    pidl.Detach();
        }
        {
            const int dot = path2.ReverseFind_Dot();
            const int separ = path2.ReverseFind_PathSepar();
            if (separ != (int)path2.Len() - 1)
                if (dot < 0 || dot < separ)
                    path2.Add_Dot();
        }
#endif // _WIN32

#ifndef _UNICODE
        if (g_IsNT) {
            SHELLEXECUTEINFOW execInfo;
            memset(&execInfo, 0, sizeof(execInfo));
            // execInfo.hwnd = NULL;
            // execInfo.lpVerb = NULL;
            // execInfo.lpFile = NULL;
            // execInfo.lpDirectory = NULL;
            // execInfo.lpParameters = NULL;
            // execInfo.hProcess = NULL;
            execInfo.cbSize = sizeof(execInfo);
            execInfo.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_DDEWAIT;
            if (!dir.IsEmpty())
                execInfo.lpDirectory = dir;
            execInfo.nShow = SW_SHOWNORMAL;

            if ((LPCITEMIDLIST)pidl) {
                execInfo.lpIDList = pidl;
                execInfo.fMask |= SEE_MASK_IDLIST;
            } else
                execInfo.lpFile = path2;

            typedef BOOL(WINAPI * Func_ShellExecuteExW)(LPSHELLEXECUTEINFOW lpExecInfo);
            Z7_DIAGNOSTIC_IGNORE_CAST_FUNCTION
            const Func_ShellExecuteExW f_ShellExecuteExW = Z7_GET_PROC_ADDRESS(
                Func_ShellExecuteExW, ::GetModuleHandleW(L"shell32.dll"), "ShellExecuteExW");
            if (!f_ShellExecuteExW)
                return 0;
            f_ShellExecuteExW(&execInfo);
            result = (UINT32)(UINT_PTR)execInfo.hInstApp;
            process.Attach(execInfo.hProcess);
        } else
#endif
        {
            SHELLEXECUTEINFO execInfo;
            memset(&execInfo, 0, sizeof(execInfo));
            // execInfo.hwnd = NULL;
            // execInfo.lpVerb = NULL;
            // execInfo.lpFile = NULL;
            // execInfo.lpDirectory = NULL;
            // execInfo.lpParameters = NULL;
            // execInfo.hProcess = NULL;
            execInfo.cbSize = sizeof(execInfo);
            execInfo.fMask = SEE_MASK_NOCLOSEPROCESS
#ifndef UNDER_CE
                             | SEE_MASK_FLAG_DDEWAIT
#endif
                ;
            execInfo.nShow = SW_SHOWNORMAL;
            const CSysString sysPath(GetSystemString(path2));
            const CSysString sysDir(GetSystemString(dir));
#ifndef UNDER_CE
            if (!sysDir.IsEmpty())
                execInfo.lpDirectory = sysDir;
#endif

            if ((LPCITEMIDLIST)pidl) {
                execInfo.lpIDList = pidl;
                execInfo.fMask |= SEE_MASK_IDLIST;
            } else
                execInfo.lpFile = sysPath;
            ::ShellExecuteEx(&execInfo);
            result = (UINT32)(UINT_PTR)execInfo.hInstApp;
            process.Attach(execInfo.hProcess);
        }
        // DEBUG_PRINT_NUM("-- ShellExecuteEx -- execInfo.hInstApp = ", result)
    }
    if (result <= 32) {
        switch (result) {
        case SE_ERR_NOASSOC:
            MessageBox_HResError(
                window, GetLastError_noZero_HRESULT(), UString()
                // L"There is no application associated with the given file name extension",
            );
        }
        return E_FAIL; // fixed in 15.13. Can we use it for any Windows version?
    }
    return S_OK;
}

void StartApplicationDontWait(const UString &dir, const UString &path, QWidget *window);
void StartApplicationDontWait(const UString &dir, const UString &path, QWidget *window) {
    CProcess process;
    StartApplication(dir, path, window, process);
}

#endif

void CBrowseDialog2::OpenOutside(const UString &path, const QtOwnedTempEntry &owner,
                                 bool sevenzip) {
    // This source browser does not wait. Retain protection after closing any
    // native handle; only the archive-open worker proves associated completion.
    auto lease = QtOwnedTempRegistry().ClaimForDeletion(owner);
    if (!lease || !OwnedPath(owner, QtPath(path), QtPath(TempFolderPath)) ||
        !lease->ProtectExternal())
        return;
    bool opened;
    if (sevenzip)
        opened = QProcess::startDetached(QCoreApplication::applicationFilePath(), {QtPath(path)});
#ifdef _WIN32
    else if (!_opener) {
        CProcess process;
        const HRESULT result = StartApplication(_snapshot->Prefix, path, this, process);
        opened = result == S_OK || (HANDLE)process != NULL;
    }
#endif
    else
        opened = (_opener ? _opener : QDesktopServices::openUrl)(QUrl::fromLocalFile(QtPath(path)));
    if (!opened) {
        lease->ExternalFinished();
        QMessageBox::warning(
            this, "7-Zip",
            QCoreApplication::translate("BrowseDialog2", "Could not open the temporary item."));
    }
    lease.reset(); // Show unconfirmed external protection after this launch lease ends.
    Reload_WithErrorMessage();
}
void CBrowseDialog2::OnContextMenu(const QPoint &position) {
    CUIntVector indices;
    GetSelected_RealIndexes(indices);
    const UInt32 k_CmdId_Delete = 1, k_CmdId_Open_Explorer = 2, k_CmdId_Open_7zip = 3,
                 k_CmdId_Props = 4;
    QMenu menu(this);
    unsigned numMenuItems = 0;
    for (unsigned cmd = k_CmdId_Delete; cmd <= k_CmdId_Props; cmd++) {
        if (cmd == k_CmdId_Delete) {
            if (indices.Size() == 0)
                continue;
        } else if (indices.Size() > 1)
            break;
        if (numMenuItems != 0)
            if (cmd == k_CmdId_Open_Explorer || cmd == k_CmdId_Props)
                menu.addSeparator();
        UString s;
        if (cmd == k_CmdId_Delete) {
            s = LangString(IDS_BUTTON_DELETE);
            s += "\tDelete";
        } else if (cmd == k_CmdId_Open_Explorer || cmd == k_CmdId_Open_7zip) {
            s = LangString(IDM_OPEN_OUTSIDE);
            if (s.IsEmpty())
                s = "Open Outside";
            s += cmd == k_CmdId_Open_Explorer ? "\tShift+Enter" : " : 7-Zip";
        } else {
            s = LangString(IDS_PROPERTIES);
            if (s.IsEmpty())
                s = "Properties";
            s += "\tAlt+Enter";
        }
        s.RemoveChar(L'&');
        auto *action = menu.addAction(QtPath(s));
        action->setData(cmd);
        if (cmd == k_CmdId_Delete)
            action->setEnabled(_delete->isEnabled());
        if (cmd == k_CmdId_Open_Explorer || cmd == k_CmdId_Open_7zip) {
            const auto owner =
                indices.Size() == 1 ? _snapshot->Items[indices[0]].Owner : _snapshot->Root;
            action->setEnabled(!owner.key.isEmpty() && !owner.active && !owner.externallyProtected);
        }
        if (cmd == k_CmdId_Props)
            action->setEnabled(indices.Size() == 1);
        numMenuItems++;
    }
    if (numMenuItems == 0)
        return;
    QAction *chosen = menu.exec(_list->viewport()->mapToGlobal(position));
    if (!chosen)
        return;
    const unsigned menuResult = chosen->data().toUInt();
    if (menuResult == k_CmdId_Delete) {
        OnDelete();
        return;
    }
    if (indices.Size() <= 1) {
        UString fullPath = _snapshot->Prefix;
        QtOwnedTempEntry owner = _snapshot->Root;
        if (indices.Size() != 0) {
            const CFileInfo &file = _snapshot->Files[_snapshot->Items[indices[0]].MainFileIndex];
            if (file.IsOsSymLink()) {
                QMessageBox::warning(this, "7-Zip",
                                     QCoreApplication::translate(
                                         "BrowseDialog2", "Link opening was blocked by 7-Zip."));
                return;
            }
            fullPath += fs2us(file.Name);
            owner = _snapshot->Items[indices[0]].Owner;
        }
        if (menuResult == k_CmdId_Open_Explorer || menuResult == k_CmdId_Open_7zip) {
            OpenOutside(fullPath, owner, menuResult == k_CmdId_Open_7zip);
            return;
        }
        if (indices.Size() == 1 && menuResult == k_CmdId_Props)
            Show_FileProps_Window(_snapshot->Files[_snapshot->Items[indices[0]].MainFileIndex]);
    }
}
void CBrowseDialog2::OnHelp() { ShowHelpWindow("fm/temp.htm", this); }
bool CBrowseDialog2::OnKeyDown(QKeyEvent *keyDownInfo) {
    const bool ctrl = keyDownInfo->modifiers().testFlag(Qt::ControlModifier);
    switch (keyDownInfo->key()) {
    case Qt::Key_Backspace:
        OpenParentFolder();
        return true;
    case Qt::Key_R:
        if (ctrl) {
            Reload_WithErrorMessage();
            return true;
        }
        return false;
    case Qt::Key_F3:
    case Qt::Key_F5:
    case Qt::Key_F6:
        if (ctrl) {
            int index = 0;
            if (keyDownInfo->key() == Qt::Key_F5)
                index = 1;
            else if (keyDownInfo->key() == Qt::Key_F6)
                index = 2;
            ChangeSorting_and_Reload(index);
            Reload_WithErrorMessage();
            return true;
        }
        return false;
    case Qt::Key_A:
        if (ctrl) {
            _list->selectAll();
            return true;
        }
        return false;
    case Qt::Key_Delete:
        OnDelete();
        return true;
    }
    return false;
}
int CBrowseDialog2::Create() {
    _topDirPrefix = TempFolderPath;
    setWindowTitle(QtPath(Title));
    QtNormalizeDialogSize(*this);
    const HRESULT res = Reload(TempFolderPath, UString());
    if (res != S_OK)
        MessageBox_HResError(this, res, TempFolderPath);
    return exec();
}
void MyBrowseForTempFolder(QWidget *owner, std::function<bool(const QUrl &)> opener) {
    FString tempPathF;
    if (!NDir::MyGetTempPath(tempPathF) || tempPathF.IsEmpty()) {
        MessageBox_LastError_path(owner, tempPathF);
        return;
    }
    CBrowseDialog2 dialog(owner, std::move(opener));
    dialog.setProperty("sourceTitleLanguageID", IDM_TEMP_DIR);
    LangString_OnlyFromLangFile(IDM_TEMP_DIR, dialog.Title);
    dialog.Title.Replace(L"...", L"");
    if (dialog.Title.IsEmpty())
        dialog.Title = "Delete Temporary Files";
    // Resolve the OS temp alias once, so persisted paths and the prefix agree.
    const QString temp = QFileInfo(QtPath(fs2us(tempPathF))).canonicalFilePath();
    if (temp.isEmpty()) {
        MessageBox_HResError(owner, E_FAIL, fs2us(tempPathF));
        return;
    }
    dialog.TempFolderPath = (temp + QDir::separator()).toStdWString().c_str();
    dialog.Create();
}
