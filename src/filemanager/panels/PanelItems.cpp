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
#include "archive/operations/ExtractingFilePath.h"
#include "filemanager/SysIconUtils.h"
#include "filemanager/panels/Panel.h"
#include <QScopedValueRollback>

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

HRESULT CPanel::LoadListItems(bool itemsLoaded, HRESULT loadedResult) {
    return _listLoadResult = LoadListItemsImpl(itemsLoaded, loadedResult);
}

HRESULT CPanel::LoadListItemsImpl(bool itemsLoaded, HRESULT loadedResult) {
    _selectionList.SetItemState_DropHighlighted(m_DropHighlighted_SelectionIndex, false);
    m_DropHighlighted_SelectionIndex = -1;
    m_DropHighlighted_SubFolderName.Empty();
    if (!_folder)
        return itemsLoaded ? loadedResult : S_OK;
    // The native early-return path has cleared rows/marks, with no count query.
    _loadedItemCount = 0;
    _dontShowMode = false;
    CDisableTimerProcessing timerProcessing(*this);
    CDisableNotify disableNotify(*this);
    DeleteListItems();
    _selectedStatusVector.Clear();
    _startGroupSelect = 0;
    _selectionIsDefined = false;
    if (!itemsLoaded) {
        CMyComPtr<IFolderSetFlatMode> folderSetFlatMode;
        _folder.QueryInterface(IID_IFolderSetFlatMode, &folderSetFlatMode);
        if (folderSetFlatMode)
            folderSetFlatMode->SetFlatMode(BoolToInt(GetFlatMode()));
    }
    _isDirVector.Clear();
    _itemIconIndices.Clear();
    _itemSystemIcons.Clear();
    IFolderFolder *folder = _folder;
    if (!itemsLoaded) {
        RINOK(_folder->LoadItems())
    } else {
        RINOK(loadedResult)
    }
    RINOK(InitColumns())
    UInt32 numItems = 0;
    _folder->GetNumberOfItems(&numItems);
    {
        NWindows::NCOM::CPropVariant prop;
        _isDirVector.ClearAndSetSize(numItems);
        bool *vec = _isDirVector.NonConstData();
        HRESULT hres = S_OK;
        unsigned i;
        for (i = 0; i < numItems; i++) {
            hres = folder->GetProperty(i, kpidIsDir, &prop);
            if (hres != S_OK)
                break;
            bool v = false;
            if (prop.vt == VT_BOOL)
                v = VARIANT_BOOLToBool(prop.boolVal);
            else if (prop.vt != VT_EMPTY)
                break;
            vec[i] = v;
        }
        if (i != numItems) {
            _isDirVector.Clear();
            if (hres == S_OK)
                hres = E_FAIL;
        }
        RINOK(hres)
    }
    // Native insertion adds one mark per source item before publishing its row.
    // Qt publishes rows later; prepare the unselected vector at this boundary.
    _selectedStatusVector.ClearAndReserve(numItems);
    for (UInt32 i = 0; i < numItems; ++i)
        _selectedStatusVector.AddInReserved(false);
    if (!IsFSFolder()) {
        CMyComPtr<IGetFolderArcProps> getFolderArcProps;
        _folder.QueryInterface(IID_IGetFolderArcProps, &getFolderArcProps);
        _thereAreDeletedItems = false;
        if (getFolderArcProps) {
            CMyComPtr<IFolderArcProps> arcProps;
            getFolderArcProps->GetFolderArcProps(&arcProps);
            if (arcProps) {
                UInt32 numLevels;
                if (arcProps->GetArcNumLevels(&numLevels) != S_OK)
                    numLevels = 0;
                NWindows::NCOM::CPropVariant prop;
                if (arcProps->GetArcProp(numLevels - 1, kpidIsDeleted, &prop) == S_OK)
                    if (prop.vt == VT_BOOL && VARIANT_BOOLToBool(prop.boolVal))
                        _thereAreDeletedItems = true;
            }
        }
    }
    _folderGetSystemIconIndex.Release();
    if (!Is_Slow_Icon_Folder() || _showRealFileIcons)
        _folder.QueryInterface(IID_IFolderGetSystemIconIndex, &_folderGetSystemIconIndex);
    _iconIsFSDrivesFolder = IsFSDrivesFolder();
    _iconIsArcFolder = IsArcFolder();
    _itemIconIndices.ClearAndSetSize(numItems);
    _itemSystemIcons.ClearAndSetSize(numItems);
    for (UInt32 i = 0; i < numItems; ++i) {
        _itemIconIndices[i] = -1;
        _itemSystemIcons[i] = false;
    }
    _loadedItemCount = numItems;
    return S_OK;
}

// Extracted source RefreshListCtrl icon phase. The GUI transport calls this
// inside its existing bounded row-publication loop.
HRESULT CPanel::LoadItemIcon(UInt32 i) {
    const wchar_t *name = nullptr;
    unsigned nameLen = 0;
    UString itemName;
    if (_folderGetItemName)
        _folderGetItemName->GetItemName(i, &name, &nameLen);
    if (!name) {
        itemName = GetItemName(i);
        name = itemName;
    }
    bool defined = false;
    Int32 iconIndex = -1;
    if (_folderGetSystemIconIndex) {
        const HRESULT res = _folderGetSystemIconIndex->GetSystemIconIndex(i, &iconIndex);
        if (res == S_OK)
            defined = iconIndex > 0;
    }
    bool systemIcon = defined;
    if (!defined) {
        UInt32 attrib = 0;
        {
            NWindows::NCOM::CPropVariant prop;
            RINOK(_folder->GetProperty(i, kpidAttrib, &prop))
            if (prop.vt == VT_UI4) {
                attrib = prop.ulVal;
                if (_iconIsArcFolder)
                    if (attrib & 0xF0000000)
                        attrib &= 0x3FFF;
            }
        }
        if (IsItem_Folder(i))
            attrib |= FILE_ATTRIBUTE_DIRECTORY;
        else
            attrib &= ~(UInt32)FILE_ATTRIBUTE_DIRECTORY;
        iconIndex = -1;
        systemIcon = false;
        if (_iconIsFSDrivesFolder) {
            FString fs(us2fs(name));
            fs.Add_PathSepar();
#ifdef _WIN32
            if (Shell_GetFileInfo_SysIconIndex_for_Path_return_HRESULT(fs, attrib, &iconIndex) ==
                S_OK)
                systemIcon = iconIndex >= 0;
#else
            iconIndex = g_Ext_to_Icon_Map.GetPathIconIndex(fs2us(fs), attrib);
#endif
        }
        if (iconIndex < 0)
            iconIndex = g_Ext_to_Icon_Map.GetIconIndex(attrib, name);
    }
    if (iconIndex < 0)
        iconIndex = 0;
    _itemIconIndices[i] = iconIndex;
    _itemSystemIcons[i] = systemIcon;
    return S_OK;
}

HRESULT CPanel::GetItemIcon(unsigned index, QIcon &icon) {
    icon = {};
    RINOK(_listLoadResult)
    if (index >= _itemIconIndices.Size())
        return E_INVALIDARG;
    if (_itemIconIndices[index] < 0) {
        const HRESULT result = LoadItemIcon(index);
        if (result != S_OK)
            return _listLoadResult = result;
    }
#ifdef _WIN32
    if (_itemSystemIcons[index])
        icon = g_Ext_to_Icon_Map.GetSystemIcon(_itemIconIndices[index]);
    else
#endif
        icon = g_Ext_to_Icon_Map.GetIcon(_itemIconIndices[index]);
    return S_OK;
}

HRESULT CPanel::RefreshListCtrl(const CSelectedState &state) {
    _selectionList.SetItemState_DropHighlighted(m_DropHighlighted_SelectionIndex, false);
    m_DropHighlighted_SelectionIndex = -1;
    m_DropHighlighted_SubFolderName.Empty();
    if (!_folder)
        return S_OK;
    if (_reloadPresentation) {
        _refreshSelection = state;
        return _reloadPresentation(state);
    }
    return RestoreLoadedSelection(state);
}

HRESULT CPanel::RestoreLoadedSelection(const CSelectedState &state) {
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
    _startGroupSelect = 0;
    _selectionIsDefined = false;
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
        if (selected && !_mySelectMode)
            _selectionList.SelectItem((int)i);
    }
    disableNotify.SetMemMode_Enable();
    disableNotify.Restore();
    if (_selectionList.GetItemCount() > 0 && cursorIndex >= 0)
        SetFocusedSelectedItem(cursorIndex, state.SelectFocused);
    SetSortRawStatus();
    _selectionList.SortItems(_sortID, _ascending, _isRawSortProp);
    if (cursorIndex < 0 && _selectionList.GetItemCount() > 0) {
        if (focusedPos >= _selectionList.GetItemCount())
            focusedPos = _selectionList.GetItemCount() - 1;
        // Select the initial parent row only in the original showDots mode.
        SetFocusedSelectedItem(focusedPos, showDots && (focusedPos == 0));
    }
    _selectionList.EnsureVisible(_selectionList.GetFocusedItem());
    UpdateSelection();
    Refresh_StatusBar();
    return S_OK;
}

HRESULT CPanel::RefreshListCtrl() {
    CSelectedState state;
    return RefreshListCtrl(state);
}

HRESULT CPanel::RefreshListCtrl_SaveFocused(bool onTimer) {
    CSelectedState state;
    SaveSelectedState(state);
    state.CalledFromTimer = onTimer;
    return RefreshListCtrl(state);
}

void CPanel::OnReload(bool onTimer) {
    const auto completed = [this](HRESULT result) {
        if (result != S_OK)
            MessageBox_Error_HRESULT(result);
    };
    QScopedValueRollback<std::function<void(HRESULT)>> completion(_reloadCompletion, completed);
    const HRESULT result = RefreshListCtrl_SaveFocused(onTimer);
    completed(result);
}

void CPanel::OnTimer() {
    if (!_processTimer)
        return;
    if (!AutoRefresh_Mode)
        return;
    if (!_folder)
        return;
    CMyComPtr<IFolderWasChanged> folderWasChanged;
    _folder.QueryInterface(IID_IFolderWasChanged, &folderWasChanged);
    if (!folderWasChanged)
        return;
    Int32 wasChanged = 0;
    if (folderWasChanged->WasChanged(&wasChanged) != S_OK)
        return;
    if (wasChanged == 0 && !_portableFolderChanged)
        return;
    _portableFolderChanged = false;
    OnReload(true);
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
    if (IsItem_Folder(realIndex))
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
        if (IsItem_Folder(index)) {
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
