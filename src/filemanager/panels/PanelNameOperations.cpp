// Native folder creation and inline rename, extracted from PanelOperations.cpp.
// Adapted from 7-Zip 0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "7zip/UI/Common/StdAfx.h"
#include "7zip/UI/FileManager/resource.h"
#include "Windows/FileName.h"
#include "filemanager/FormatUtils.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/panels/PanelFolderOperations.h"
#include "fileops/native/correctfspath.h"
#include "localization/LangUtils.h"
#include <QDir>
#include <QFileInfo>
#include <utility>

static const UInt32 kParentIndex = (UInt32)-1;

static bool IsCorrectFsName(const UString &name) {
    const UString lastPart = name.Ptr((unsigned)(name.ReverseFind_PathSepar() + 1));
    return !lastPart.IsEqualTo(".") && !lastPart.IsEqualTo("..");
}

static UString FirstPathPart(UString name) {
    const int pos = NWindows::NFile::NName::FindSepar(name);
    if (pos >= 0)
        name.DeleteFrom((unsigned)pos);
    return name;
}

static UString VisibleFilesystemItem(const QString &base, const QString &destination,
                                     bool flat = false) {
    if (base.isEmpty() || destination.isEmpty())
        return {};
    const QString relative = QDir(base).relativeFilePath(destination);
    if (relative == "." || relative == ".." || relative.startsWith("../") ||
        QDir::isAbsolutePath(relative))
        return {};
    const UString path = ToUString(QDir::toNativeSeparators(relative));
    return flat ? path : FirstPathPart(path);
}

static void PrepareCreatedSelection(const CPanel &panel, UString newName, CSelectedState &state) {
    if (panel.IsFSFolder()) {
        const QDir folder(ToQString(panel.GetFsPath()));
        newName = VisibleFilesystemItem(folder.canonicalPath(),
                                        QFileInfo(folder, ToQString(newName)).canonicalFilePath());
        if (newName.IsEmpty())
            return;
    } else
        newName = FirstPathPart(newName);
    state.SelectedNames.Clear();
    state.FocusedName = newName;
    state.FocusedName_Defined = true;
    state.SelectFocused = true;
    state.KeepViewport = false;
}

static UString RenamedItemInView(const CPanel &panel, const UString &relative) {
    if (NWindows::NFile::NName::FindSepar(relative) < 0)
        return relative;
    if (panel.IsArcFolder())
        return panel.GetFlatMode() ? relative : FirstPathPart(relative);
    if (panel.IsFSFolder()) {
        const UString base = panel.GetFsPath();
        const QFileInfo target(ToQString(base + relative));
        // Resolve the containing directory while keeping the renamed leaf,
        // which can be a symbolic link.
        const QString parent = QDir(target.path()).canonicalPath();
        if (!parent.isEmpty())
            return VisibleFilesystemItem(QDir(ToQString(base)).canonicalPath(),
                                         QDir(parent).filePath(target.fileName()),
                                         panel.GetFlatMode());
    }
    return relative;
}

bool CPanel::CorrectFsPath(const UString &path2, UString &result) {
    return ::CorrectFsPath(GetFsPath(), path2, result);
}

static bool CorrectItemPath(CPanel &panel, const UString &path, UString &result) {
    if (!panel.CorrectFsPath(path, result) || result.IsEmpty())
        return false;
    return !HasEmptyWindowsPathComponent(
        path, NWindows::NFile::NName::IsAbsolutePath(result) ? result : panel.GetFsPath() + result);
}

static bool PrepareFilesystemName(CPanel &panel, UString &name) {
    if (!panel.IsFSFolder())
        return true;
    UString correctName;
    if (!CorrectItemPath(panel, name, correctName)) {
        panel.MessageBox_Error_HRESULT(E_INVALIDARG);
        return false;
    }
    name = correctName;
    return true;
}

static void SetPublishedArchive(const CPanel &panel, const CThreadFolderOperations &operation,
                                QString *publishedArchive) {
    const auto &parents = panel.GetParentFolders();
    if (publishedArchive && parents.Size() == 1 && operation.UpdateCallback->Published == true)
        *publishedArchive = ToQString(parents.Front().VirtualPath);
}

HRESULT CPanel::CreateFolder(UString name, QString &error, QString *destinationDirectory,
                             QString *publishedArchive) {
    if (IsHashFolder() || !_folderOperations || IsThereReadOnlyFolder())
        return E_NOTIMPL;
    if (name.IsEmpty() || !IsCorrectFsName(name))
        return E_INVALIDARG;
    if (IsFSFolder()) {
        UString corrected;
        if (!CorrectItemPath(*this, name, corrected))
            return E_INVALIDARG;
        name = corrected;
    }
    QString parentDirectory;
    QString firstMissingParent;
    if (destinationDirectory && IsFSFolder()) {
        parentDirectory = QFileInfo(QDir(ToQString(GetFsPath())), QDir::cleanPath(ToQString(name)))
                              .absolutePath();
        QString ancestor = parentDirectory;
        while (!QFileInfo::exists(ancestor)) {
            const QString parent = QFileInfo(ancestor).absolutePath();
            if (parent == ancestor)
                break;
            firstMissingParent = ancestor;
            ancestor = parent;
        }
    }
    CDisableTimerProcessing disableTimerProcessing2(*this);
    CSelectedState state;
    SaveSelectedState(state);
    state.PreserveAddress = true;
    CDisableNotify disableNotify(*this);
    CThreadFolderOperations op(FOLDER_TYPE_CREATE_FOLDER);
    op.FolderOperations = _folderOperations;
    op.Name = name;
    op.DeferResultReview = true;
    const HRESULT res =
        op.DoOperation(*this, LangString(IDS_CREATE_FOLDER), LangString(IDS_CREATE_FOLDER_ERROR));
    error = ToQString(op.Sync.FinalMessage.ErrorMessage.Message);
    if (FAILED(res) && res != E_ABORT && !op.Sync.Messages.IsEmpty()) {
        QStringList diagnostics;
        FOR_VECTOR(i, op.Sync.Messages)
        diagnostics.append(ToQString(op.Sync.Messages[i]));
        error = diagnostics.join(QLatin1Char('\n'));
    }
    SetPublishedArchive(*this, op, publishedArchive);
    if (res == S_OK)
        PrepareCreatedSelection(*this, name, state);
    if (!parentDirectory.isEmpty() && res == S_OK)
        *destinationDirectory = parentDirectory;
    else if (!firstMissingParent.isEmpty() && QFileInfo(firstMissingParent).isDir())
        *destinationDirectory = QFileInfo(firstMissingParent).absolutePath();
    RefreshTitleAlways();
    RefreshListCtrl(state);
    return res;
}

bool CPanel::OnBeginLabelEdit(int row) {
    const unsigned realIndex = GetRealItemIndex(row);
    if (realIndex == kParentIndex)
        return true;
    if (IsThereReadOnlyFolder())
        return true;
    return false;
}

void CPanel::RenameFile() {
    if (!CheckBeforeUpdate(IDS_ERROR_RENAMING))
        return;
    int index = _selectionList.GetFocusedItem();
    if (index >= 0)
        _selectionList.EditLabel(index);
}

HRESULT CPanel::OnEndLabelEdit(UInt32 realIndex, const UString &enteredName,
                               QString *destinationDirectory, QString *publishedArchive) {
    CDisableTimerProcessing disableTimerProcessing2(*this);
    if (!CheckBeforeUpdate(IDS_ERROR_RENAMING))
        return E_ABORT;
    if (enteredName.IsEmpty() || !IsCorrectFsName(enteredName)) {
        MessageBox_Error_HRESULT(E_INVALIDARG);
        return E_INVALIDARG;
    }
    UString newName = enteredName;
    if (!PrepareFilesystemName(*this, newName))
        return E_INVALIDARG;
    CSelectedState state;
    SaveSelectedState(state);
    state.PreserveAddress = true;
    if (realIndex == kParentIndex)
        return E_INVALIDARG;
    const UString prefix = GetItemPrefix(realIndex);
    const UString oldName = GetItemName(realIndex);
    CDisableNotify disableNotify(*this);
    CThreadFolderOperations op(FOLDER_TYPE_RENAME);
    op.FolderOperations = _folderOperations;
    op.Index = realIndex;
    op.Name = newName;
    const HRESULT result =
        op.DoOperation(*this, LangString(IDS_RENAMING), LangString(IDS_ERROR_RENAMING));
    SetPublishedArchive(*this, op, publishedArchive);
    if (result == S_OK) {
        state.FocusedName = RenamedItemInView(*this, prefix + newName);
        state.SelectFocused = true;
        if (destinationDirectory && IsFSFolder())
            *destinationDirectory =
                QFileInfo(ToQString(GetFsPath() + prefix + newName)).absolutePath();
    } else
        state.FocusedName = prefix + oldName;
    state.FocusedName_Defined = true;
    _dontShowMode = true;
    // Qt already queued the editor completion; its publisher takes this source
    // reload state instead of posting a second native list message.
    _refreshSelection = std::move(state);
    return result;
}
