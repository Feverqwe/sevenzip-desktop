// Adapted from 7-Zip CPP/7zip/UI/FileManager/PanelSort.cpp at 0766b73.
// Copyright (C) 1999-2026 Igor Pavlov. GNU LGPL 2.1 or later.
// Source property comparison and panel sort state; Qt carries source item indices.
#include "7zip/PropID.h"
#include "Windows/FileIO.h"
#include "Windows/PropVariant.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/sorting/filenamesort.h"
#include "platform/cpu/CpuArch.h"
#include <optional>

static int CompareFileNames_Le16(const Byte *s1, unsigned size1, const Byte *s2, unsigned size2) {
    size1 &= ~1u;
    size2 &= ~1u;
    for (unsigned i = 0;; i += 2) {
        if (i >= size1)
            return (i >= size2) ? 0 : -1;
        if (i >= size2)
            return 1;
        UInt16 c1 = GetUi16(s1 + i);
        UInt16 c2 = GetUi16(s2 + i);
        if (c1 == c2) {
            if (c1 == 0)
                return 0;
            continue;
        }
        return c1 < c2 ? -1 : 1;
    }
}

static std::optional<int> CompareRawItems(IArchiveGetRawProps *props, UInt32 first, UInt32 second,
                                          PROPID propID) {
    const void *data1 = nullptr;
    const void *data2 = nullptr;
    UInt32 dataSize1 = 0;
    UInt32 dataSize2 = 0;
    UInt32 propType1 = 0;
    UInt32 propType2 = 0;
    if (props->GetRawProp(first, propID, &data1, &dataSize1, &propType1) != S_OK ||
        props->GetRawProp(second, propID, &data2, &dataSize2, &propType2) != S_OK)
        return 0;
    if (dataSize1 == 0)
        return dataSize2 == 0 ? 0 : -1;
    if (dataSize2 == 0)
        return 1;
    if (propType1 != NPropDataType::kRaw || propType2 != NPropDataType::kRaw)
        return 0;
    if (propID == kpidNtReparse) {
        NWindows::NFile::CReparseShortInfo r1;
        NWindows::NFile::CReparseShortInfo r2;
        if (!r1.Parse(static_cast<const Byte *>(data1), dataSize1) ||
            !r2.Parse(static_cast<const Byte *>(data2), dataSize2))
            return 0;
        return CompareFileNames_Le16(static_cast<const Byte *>(data1) + r1.Offset, r1.Size,
                                     static_cast<const Byte *>(data2) + r2.Offset, r2.Size);
    }
    return std::nullopt;
}

// The source CompareItems2/CompareItems property loop, adapted to source item
// indices and the source catalog's raw-sort flag.
int CPanel::CompareItemIndices(UInt32 first, UInt32 second, PROPID sortID, bool isRawProp) const {
    if (first == UInt32(-1))
        return -1;
    if (second == UInt32(-1))
        return 1;
    const bool isDir1 = IsItem_Folder(first);
    const bool isDir2 = IsItem_Folder(second);
    if (isDir1 != isDir2)
        return isDir1 ? -1 : 1;

    PROPID propID = sortID;
    int res = 0;
    for (unsigned iter = 0; iter < 3; iter++) {
        const std::optional<int> rawResult =
            iter == 0 && isRawProp && _folderRawProps
                ? CompareRawItems(_folderRawProps, first, second, propID)
                : std::nullopt;
        if (rawResult)
            res = *rawResult;
        else if (_folderCompare)
            res = _folderCompare->CompareItems(first, second, propID, iter ? 0 : Int32(isRawProp));
        else if (propID == kpidName)
            res = CompareFileNames_ForFolderList(QStringView(ToQString(GetItemName(first))),
                                                 QStringView(ToQString(GetItemName(second))));
        else if (propID == kpidExtension) {
            const UString name1 = GetItemName(first);
            const UString name2 = GetItemName(second);
            res = CompareFileNames_ForFolderList(GetExtensionPtr(QStringView(ToQString(name1))),
                                                 GetExtensionPtr(QStringView(ToQString(name2))));
        } else {
            NWindows::NCOM::CPropVariant prop1, prop2;
            _folder->GetProperty(first, propID, &prop1);
            _folder->GetProperty(second, propID, &prop2);
            if (prop1.vt != prop2.vt)
                res = prop1.vt < prop2.vt ? -1 : 1;
            else if (prop1.vt == VT_BSTR)
                res = MyStringCompareNoCase(prop1.bstrVal, prop2.bstrVal);
            else
                res = prop1.Compare(prop2);
        }
        if (res)
            break;
        if (propID == kpidName) {
            propID = kpidPrefix;
            continue;
        }
        if (iter)
            break;
        propID = kpidName;
    }
    if (res == 0)
        res = first < second ? -1 : first > second ? 1 : 0;
    return res;
}

void CPanel::SetSortRawStatus() {
    _isRawSortProp = 0; // false;
    FOR_VECTOR(i, _columns) {
        const CPropColumn &prop = _columns[i];
        if (prop.ID == _sortID) {
            _isRawSortProp = prop.IsRawProp ? 1 : 0;
            return;
        }
    }
}

bool CPanel::GetDefaultSortAscending(PROPID propID) {
    switch (propID) {
    case kpidSize:
    case kpidPackSize:
    case kpidCTime:
    case kpidATime:
    case kpidMTime:
        return false;
    default:
        return true;
    }
}

void CPanel::SortItemsWithPropID(PROPID propID) {
    if (propID == _sortID)
        _ascending = !_ascending;
    else {
        _sortID = propID;
        _ascending = GetDefaultSortAscending(propID);
    }
    SetSortRawStatus();
    _selectionList.SortItems(_sortID, _ascending, _isRawSortProp);
    _selectionList.EnsureVisible(_selectionList.GetFocusedItem());
}

void CPanel::OnColumnClick(PROPID propID) {
    // Qt carries the clicked source-visible property instead of NMLISTVIEW.
    SortItemsWithPropID(propID);
}
