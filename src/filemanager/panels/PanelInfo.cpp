// Selection/copy summary extracted from 7-Zip App.cpp (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Native counting, unknown-size and path-list algorithms retain their bodies.
#include "filemanager/panels/PanelInfo.h"
#include "7zip/PropID.h"
#include "7zip/UI/FileManager/OverwriteDialogRes.h"
#include "7zip/UI/FileManager/PropertyNameRes.h"
#include "Windows/PropVariant.h"
#include "Windows/PropVariantConv.h"
#include "dialogs/copy/CopyDialog.h"
#include "filemanager/FormatUtils.h"
#include "filemanager/panels/Panel.h"
#include "localization/LangUtils.h"
#include "settings/ViewSettings.h"

static void AddExactSizeValue(UString &s, UInt64 size) { s += MyFormatNew(IDS_FILE_SIZE, size); }

static void AddValuePair1(UString &s, UINT resourceID, UInt64 size) {
    AddLangString(s, resourceID);
    s += ": ";
    AddExactSizeValue(s, size);
    s.Add_LF();
}

void AddValuePair2(UString &s, UINT resourceID, UInt64 num, UInt64 size) {
    if (num == 0)
        return;
    AddLangString(s, resourceID);
    s += ": ";
    s += ToUString(ConvertSizeToString(num));
    if (size != (UInt64)(Int64)-1) {
        s += "    ( ";
        AddExactSizeValue(s, size);
        s += " )";
    }
    s.Add_LF();
}

static void AddPropValueToSum(IFolderFolder *folder, UInt32 index, PROPID propID, UInt64 &sum) {
    if (sum == (UInt64)(Int64)-1)
        return;
    NWindows::NCOM::CPropVariant prop;
    folder->GetProperty(index, propID, &prop);
    UInt64 val = 0;
    if (ConvertPropVariantToUInt64(prop, val))
        sum += val;
    else
        sum = (UInt64)(Int64)-1;
}

CPanelItemTotals CPanel::GetItemTotals(const CRecordVector<UInt32> &indices) const {
    CPanelItemTotals totals;
    for (unsigned i = 0; i < indices.Size(); i++) {
        const UInt32 index = indices[i];
        if (IsItem_Folder(index)) {
            AddPropValueToSum(_folder, index, kpidSize, totals.foldersSize);
            totals.numDirs++;
        } else {
            AddPropValueToSum(_folder, index, kpidSize, totals.filesSize);
            totals.numFiles++;
        }
    }
    return totals;
}

QString CPanel::GetItemsInfoString(const CRecordVector<UInt32> &indices,
                                   const CPanelItemTotals *totals) const {
    UString info;
    const auto [numDirs, numFiles, filesSize, foldersSize] =
        totals ? *totals : GetItemTotals(indices);
    AddValuePair2(info, IDS_PROP_FOLDERS, numDirs, foldersSize);
    AddValuePair2(info, IDS_PROP_FILES, numFiles, filesSize);
    int numDefined = ((foldersSize != (UInt64)(Int64)-1) && foldersSize != 0) ? 1 : 0;
    numDefined += ((filesSize != (UInt64)(Int64)-1) && filesSize != 0) ? 1 : 0;
    if (numDefined == 2)
        AddValuePair1(info, IDS_PROP_SIZE, filesSize + foldersSize);
    info.Add_LF();
    info += _currentFolderPrefix;
    unsigned i;
    for (i = 0; i < indices.Size() && (int)i < (int)kCopyDialog_NumInfoLines - 6; i++) {
        info.Add_LF();
        info += "  ";
        const UInt32 index = indices[i];
        info += GetItemRelPath(index);
        if (IsItem_Folder(index))
            info.Add_PathSepar();
    }
    if (i != indices.Size()) {
        info.Add_LF();
        info += "  ...";
    }
    return ToQString(info);
}
