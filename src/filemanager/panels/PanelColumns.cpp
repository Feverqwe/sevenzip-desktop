// Column policy extracted from 7-Zip CPP/7zip/UI/FileManager/PanelItems.cpp.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Native catalog/order/visibility/settings algorithms; Qt projects the result.
#include "7zip/PropID.h"
#include "filemanager/PropertyName.h"
#include "filemanager/panels/Panel.h"
#include "settings/ViewSettings.h"

static bool GetColumnVisible(PROPID propID, bool isFsFolder) {
    if (isFsFolder) {
        switch (propID) {
        case kpidATime:
        case kpidChangeTime:
        case kpidAttrib:
        case kpidPackSize:
        case kpidINode:
        case kpidLinks:
        case kpidNtReparse:
            return false;
        }
    }
    return true;
}

static unsigned GetColumnWidth(PROPID propID, VARTYPE /* varType */) {
    switch (propID) {
    case kpidName:
        return 160;
    }
    return 100;
}

Qt::Alignment GetColumnAlign(PROPID propID, VARTYPE varType) {
    switch (propID) {
    case kpidCTime:
    case kpidATime:
    case kpidMTime:
    case kpidChangeTime:
        return Qt::AlignLeft | Qt::AlignVCenter;
    }
    switch (varType) {
    case VT_UI1:
    case VT_I2:
    case VT_UI2:
    case VT_I4:
    case VT_INT:
    case VT_UI4:
    case VT_UINT:
    case VT_I8:
    case VT_UI8:
    case VT_BOOL:
        return Qt::AlignRight | Qt::AlignVCenter;
    case VT_EMPTY:
    case VT_I1:
    case VT_FILETIME:
    case VT_BSTR:
        return Qt::AlignLeft | Qt::AlignVCenter;
    default:
        return Qt::AlignHCenter | Qt::AlignVCenter;
    }
}

static int ItemProperty_Compare_NameFirst(void *const *a1, void *const *a2, void * /* param */) {
    return (*(*((const CPropColumn *const *)a1)))
        .Compare_NameFirst(*(*((const CPropColumn *const *)a2)));
}

HRESULT CPanel::InitColumns() {
    SaveListViewInfo(_visibleColumns);
    _selectedStatusVector.Clear();
    if (!_folder)
        return E_FAIL;
    const UString oldType = _typeIDString;
    _typeIDString = GetFolderTypeID();
    if (!_needSaveInfo || _typeIDString != oldType)
        _listViewInfo.Read(_typeIDString, _settingsPath);
    _ascending = _listViewInfo.Ascending;
    _columns.Clear();
    const bool isFsFolder = IsFSFolder() || IsAltStreamsFolder();
    {
        UInt32 numProps = 0;
        _folder->GetNumberOfProperties(&numProps);
        for (UInt32 i = 0; i < numProps; i++) {
            CMyComBSTR name;
            PROPID propID;
            VARTYPE varType;
            const HRESULT res = _folder->GetPropertyInfo(i, &name, &propID, &varType);
            if (res != S_OK || propID == kpidIsDir)
                continue;
            CPropColumn prop;
            prop.Type = varType;
            prop.ID = propID;
            prop.Name = GetNameOfProperty(propID, name);
            prop.Order = -1;
            prop.IsVisible = GetColumnVisible(propID, isFsFolder);
            prop.Width = GetColumnWidth(propID, varType);
            prop.IsRawProp = false;
            _columns.Add(prop);
        }
    }
    if (_folderRawProps) {
        UInt32 numProps = 0;
        _folderRawProps->GetNumRawProps(&numProps);
        for (UInt32 i = 0; i < numProps; i++) {
            CMyComBSTR name;
            PROPID propID;
            if (_folderRawProps->GetRawPropInfo(i, &name, &propID) != S_OK)
                continue;
            CPropColumn prop;
            prop.Type = VT_EMPTY;
            prop.ID = propID;
            prop.Name = GetNameOfProperty(propID, name);
            prop.Order = -1;
            prop.IsVisible = GetColumnVisible(propID, isFsFolder);
            prop.Width = GetColumnWidth(propID, VT_BSTR);
            prop.IsRawProp = true;
            _columns.Add(prop);
        }
    }
    unsigned order = 0;
    unsigned i;
    for (i = 0; i < _listViewInfo.Columns.Size(); i++) {
        const CColumnInfo &columnInfo = _listViewInfo.Columns[i];
        const int index = _columns.FindItem_for_PropID(columnInfo.PropID);
        if (index >= 0) {
            CPropColumn &item = _columns[index];
            if (item.Order >= 0)
                continue;
            bool isVisible = columnInfo.IsVisible;
            if (columnInfo.PropID == kpidName)
                isVisible = true;
            item.IsVisible = isVisible;
            item.Width = columnInfo.Width;
            if (isVisible)
                item.Order = (int)(order++);
        }
    }
    for (i = 0; i < _columns.Size(); i++) {
        CPropColumn &item = _columns[i];
        if (item.IsVisible && item.Order < 0)
            item.Order = (int)(order++);
    }
    for (i = 0; i < _columns.Size(); i++) {
        CPropColumn &item = _columns[i];
        if (item.Order < 0)
            item.Order = (int)(order++);
    }
    if (_listViewInfo.IsLoaded)
        _sortID = _listViewInfo.SortID;
    else {
        _sortID = 0;
        if (IsFSFolder() || IsAltStreamsFolder() || IsArcFolder())
            _sortID = kpidName;
    }
    CPropColumns newColumns;
    for (i = 0; i < _columns.Size(); i++) {
        const CPropColumn &prop = _columns[i];
        if (prop.IsVisible)
            newColumns.Add(prop);
    }
    newColumns.Sort(ItemProperty_Compare_NameFirst, NULL);
    if (newColumns.IsEqualTo(_visibleColumns))
        return S_OK;
    for (;;) {
        const unsigned numColumns = _visibleColumns.Size();
        if (numColumns == 0)
            break;
        DeleteColumn(numColumns - 1);
    }
    FOR_VECTOR(columnIndex, newColumns)
    AddColumn(newColumns[columnIndex]);
    _needSaveInfo = true;
    return S_OK;
}

bool CPanel::GetColumnOrder(CIntArr &columns) const {
    // InitColumns' SetColumnOrderArray phase is deferred until Qt has created
    // its physical sections. Source logical indices remain name-first.
    columns.Alloc(_visibleColumns.Size());
    unsigned i;
    for (i = 0; i < _visibleColumns.Size(); i++)
        columns[i] = -1;
    bool orderError = false;
    for (i = 0; i < _visibleColumns.Size(); i++) {
        const CPropColumn &prop = _visibleColumns[i];
        if (prop.Order < (int)_visibleColumns.Size() && columns[prop.Order] == -1)
            columns[prop.Order] = (int)i;
        else
            orderError = true;
    }
    return !orderError;
}

void CPanel::SaveListViewInfo(const CPropColumns &visibleColumns) {
    if (!_needSaveInfo)
        return;

    _visibleColumns = visibleColumns;

    CListViewInfo viewInfo;
    const PROPID sortPropID = _sortID;
    CPropColumns sortedProperties = _visibleColumns;
    sortedProperties.Sort();
    FOR_VECTOR(i, sortedProperties) {
        const CPropColumn &prop = sortedProperties[i];
        CColumnInfo columnInfo;
        columnInfo.IsVisible = prop.IsVisible;
        columnInfo.PropID = prop.ID;
        columnInfo.Width = prop.Width;
        viewInfo.Columns.Add(columnInfo);
    }
    FOR_VECTOR(i, _columns) {
        const CPropColumn &prop = _columns[i];
        if (sortedProperties.FindItem_for_PropID(prop.ID) < 0) {
            CColumnInfo columnInfo;
            columnInfo.IsVisible = false;
            columnInfo.PropID = prop.ID;
            columnInfo.Width = prop.Width;
            viewInfo.Columns.Add(columnInfo);
        }
    }
    viewInfo.SortID = sortPropID;
    viewInfo.Ascending = _ascending;
    viewInfo.IsLoaded = true;
    if (!_listViewInfo.IsEqual(viewInfo)) {
        viewInfo.Save(_typeIDString, _settingsPath);
        _listViewInfo = viewInfo;
    }
}

void CPanel::DeleteColumn(unsigned index) { _visibleColumns.Delete(index); }

void CPanel::AddColumn(const CPropColumn &prop) { _visibleColumns.Add(prop); }

void CPanel::ShowColumnsContextMenu(unsigned index) {
    // Qt cannot select the disabled first entry or an out-of-range command.
    if (index == 0 || index >= _columns.Size())
        return;
    CPropColumn &prop = _columns[index];
    prop.IsVisible = !prop.IsVisible;
    if (prop.IsVisible) {
        prop.Order = (int)_visibleColumns.Size();
        AddColumn(prop);
    } else {
        const int visibleIndex = _visibleColumns.FindItem_for_PropID(prop.ID);
        if (visibleIndex >= 0) {
            if (_sortID == prop.ID) {
                _sortID = kpidName;
                _ascending = true;
            }
            DeleteColumn((unsigned)visibleIndex);
        }
    }
}
