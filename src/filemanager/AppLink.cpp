// Adapted from 7-Zip CPP/7zip/UI/FileManager/LinkDialog.cpp (0766b733).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// File Manager selection, destination and refresh for the native Link command.
#include "7zip/PropID.h"
#include "7zip/UI/FileManager/resource.h"
#include "dialogs/link/LinkDialog.h"
#include "filemanager/App.h"
#include "filemanager/panels/Panel.h"

void CApp::Link() {
    const unsigned srcPanelIndex = GetFocusedPanelIndex();
    CPanel &srcPanel = Panels[srcPanelIndex];
    if (!srcPanel.IsFSFolder()) {
        srcPanel.MessageBox_Error_UnsupportOperation();
        return;
    }
    CRecordVector<UInt32> indices;
    srcPanel.Get_ItemIndices_Operated(indices);
    if (indices.IsEmpty())
        return;
    if (indices.Size() != 1) {
        srcPanel.MessageBox_Error_LangID(IDS_SELECT_ONE_FILE);
        return;
    }
    const UInt32 index = indices[0];
    const UString itemName = srcPanel.GetItemName(index);

    const UString fsPrefix = srcPanel.GetFsPath();
    const UString srcPath = fsPrefix + srcPanel.GetItemPrefix(index);
    UString path = srcPath;
    {
        const unsigned destPanelIndex = (NumPanels <= 1) ? srcPanelIndex : (1 - srcPanelIndex);
        CPanel &destPanel = Panels[destPanelIndex];
        if (NumPanels > 1)
            if (destPanel.IsFSFolder())
                path = destPanel.GetFsPath();
    }

    CSelectedState srcSelState;
    srcPanel.SaveSelectedState(srcSelState);

    CLinkDialog dlg;
    dlg.CurDirPrefix = fsPrefix;
    dlg.FilePath = srcPath + itemName;
    dlg.AnotherPath = path;

    if (dlg.Create(srcPanel.GetParent()) != QDialog::Accepted)
        return;

    // we refresh srcPanel to show changes in "Link" (kpidNtReparse) column.
    // maybe we should refresh another panel also?
    if (srcPanel._visibleColumns.FindItem_for_PropID(kpidNtReparse) >= 0)
        srcPanel.RefreshListCtrl(srcSelState);

    RefreshTitleAlways();
}
