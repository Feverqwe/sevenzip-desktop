// PanelSelect.cpp — adapted from 7-Zip CPP/7zip/UI/FileManager/PanelSelect.cpp
// at 0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17.
// Copyright (C) 1999-2026 Igor Pavlov. GNU LGPL 2.1 or later.

#include "filemanager/panels/Panel.h"

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
    const bool notify = _enableItemChangeNotify;
    _enableItemChangeNotify = false;
    const int numItems = _selectionList.GetItemCount();
    for (int i = 0; i < numItems; ++i) {
        const unsigned realIndex = GetRealItemIndex(i);
        if (realIndex < _selectedStatusVector.Size())
            _selectionList.SetItemState_Selected(i, _selectedStatusVector[realIndex]);
    }
    _enableItemChangeNotify = notify;
    _selectionList.Invalidate();
}

bool CPanel::IsItemMarked(UInt32 index) const {
    return index != kParentIndex && index < _selectedStatusVector.Size() &&
           _selectedStatusVector[index];
}

void CPanel::SelectAll(bool selectMode) {
    EnsureSelectionVectorSize();
    FOR_VECTOR(i, _selectedStatusVector)
    _selectedStatusVector[i] = false;
    if (selectMode) {
        for (int row = 0; row < _selectionList.GetItemCount(); ++row) {
            const unsigned index = GetRealItemIndex(row);
            if (index < _selectedStatusVector.Size())
                _selectedStatusVector[index] = true;
        }
    }
    UpdateSelection();
}

void CPanel::KillSelection() { SelectAll(false); }
