// PanelSelect.cpp — adapted from 7-Zip CPP/7zip/UI/FileManager/PanelSelect.cpp
// at 0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17.
// Copyright (C) 1999-2026 Igor Pavlov. GNU LGPL 2.1 or later.

#include "7zip/UI/FileManager/resource.h"
#include "dialogs/combo/ComboDialog.h"
#include "filemanager/panels/Panel.h"
#include "fileops/patterns/Wildcard.h"
#include "localization/LangUtils.h"

static const UInt32 kParentIndex = (UInt32)-1;

void CPanel::EnsureSelectionVectorSize() {
    // Qt can publish bound rows without calling RefreshListCtrl on initial load.
    const UInt32 numItems = GetFolderItemCount();
    if (_selectedStatusVector.Size() > numItems)
        _selectedStatusVector.DeleteFrom(numItems);
    while (_selectedStatusVector.Size() < numItems)
        _selectedStatusVector.Add(false);
}

void CPanel::UpdateSelection() {
    EnsureSelectionVectorSize();
    if (!_mySelectMode) {
        bool enableTemp = _enableItemChangeNotify;
        _enableItemChangeNotify = false;
        int numItems = _selectionList.GetItemCount();
        for (int i = 0; i < numItems; i++) {
            const unsigned realIndex = GetRealItemIndex(i);
            if (realIndex != kParentIndex && realIndex < _selectedStatusVector.Size())
                _selectionList.SetItemState_Selected(i, _selectedStatusVector[realIndex]);
        }
        _enableItemChangeNotify = enableTemp;
    }
    // Qt exposes alternative marks as model roles instead of custom-draw state.
    const int numItems = _selectionList.GetItemCount();
    for (int i = 0; i < numItems; i++) {
        const unsigned realIndex = GetRealItemIndex(i);
        if (realIndex != kParentIndex && realIndex < _selectedStatusVector.Size()) {
            _selectionList.SetItemMarked(i, _mySelectMode && _selectedStatusVector[realIndex]);
        } else
            _selectionList.SetItemMarked(i, false);
    }
    _selectionList.Invalidate();
}

bool CPanel::IsItemMarked(UInt32 index) const {
    return index != kParentIndex && index < _selectedStatusVector.Size() &&
           _selectedStatusVector[index];
}

void CPanel::SetAlternativeSelection(bool enabled) {
    if (_mySelectMode == enabled)
        return;
    _mySelectMode = enabled;
    _selectionIsDefined = false;
    _startGroupSelect = MyMax(0, _selectionList.GetFocusedItem());
    _selectionList.SetSingleSelection(enabled);
    if (enabled && _selectionList.IsBound()) {
        const int focusedItem = _selectionList.GetFocusedItem();
        CDisableNotify disableNotify(*this);
        _selectionList.ClearSelection();
        if (focusedItem >= 0)
            _selectionList.SetItemState_Selected(focusedItem, true);
    }
    UpdateSelection();
}

void CPanel::BeginShiftSelection() {
    _selectionIsDefined = false;
    _prevFocusedItem = _selectionList.GetFocusedItem();
}

void CPanel::OnShiftSelectMessage() {
    if (!_mySelectMode)
        return;
    EnsureSelectionVectorSize();
    const int focusedItem = _selectionList.GetFocusedItem();
    if (focusedItem < 0 || !_selectionIsDefined)
        return;
    const int startItem = MyMin(focusedItem, _prevFocusedItem);
    const int finishItem = MyMax(focusedItem, _prevFocusedItem);
    const int numItems = _selectionList.GetItemCount();
    for (int i = 0; i < numItems; i++) {
        const unsigned realIndex = GetRealItemIndex(i);
        if (realIndex == kParentIndex)
            continue;
        if (i >= startItem && i <= finishItem)
            if (_selectedStatusVector[realIndex] != _selectMark)
                _selectedStatusVector[realIndex] = _selectMark;
    }
    _prevFocusedItem = focusedItem;
    {
        CDisableNotify disableNotify(*this);
        _selectionList.SelectOnly(focusedItem);
    }
    UpdateSelection();
}

void CPanel::OnArrowWithShift() {
    if (!_mySelectMode)
        return;
    EnsureSelectionVectorSize();
    const int focusedItem = _selectionList.GetFocusedItem();
    if (focusedItem < 0)
        return;
    const unsigned realIndex = GetRealItemIndex(focusedItem);
    if (_selectionIsDefined) {
        if (realIndex != kParentIndex)
            _selectedStatusVector[realIndex] = _selectMark;
    } else {
        if (realIndex == kParentIndex) {
            _selectionIsDefined = true;
            _selectMark = true;
        } else {
            _selectionIsDefined = true;
            _selectMark = !_selectedStatusVector[realIndex];
            _selectedStatusVector[realIndex] = _selectMark;
        }
    }
    _prevFocusedItem = focusedItem;
    UpdateSelection();
    // The Qt key adapter calls OnShiftSelectMessage after the view moves focus.
}

void CPanel::OnInsert() {
    EnsureSelectionVectorSize();
    const int focusedItem = _selectionList.GetFocusedItem();
    if (focusedItem < 0)
        return;
    const unsigned realIndex = GetRealItemIndex(focusedItem);
    if (realIndex != kParentIndex) {
        const bool isSelected = !_selectedStatusVector[realIndex];
        _selectedStatusVector[realIndex] = isSelected;
        if (!_mySelectMode)
            _selectionList.SetItemState_Selected(focusedItem, isSelected);
        UpdateSelection();
    }
    const int nextIndex = focusedItem + 1;
    if (nextIndex < _selectionList.GetItemCount()) {
        _selectionList.SetFocusedItem(nextIndex);
        if (_mySelectMode)
            _selectionList.SelectOnly(nextIndex);
        else
            _selectionList.SetItemState_Selected(nextIndex, true);
        _selectionList.EnsureVisible(nextIndex);
    }
}

void CPanel::OnLeftClick(int indexInList, Qt::KeyboardModifiers modifiers) {
    if (indexInList < 0)
        return;
    EnsureSelectionVectorSize();
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        const int focusedIndex = _startGroupSelect;
        if (focusedIndex < 0)
            return;
        const int startItem = MyMin(focusedIndex, indexInList);
        const int finishItem = MyMax(focusedIndex, indexInList);
        const int numItems = _selectionList.GetItemCount();
        for (int i = 0; i < numItems; i++) {
            const unsigned realIndex = GetRealItemIndex(i);
            if (realIndex == kParentIndex)
                continue;
            const bool selected = (i >= startItem && i <= finishItem);
            if (_selectedStatusVector[realIndex] != selected)
                _selectedStatusVector[realIndex] = selected;
        }
    } else {
        _startGroupSelect = indexInList;
        if (modifiers.testFlag(Qt::ControlModifier)) {
            const unsigned realIndex = GetRealItemIndex(indexInList);
            if (realIndex != kParentIndex)
                _selectedStatusVector[realIndex] = !_selectedStatusVector[realIndex];
        }
    }
    if (_mySelectMode) {
        CDisableNotify disableNotify(*this);
        _selectionList.SetFocusedItem(indexInList);
        _selectionList.SelectOnly(indexInList);
    }
    UpdateSelection();
}

void CPanel::SelectSpec(bool selectMode) {
    const UInt32 titleID = selectMode ? IDS_SELECT : IDS_DESELECT;
    CComboDialog dlg(ToQString(LangString(titleID)), ToQString(LangString(IDS_SELECT_MASK)),
                     QStringLiteral("*"), GetParent());
    dlg.BindLanguage(titleID, IDS_SELECT_MASK);
    if (dlg.exec() != QDialog::Accepted)
        return;
    SelectSpec(dlg.Value, selectMode);
}

// The explicit-mask integration entry shares the original source loop.
void CPanel::SelectSpec(const UString &mask, bool selectMode) {
    FOR_VECTOR(i, _selectedStatusVector)
    if (DoesWildcardMatchName(mask, GetItemName(i)))
        _selectedStatusVector[i] = selectMode;
    UpdateSelection();
}

void CPanel::SelectByType(bool selectMode) {
    const int focusedItem = _selectionList.GetFocusedItem();
    if (focusedItem < 0)
        return;
    const unsigned realIndex = GetRealItemIndex(focusedItem);
    if (realIndex == kParentIndex)
        return;
    UString name = GetItemName(realIndex);
    bool isItemFolder = IsItem_Folder(realIndex);

    if (isItemFolder) {
        FOR_VECTOR(i, _selectedStatusVector)
        if (IsItem_Folder(i) == isItemFolder)
            _selectedStatusVector[i] = selectMode;
    } else {
        int pos = name.ReverseFind_Dot();
        if (pos < 0) {
            FOR_VECTOR(i, _selectedStatusVector)
            if (IsItem_Folder(i) == isItemFolder && GetItemName(i).ReverseFind_Dot() < 0)
                _selectedStatusVector[i] = selectMode;
        } else {
            UString mask('*');
            mask += name.Ptr((unsigned)pos);
            FOR_VECTOR(i, _selectedStatusVector)
            if (IsItem_Folder(i) == isItemFolder && DoesWildcardMatchName(mask, GetItemName(i)))
                _selectedStatusVector[i] = selectMode;
        }
    }

    UpdateSelection();
}

void CPanel::SelectAll(bool selectMode) {
    FOR_VECTOR(i, _selectedStatusVector)
    _selectedStatusVector[i] = selectMode;
    UpdateSelection();
}

void CPanel::InvertSelection() {
    FOR_VECTOR(i, _selectedStatusVector)
    _selectedStatusVector[i] = !_selectedStatusVector[i];
    UpdateSelection();
}

void CPanel::KillSelection() { SelectAll(false); }
