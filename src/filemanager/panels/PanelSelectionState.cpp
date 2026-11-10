// Selection state adapted from 7-Zip CPP/7zip/UI/FileManager/PanelItems.cpp.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "7zip/PropID.h"
#include "Windows/FileName.h"
#include "Windows/PropVariant.h"
#include "Windows/WinDefs.h"
#include "filemanager/panels/Panel.h"
#include <algorithm>

static const UInt32 kParentIndex = (UInt32)-1;

void CPanel::Get_ItemIndices_Selected(CRecordVector<UInt32> &indices) const {
    indices.Clear();
    const bool *v = _selectedStatusVector.ConstData();
    const unsigned size = _selectedStatusVector.Size();
    for (unsigned i = 0; i < size; i++)
        if (v[i])
            indices.Add(i);
}

void CPanel::GetSelectedNames(UStringVector &selectedNames) {
    CRecordVector<UInt32> indices;
    Get_ItemIndices_Selected(indices);
    selectedNames.ClearAndReserve(indices.Size());
    FOR_VECTOR(i, indices)
    selectedNames.AddInReserved(GetItemRelPath(indices[i]));
    selectedNames.Sort();
}

void CPanel::Get_ItemIndices_Operated(CRecordVector<UInt32> &indices) const {
    Get_ItemIndices_Selected(indices);
    if (!indices.IsEmpty())
        return;
    if (_selectionList.GetSelectedCount() == 0)
        return;
    const int focusedItem = _selectionList.GetFocusedItem();
    if (focusedItem >= 0) {
        if (_selectionList.IsItemSelected(focusedItem)) {
            const unsigned realIndex = GetRealItemIndex(focusedItem);
            if (realIndex != kParentIndex)
                indices.Add(realIndex);
        }
    }
}

void CPanel::Get_ItemIndices_All(CRecordVector<UInt32> &indices) const {
    indices.Clear();
    UInt32 numItems;
    if (_folder->GetNumberOfItems(&numItems) != S_OK)
        return;
    indices.ClearAndSetSize(numItems);
    UInt32 *vec = indices.NonConstData();
    for (UInt32 i = 0; i < numItems; i++)
        vec[i] = i;
}

void CPanel::Get_ItemIndices_OperSmart(CRecordVector<UInt32> &indices) const {
    Get_ItemIndices_Operated(indices);
    if (!indices.IsEmpty() && (indices.Size() != 1 || indices[0] != kParentIndex))
        return;
    if (!IsFSFolder()) {
        Get_ItemIndices_All(indices);
        return;
    }
    indices.Clear();
    for (int row = 0; row < _selectionList.GetItemCount(); ++row) {
        const UInt32 index = GetRealItemIndex(row);
        if (index != kParentIndex)
            indices.Add(index);
    }
    if (indices.Size() > 1)
        std::sort(indices.NonConstData(), indices.NonConstData() + indices.Size());
}

void CPanel::SaveSelectedState(CSelectedState &s) {
    s.KeepViewport = true;
    s.FocusedName_Defined = false;
    s.FocusedName.Empty();
    s.SelectFocused = true;
    s.SelectedNames.Clear();
    s.FocusedItem = _selectionList.GetFocusedItem();
    if (s.FocusedItem >= 0) {
        s.FocusedName = GetItemRelPath(GetRealItemIndex(s.FocusedItem));
        s.FocusedName_Defined = true;
        s.SelectFocused = _selectionList.IsItemSelected(s.FocusedItem);
    }
    GetSelectedNames(s.SelectedNames);
}

void CPanel::SetFocusedSelectedItem(int index, bool select, bool keepColumn) {
    _selectionList.SetFocusedItem(index, keepColumn);
    if (select) {
        _selectionList.SelectItem(index);
        const unsigned realIndex = GetRealItemIndex(index);
        if (realIndex != kParentIndex)
            _selectedStatusVector[realIndex] = true;
    }
}

HRESULT CPanel::RestoreLoadedSelection(const CSelectedState &state, bool revealFocused) {
    _selectionList.SetItemState_DropHighlighted(m_DropHighlighted_SelectionIndex, false);
    m_DropHighlighted_SelectionIndex = -1;
    m_DropHighlighted_SubFolderName.Empty();
    if (!_folder)
        return S_OK;
    RINOK(_listLoadResult)
    _dontShowMode = false;
    if (_selectionList.GetItemCount() == 0)
        _refreshSelection = state;
    CDisableTimerProcessing timerProcessing(*this);
    CDisableNotify disableNotify(*this);
    int focusedPos = state.FocusedItem;
    if (focusedPos < 0)
        focusedPos = 0;
    _selectionList.ClearSelection();
    const UInt32 numItems = (UInt32)_selectionList.GetItemCount();
    const UInt32 numFolderItems = GetFolderItemCount();
    const bool showDots = _showDots && !IsRootFolder();
    _selectedStatusVector.ClearAndReserve(numFolderItems);
    for (UInt32 i = 0; i < numFolderItems; ++i)
        _selectedStatusVector.AddInReserved(false);
    int cursorIndex = -1;
    UString relPath, focusedNamePure, selectedName;
    bool needCheckSlash = false;
    if (state.FocusedName_Defined) {
        focusedNamePure = state.FocusedName;
        const int pos = focusedNamePure.ReverseFind_PathSepar();
        if (pos >= 0) {
            focusedNamePure.DeleteFrontal((unsigned)pos + 1);
            needCheckSlash = true;
        }
    }
    if (state.SelectedNames.Size() == 1) {
        selectedName = state.SelectedNames[0];
        const int pos = selectedName.ReverseFind_PathSepar();
        if (pos >= 0) {
            selectedName.DeleteFrontal((unsigned)pos + 1);
            needCheckSlash = true;
        }
    }
    for (UInt32 i = 0; i < numItems; i++) {
        const unsigned realIndex = GetRealItemIndex(int(i));
        if (realIndex == kParentIndex) {
            if (showDots && state.FocusedName == UString(".."))
                cursorIndex = (int)i;
            continue;
        }
        if (realIndex >= numFolderItems)
            throw 1;
        const wchar_t *name = NULL;
        unsigned nameLen = 0;
        UString itemName;
        if (_folderGetItemName)
            _folderGetItemName->GetItemName(realIndex, &name, &nameLen);
        if (!name) {
            itemName = GetItemName(realIndex);
            name = itemName;
        }
        bool selected = false;
        if (state.SelectedNames.Size() > 1 ||
            (state.SelectedNames.Size() == 1 && selectedName == name) ||
            (state.FocusedName_Defined && focusedNamePure == name) ||
            (needCheckSlash && NWindows::NFile::NName::FindSepar(name) >= 0)) {
            relPath.Empty();
            if (GetFlatMode()) {
                const wchar_t *prefix = NULL;
                if (_folderGetItemName) {
                    unsigned prefixLen = 0;
                    _folderGetItemName->GetItemPrefix(realIndex, &prefix, &prefixLen);
                    if (prefix)
                        relPath = prefix;
                }
                if (!prefix) {
                    NWindows::NCOM::CPropVariant prop;
                    if (_folder->GetProperty(realIndex, kpidPrefix, &prop) != S_OK)
                        throw 2723400;
                    if (prop.vt == VT_BSTR)
                        relPath.SetFromBstr(prop.bstrVal);
                }
            }
            relPath += name;
            if (relPath == state.FocusedName)
                cursorIndex = (int)i;
            if (state.SelectedNames.FindInSorted(relPath) != -1)
                selected = true;
        }
        _selectedStatusVector[realIndex] = selected;
    }
    disableNotify.SetMemMode_Enable();
    disableNotify.Restore();
    const bool keepColumn = state.KeepViewport && !revealFocused;
    if (_selectionList.GetItemCount() > 0 && cursorIndex >= 0)
        SetFocusedSelectedItem(cursorIndex, state.SelectFocused, keepColumn);
    SetSortRawStatus();
    _selectionList.SortItems(_sortID, _ascending, _isRawSortProp);
    if (cursorIndex < 0 && _selectionList.GetItemCount() > 0) {
        if (focusedPos >= _selectionList.GetItemCount())
            focusedPos = _selectionList.GetItemCount() - 1;
        // Select the initial parent row only in the original showDots mode.
        SetFocusedSelectedItem(
            focusedPos, showDots && focusedPos == 0 && !state.FocusedName_Defined, keepColumn);
    }
    if (revealFocused)
        _selectionList.EnsureVisible(_selectionList.GetFocusedItem());
    UpdateSelection();
    Refresh_StatusBar();
    return S_OK;
}
