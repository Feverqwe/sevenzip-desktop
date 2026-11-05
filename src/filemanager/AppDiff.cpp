// File Manager comparison commands extracted from PanelItemOpen.cpp.
// Adapted from 7-Zip 0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "7zip/UI/FileManager/resource.h"
#include "filemanager/App.h"
#include "filemanager/external/qtexternalapplications.h"
#include "filemanager/panels/Panel.h"
#include "localization/LangUtils.h"
#include "settings/RegistryUtils.h"

void CApp::DiffFiles() {
    const CPanel &panel = GetFocusedPanel();
    if (!panel.Is_IO_FS_Folder()) {
        panel.MessageBox_Error_UnsupportOperation();
        return;
    }
    CRecordVector<UInt32> indices;
    panel.Get_ItemIndices_Selected(indices);
    UString path1, path2;
    if (indices.Size() == 2) {
        path1 = panel.GetItemFullPath(indices[0]);
        path2 = panel.GetItemFullPath(indices[1]);
    } else if (indices.Size() == 1 && NumPanels >= 2) {
        const CPanel &destPanel = Panels[1 - LastFocusedPanel];
        if (!destPanel.Is_IO_FS_Folder()) {
            panel.MessageBox_Error_UnsupportOperation();
            return;
        }
        path1 = panel.GetItemFullPath(indices[0]);
        CRecordVector<UInt32> indices2;
        destPanel.Get_ItemIndices_Selected(indices2);
        if (indices2.Size() == 1)
            path2 = destPanel.GetItemFullPath(indices2[0]);
        else {
            UString relPath = panel.GetItemRelPath2(indices[0]);
            if (panel._flatMode && !destPanel._flatMode)
                relPath = panel.GetItemName(indices[0]);
            path2 = destPanel._currentFolderPrefix + relPath;
        }
    } else
        return;
    DiffFiles(path1, path2);
}

void CApp::DiffFiles(const UString &path1, const UString &path2) {
    UString command;
    ReadRegDiff(command, _settingsPath);
    if (command.IsEmpty())
        return;
    UStringVector params;
    params.Add(path1);
    params.Add(path2);
    auto *process = GetFocusedPanel()._externalFileServices;
    if (!process) {
        GetFocusedPanel().MessageBox_Error_UnsupportOperation();
        return;
    }
    const HRESULT res = StartAppWithParams(command, params, *process, _window);
    if (res == S_OK)
        return;
    GetFocusedPanel().MessageBox_Error(LangString(IDS_CANNOT_START_EDITOR));
}
