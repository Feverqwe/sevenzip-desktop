// List loading and refresh adapted from 7-Zip UI/FileManager/PanelItems.cpp.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "7zip/PropID.h"
#include "Common/StringConvert.h"
#include "Windows/FileName.h"
#include "Windows/PropVariant.h"
#include "Windows/WinDefs.h"
#include "filemanager/SysIconUtils.h"
#include "filemanager/panels/Panel.h"
#include <QScopedValueRollback>

static const UInt32 kParentIndex = (UInt32)-1;

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
