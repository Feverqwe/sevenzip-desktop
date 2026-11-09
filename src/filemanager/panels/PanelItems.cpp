// Adapted from 7-Zip CPP/7zip/UI/FileManager/PanelItems.cpp (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// IFolder/variant property bodies retain upstream control flow. Qt list rows
// use the source load/property stages and a separate Qt row-publication stage.
#include "7zip/PropID.h"
#include "7zip/UI/FileManager/resource.h"
#include "Common/StringConvert.h"
#include "Windows/FileName.h"
#include "Windows/PropVariant.h"
#include "Windows/PropVariantConv.h"
#include "Windows/WinDefs.h"
#include "archive/opening/archivecontainerpath.h"
#include "archive/operations/ExtractingFilePath.h"
#include "filemanager/panels/Panel.h"
#include "ui/progress/ProgressDialog2.h"
#include <QCoreApplication>

static const UInt32 kParentIndex = (UInt32)-1;

namespace {
class FolderSizeCheckpoint final : public IProgress, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_1(IProgress)
    CProgressSync &sync_;

  public:
    explicit FolderSizeCheckpoint(CProgressSync &sync) : sync_(sync) {}
    Z7_COM7F_IMF(SetTotal(UInt64)) override { return sync_.CheckStop(); }
    Z7_COM7F_IMF(SetCompleted(const UInt64 *)) override { return sync_.CheckStop(); }
};

class FolderSizeOperation final : public CProgressThreadVirt {
    IFolderCalcItemFullSize &folder_;
    const CRecordVector<UInt32> &indices_;
    const UStringVector &paths_;

    HRESULT ProcessVirt() override {
        CMyComPtr<IProgress> progress = new FolderSizeCheckpoint(Sync);
        HRESULT firstError = S_OK;
        FOR_VECTOR(i, indices_) {
            RINOK(Sync.CheckStop())
            Sync.Set_FilePath(paths_[i], true);
            const HRESULT result = folder_.CalcItemFullSize(indices_[i], progress);
            if (result == E_ABORT)
                return result;
            if (result != S_OK) {
                if (firstError == S_OK)
                    firstError = result;
                AddErrorPath(us2fs(paths_[i]));
            }
        }
        return firstError;
    }

  public:
    FolderSizeOperation(IFolderCalcItemFullSize &folder, const CRecordVector<UInt32> &indices,
                        const UStringVector &paths)
        : folder_(folder), indices_(indices), paths_(paths) {
        ShowCompressionInfo = false;
        WaitMode = true;
    }
};
} // namespace

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
            CRecordVector<UInt32> indices;
            Get_ItemIndices_Operated(indices);
            CRecordVector<UInt32> folders;
            FOR_VECTOR(i, indices) {
                if (IsItem_Folder(indices[i]))
                    folders.Add(indices[i]);
            }
            if (!folders.IsEmpty()) {
                CDisableTimerProcessing timer(*this);
                UStringVector paths;
                GetFilePaths(folders, paths);
                FolderSizeOperation operation(*calcItemFullSize.Interface(), folders, paths);
                const auto title =
                    QCoreApplication::translate("ArchiveDirectory", "Calculating folder size…");
                {
                    CDisableNotify notify(*this);
                    operation.Create(ToUString(title), GetParent());
                }
                if (_sortID == kpidSize)
                    _selectionList.SortItems(_sortID, _ascending, _isRawSortProp);
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

bool CPanel::CanOpenItemsOutside(const CRecordVector<UInt32> &indices) const {
    if (!IsArcFolder())
        return true;
    if (indices.IsEmpty())
        return false;
    FOR_VECTOR(i, indices)
    if (IsItem_Folder(indices[i]))
        return false;
    return true;
}

void CPanel::OpenSelectedItems(bool tryInternal) {
    CRecordVector<UInt32> indices;
    Get_ItemIndices_Operated(indices);
    if (!tryInternal && !CanOpenItemsOutside(indices))
        return;
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
