// Native file/folder creation and inline rename, extracted from PanelOperations.cpp.
// Adapted from 7-Zip 0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "7zip/UI/Common/StdAfx.h"
#include "7zip/UI/FileManager/resource.h"
#include "dialogs/browse/BrowseDialog.h"
#include "dialogs/combo/ComboDialog.h"
#include "filemanager/FormatUtils.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/panels/PanelFolderOperations.h"
#include "fileops/native/correctfspath.h"
#include "localization/LangUtils.h"
#include <utility>

static const UInt32 kParentIndex = (UInt32)-1;

static bool IsCorrectFsName(const UString &name) {
    const UString lastPart = name.Ptr((unsigned)(name.ReverseFind_PathSepar() + 1));
    return !lastPart.IsEqualTo(".") && !lastPart.IsEqualTo("..");
}

static void PrepareCreatedSelection(UString &newName, bool preserveSelection,
                                    CSelectedState &state) {
    const int pos = newName.Find(WCHAR_PATH_SEPARATOR);
    if (pos >= 0)
        newName.DeleteFrom((unsigned)pos);
    if (!preserveSelection)
        state.SelectedNames.Clear();
    state.FocusedName = newName;
    state.FocusedName_Defined = true;
    state.SelectFocused = true;
}

bool CPanel::CorrectFsPath(const UString &path2, UString &result) {
    return ::CorrectFsPath(GetFsPath(), path2, result);
}

static bool PrepareFilesystemName(CPanel &panel, UString &name) {
    if (!panel.IsFSFolder())
        return true;
    UString correctName;
    if (!panel.CorrectFsPath(name, correctName)) {
        panel.MessageBox_Error_HRESULT(E_INVALIDARG);
        return false;
    }
    name = correctName;
    return true;
}

void CPanel::CreateFolder() {
    if (IsHashFolder())
        return;
    if (!CheckBeforeUpdate(IDS_CREATE_FOLDER_ERROR))
        return;
    CDisableTimerProcessing disableTimerProcessing2(*this);
    CSelectedState state;
    SaveSelectedState(state);
    UString newName;
    if (!Dlg_CreateFolder(GetParent(), newName))
        return;
    if (!IsCorrectFsName(newName)) {
        MessageBox_Error_HRESULT(E_INVALIDARG);
        return;
    }
    if (!PrepareFilesystemName(*this, newName))
        return;
    HRESULT res;
    CDisableNotify disableNotify(*this);
    {
        CThreadFolderOperations op(FOLDER_TYPE_CREATE_FOLDER);
        op.FolderOperations = _folderOperations;
        op.Name = newName;
        res = op.DoOperation(*this, LangString(IDS_CREATE_FOLDER),
                             LangString(IDS_CREATE_FOLDER_ERROR));
        // fixed for 9.26: we must refresh always
    }
    if (res == S_OK)
        PrepareCreatedSelection(newName, _mySelectMode, state);
    RefreshTitleAlways();
    RefreshListCtrl(state);
}

void CPanel::CreateFile() {
    if (IsHashFolder())
        return;
    if (!CheckBeforeUpdate(IDS_CREATE_FILE_ERROR))
        return;
    CDisableTimerProcessing disableTimerProcessing2(*this);
    CSelectedState state;
    SaveSelectedState(state);
    CComboDialog dlg(ToQString(LangString(IDS_CREATE_FILE)),
                     ToQString(LangString(IDS_CREATE_FILE_NAME)),
                     ToQString(LangString(IDS_CREATE_FILE_DEFAULT_NAME)), GetParent());
    dlg.BindLanguage(IDS_CREATE_FILE, IDS_CREATE_FILE_NAME);
    if (dlg.exec() != QDialog::Accepted)
        return;
    CDisableNotify disableNotify(*this);
    UString newName = dlg.Value;
    if (!PrepareFilesystemName(*this, newName))
        return;
    const HRESULT result = _folderOperations->CreateFile(newName, NULL);
    if (result != S_OK) {
        MessageBox_Error_HRESULT_Caption(result, LangString(IDS_CREATE_FILE_ERROR));
        return;
    }
    PrepareCreatedSelection(newName, _mySelectMode, state);
    RefreshListCtrl(state);
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

HRESULT CPanel::OnEndLabelEdit(UInt32 realIndex, const UString &enteredName) {
    CDisableTimerProcessing disableTimerProcessing2(*this);
    if (!CheckBeforeUpdate(IDS_ERROR_RENAMING))
        return E_ABORT;
    if (!IsCorrectFsName(enteredName)) {
        MessageBox_Error_HRESULT(E_INVALIDARG);
        return E_INVALIDARG;
    }
    UString newName = enteredName;
    if (!PrepareFilesystemName(*this, newName))
        return E_INVALIDARG;
    CSelectedState state;
    SaveSelectedState(state);
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
    if (result == S_OK)
        state.FocusedName = prefix + newName;
    else
        state.FocusedName = prefix + oldName;
    state.FocusedName_Defined = true;
    state.SelectFocused = true;
    _dontShowMode = true;
    // Qt already queued the editor completion; its publisher takes this source
    // reload state instead of posting a second native list message.
    _refreshSelection = std::move(state);
    return result;
}
