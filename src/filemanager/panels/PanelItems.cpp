// Adapted from 7-Zip CPP/7zip/UI/FileManager/PanelItems.cpp (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// IFolder/variant property bodies retain upstream control flow. Qt list rows
// use the source load/property stages and a separate Qt row-publication stage.
#include "7zip/PropID.h"
#include "7zip/UI/FileManager/resource.h"
#include "Windows/FileName.h"
#include "Windows/PropVariant.h"
#include "Windows/PropVariantConv.h"
#include "Windows/WinDefs.h"
#include "archive/opening/archivecontainerpath.h"
#include "archive/operations/ExtractingFilePath.h"
#include "filemanager/panels/Panel.h"

static const UInt32 kParentIndex = (UInt32)-1;

UInt32 CPanel::GetFocusedFileIndex() const {
    const int focusedItem = _selectionList.GetFocusedItem();
    if (focusedItem < 0)
        return kParentIndex;
    const unsigned realIndex = GetRealItemIndex(focusedItem);
    if (realIndex == kParentIndex || IsItem_Folder(realIndex))
        return kParentIndex;
    return realIndex;
}

void CPanel::EditItem(bool useEditor) {
    if (!useEditor) {
        CMyComPtr<IFolderCalcItemFullSize> calcItemFullSize;
        _folder.QueryInterface(IID_IFolderCalcItemFullSize, &calcItemFullSize);
        if (calcItemFullSize) {
            bool needRefresh = false;
            CRecordVector<UInt32> indices;
            Get_ItemIndices_Operated(indices);
            FOR_VECTOR(i, indices) {
                UInt32 index = indices[i];
                if (IsItem_Folder(index)) {
                    calcItemFullSize->CalcItemFullSize(index, NULL);
                    needRefresh = true;
                }
            }
            if (needRefresh) {
                InvalidateList();
                return;
            }
        }
    }
    const int focusedItem = _selectionList.GetFocusedItem();
    if (focusedItem < 0)
        return;
    const unsigned realIndex = GetRealItemIndex(focusedItem);
    if (realIndex == kParentIndex)
        return;
    if (!IsItem_Folder(realIndex))
        EditItem(realIndex, useEditor);
}

unsigned CPanel::GetRealItemIndex(int indexInListView) const {
    quintptr param;
    if (!_selectionList.GetItemParam((unsigned)indexInListView, param))
        throw 1;
    return (unsigned)param;
}

UInt32 CPanel::GetFolderItemCount() const {
    if (_loadedItemCount)
        return *_loadedItemCount;
    if (!_folder)
        return 0;
    UInt32 numItems;
    if (_folder->GetNumberOfItems(&numItems) != S_OK)
        throw 2723400;
    return numItems;
}

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
    if (indices.IsEmpty() || (indices.Size() == 1 && indices[0] == (UInt32)(Int32)-1))
        Get_ItemIndices_All(indices);
}

void CPanel::SaveSelectedState(CSelectedState &s) {
    s.FocusedName_Defined = false;
    s.FocusedName.Empty();
    s.SelectFocused = true;
    s.SelectedNames.Clear();
    s.FocusedItem = _selectionList.GetFocusedItem();
    {
        if (s.FocusedItem >= 0) {
            const unsigned realIndex = GetRealItemIndex(s.FocusedItem);
            if (realIndex != kParentIndex) {
                s.FocusedName = GetItemRelPath(realIndex);
                s.FocusedName_Defined = true;
                s.SelectFocused = _selectionList.IsItemSelected(s.FocusedItem);
            }
        }
    }
    GetSelectedNames(s.SelectedNames);
}

void CPanel::SetFocusedSelectedItem(int index, bool select) {
    _selectionList.SetFocusedItem(index);
    if (select) {
        _selectionList.SelectItem(index);
        const unsigned realIndex = GetRealItemIndex(index);
        if (!_mySelectMode && realIndex != kParentIndex)
            _selectedStatusVector[realIndex] = true;
    }
}

UInt64 CPanel::GetItem_UInt64Prop(UInt32 itemIndex, UInt32 propID) const {
    if (itemIndex == kParentIndex)
        return 0;
    NWindows::NCOM::CPropVariant prop;
    if (_folder->GetProperty(itemIndex, propID, &prop) != S_OK)
        throw 2723400;
    UInt64 val = 0;
    if (ConvertPropVariantToUInt64(prop, val))
        return val;
    return 0;
}

UInt64 CPanel::GetItemSize(UInt32 itemIndex) const {
    if (itemIndex == kParentIndex)
        return 0;
    if (_folderGetItemName)
        return _folderGetItemName->GetItemSize(itemIndex);
    return GetItem_UInt64Prop(itemIndex, kpidSize);
}

bool CPanel::GetItem_BoolProp(UInt32 itemIndex, PROPID propID) const {
    NWindows::NCOM::CPropVariant prop;
    if (_folder->GetProperty(itemIndex, propID, &prop) != S_OK)
        throw 2723400;
    if (prop.vt == VT_BOOL)
        return VARIANT_BOOLToBool(prop.boolVal);
    if (prop.vt == VT_EMPTY)
        return false;
    throw 2723401;
}

bool CPanel::IsItem_Deleted(unsigned itemIndex) const {
    if (itemIndex == kParentIndex)
        return false;
    return GetItem_BoolProp(itemIndex, kpidIsDeleted);
}

bool CPanel::IsItem_AltStream(unsigned itemIndex) const {
    if (itemIndex == kParentIndex)
        return false;
    return GetItem_BoolProp(itemIndex, kpidIsAltStream);
}

void CPanel::OpenFocusedItemAsInternal(const wchar_t *type) {
    const int focusedItem = _selectionList.GetFocusedItem();
    if (focusedItem < 0)
        return;
    const unsigned realIndex = GetRealItemIndex(focusedItem);
    if (IsItem_Folder(realIndex) && !(IsFSFolder() && IsSparseBundleName(GetItemName(realIndex))))
        OpenFolder(realIndex);
    else
        OpenItem(realIndex, true, false, type);
}

void CPanel::OpenSelectedItems(bool tryInternal) {
    CRecordVector<UInt32> indices;
    Get_ItemIndices_Operated(indices);
    if (indices.Size() > 20) {
        MessageBox_Error_LangID(IDS_TOO_MANY_ITEMS);
        return;
    }
    const int focusedItem = _selectionList.GetFocusedItem();
    if (focusedItem >= 0) {
        const unsigned realIndex = GetRealItemIndex(focusedItem);
        if (realIndex == kParentIndex && (tryInternal || indices.Size() == 0) &&
            _selectionList.IsItemSelected(focusedItem))
            indices.Insert(0, realIndex);
    }
    FOR_VECTOR(i, indices) {
        UInt32 index = indices[i];
        if (IsItem_Folder(index) &&
            !(tryInternal && IsFSFolder() && IsSparseBundleName(GetItemName(index)))) {
            if (tryInternal) {
                OpenFolder(index);
                break;
            } else
                OpenFolderExternal(index);
        } else
            OpenItem(index, (tryInternal && indices.Size() == 1), true);
    }
}

bool CPanel::IsItem_Folder(UInt32 itemIndex) const {
    if (itemIndex == kParentIndex)
        return true;
    if (itemIndex < _isDirVector.Size())
        return _isDirVector[itemIndex];
    return GetItem_BoolProp(itemIndex, kpidIsDir);
}

UString CPanel::GetItemName(unsigned itemIndex) const {
    if (itemIndex == kParentIndex)
        return L"..";
    NWindows::NCOM::CPropVariant prop;
    if (_folder->GetProperty(itemIndex, kpidName, &prop) != S_OK)
        throw 2723400;
    if (prop.vt != VT_BSTR)
        throw 2723401;
    return prop.bstrVal;
}

UString CPanel::GetItemName_for_Copy(unsigned itemIndex) const {
    if (itemIndex == kParentIndex)
        return L"..";
    UString s;
    {
        NWindows::NCOM::CPropVariant prop;
        if (_folder->GetProperty(itemIndex, kpidOutName, &prop) == S_OK) {
            if (prop.vt == VT_BSTR)
                s = prop.bstrVal;
            else if (prop.vt != VT_EMPTY)
                throw 2723401;
        }
        if (s.IsEmpty())
            s = GetItemName(itemIndex);
    }
    return Get_Correct_FsFile_Name(s);
}

UString CPanel::GetItemPrefix(unsigned itemIndex) const {
    UString prefix;
    if (itemIndex != kParentIndex) {
        NWindows::NCOM::CPropVariant prop;
        if (_folder->GetProperty(itemIndex, kpidPrefix, &prop) != S_OK)
            throw 2723400;
        if (prop.vt == VT_BSTR)
            prefix.SetFromBstr(prop.bstrVal);
    }
    return prefix;
}

UString CPanel::GetItemRelPath(unsigned itemIndex) const {
    return GetItemPrefix(itemIndex) + GetItemName(itemIndex);
}

UString CPanel::GetItemRelPath2(unsigned itemIndex) const {
    UString s = GetItemRelPath(itemIndex);
#if defined(_WIN32) && !defined(UNDER_CE)
    if (s.Len() == 2 && NWindows::NFile::NName::IsDrivePath2(s)) {
        if (IsFSDrivesFolder() && !IsDeviceDrivesPrefix())
            s.Add_PathSepar();
    }
#endif
    return s;
}

void CPanel::Add_ItemRelPath2_To_String(unsigned itemIndex, UString &s) const {
    if (itemIndex == kParentIndex) {
        s += "..";
        return;
    }
    const unsigned start = s.Len();
    NWindows::NCOM::CPropVariant prop;
    if (_folder->GetProperty(itemIndex, kpidPrefix, &prop) != S_OK)
        throw 2723400;
    if (prop.vt == VT_BSTR)
        s += prop.bstrVal;
    const wchar_t *name = NULL;
    unsigned nameLen = 0;
    if (_folderGetItemName)
        _folderGetItemName->GetItemName(itemIndex, &name, &nameLen);
    if (name)
        s += name;
    else {
        prop.Clear();
        if (_folder->GetProperty(itemIndex, kpidName, &prop) != S_OK)
            throw 2723400;
        if (prop.vt != VT_BSTR)
            throw 2723401;
        s += prop.bstrVal;
    }
#if defined(_WIN32) && !defined(UNDER_CE)
    if (s.Len() - start == 2 && NWindows::NFile::NName::IsDrivePath2(s.Ptr(start))) {
        if (IsFSDrivesFolder() && !IsDeviceDrivesPrefix())
            s.Add_PathSepar();
    }
#else
    (void)start;
#endif
}

UString CPanel::GetItemFullPath(unsigned itemIndex) const {
    return GetFsPath() + GetItemRelPath2(itemIndex);
}
