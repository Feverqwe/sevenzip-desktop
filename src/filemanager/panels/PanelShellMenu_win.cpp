// Adapted from 7-Zip CPP/7zip/UI/FileManager/PanelMenu.cpp (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Native shell provider construction and invocation for the owning panel.
#include "7zip/UI/FileManager/resource.h"
#include "Common/StringConvert.h"
#include "Windows/COM.h"
#include "Windows/FileName.h"
#include "Windows/Menu.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/panels/panelmenuids.h"
#include "localization/LangUtils.h"
#include <QWidget>
#include <cstring>
#include <shlobj.h>

#ifdef _WIN32
using namespace NWindows;
#define ODS(sz)
#define ODS_U(s)
void CPanel::InvokeSystemCommand(const char *command) {
    NCOM::CComInitializer comInitializer;
    if (!IsFsOrPureDrivesFolder())
        return;
    CRecordVector<UInt32> operatedIndices;
    Get_ItemIndices_Operated(operatedIndices);
    if (operatedIndices.IsEmpty())
        return;
    CMyComPtr<IContextMenu> contextMenu;
    if (CreateShellContextMenu(operatedIndices, contextMenu) != S_OK)
        return;

    CMINVOKECOMMANDINFO ci;
    ZeroMemory(&ci, sizeof(ci));
    ci.cbSize = sizeof(CMINVOKECOMMANDINFO);
    ci.hwnd = GetParent() ? reinterpret_cast<HWND>(GetParent()->winId()) : NULL;
    ci.lpVerb = command;
    // Request normal presentation explicitly. Native visibility is checked by
    // the Windows property-sheet consumer; nShow alone does not establish it.
    ci.nShow = SW_SHOWNORMAL;
    contextMenu->InvokeCommand(&ci);
}

struct CFolderPidls {
    LPITEMIDLIST parent;
    CRecordVector<LPITEMIDLIST> items;

    CFolderPidls() : parent(NULL) {}
    ~CFolderPidls() {
        FOR_VECTOR(i, items)
        CoTaskMemFree(items[i]);
        CoTaskMemFree(parent);
    }
};

HRESULT ShellFolder_ParseDisplayName(IShellFolder *shellFolder, HWND hwnd, const UString &path,
                                     LPITEMIDLIST *ppidl);
HRESULT ShellFolder_ParseDisplayName(IShellFolder *shellFolder, HWND hwnd, const UString &path,
                                     LPITEMIDLIST *ppidl) {
    // ULONG eaten = 0;
    return shellFolder->ParseDisplayName(hwnd, NULL, path.Ptr_non_const(),
                                         NULL, // &eaten
                                         ppidl, NULL);
}

HRESULT CPanel::CreateShellContextMenu(const CRecordVector<UInt32> &operatedIndices,
                                       CMyComPtr<IContextMenu> &systemContextMenu) {
    ODS("==== CPanel::CreateShellContextMenu");
    systemContextMenu.Release();
    UString folderPath = GetFsPath();

    CMyComPtr<IShellFolder> desktopFolder;
    RINOK(::SHGetDesktopFolder(&desktopFolder))
    if (!desktopFolder) {
        // ShowMessage("Failed to get Desktop folder");
        return E_FAIL;
    }

    CFolderPidls pidls;
    // NULL is allowed for parentHWND in ParseDisplayName()
    const HWND parentHWND_for_ParseDisplayName =
        GetParent() ? reinterpret_cast<HWND>(GetParent()->winId()) : NULL;
    // if (folderPath.IsEmpty()), then ParseDisplayName returns pidls of "My Computer"
    /* win10: ParseDisplayName() supports folder path with tail slash
      ParseDisplayName() returns {
        E_INVALIDARG         : path with super path prefix "\\\\?\\"
        ERROR_FILE_NOT_FOUND : path for network share (\\server\path1\long path2") larger than
      MAX_PATH
      } */
    const HRESULT res = ShellFolder_ParseDisplayName(desktopFolder, parentHWND_for_ParseDisplayName,
                                                     folderPath, &pidls.parent);
    if (res != S_OK) {
        ODS_U(folderPath);
        if (res != E_INVALIDARG)
            return res;
        if (!NFile::NName::If_IsSuperPath_RemoveSuperPrefix(folderPath))
            return res;
        RINOK(ShellFolder_ParseDisplayName(desktopFolder, parentHWND_for_ParseDisplayName,
                                           folderPath, &pidls.parent))
    }
    if (!pidls.parent)
        return E_FAIL;

    /*
    UString path2;
    NShell::GetPathFromIDList(pidls.parent, path2);
    ODS_U(path2);
    */

    if (operatedIndices.IsEmpty()) {
        // how to get IContextMenu, if there are no selected files?
        return E_FAIL;

        /*
        xp64 :
        1) we can't use GetUIObjectOf() with (numItems == 0), it throws exception
        2) we can't use desktopFolder->GetUIObjectOf() with absolute pidls of folder
            context menu items are different in that case:
              "Open / Explorer" for folder
              "Delete" for "My Computer" icon
              "Preperties" for "System"
        */
        /*
        parentFolder = desktopFolder;
        pidls.items.AddInReserved(pidls.parent);
        pidls.parent = NULL;
        */

        // CreateViewObject() doesn't show all context menu items
        /*
        HRESULT res = parentFolder->CreateViewObject(
            GetParent(), IID_IContextMenu, (void**)&systemContextMenu);
        */
    }

    CMyComPtr<IShellFolder> parentFolder;
    RINOK(desktopFolder->BindToObject(pidls.parent, NULL, IID_IShellFolder, (void **)&parentFolder))
    if (!parentFolder)
        return E_FAIL;

    ODS("==== CPanel::CreateShellContextMenu pidls START");

    pidls.items.ClearAndReserve(operatedIndices.Size());
    UString fileName;
    FOR_VECTOR(i, operatedIndices) {
        fileName.Empty();
        Add_ItemRelPath2_To_String(operatedIndices[i], fileName);
        /* ParseDisplayName() in win10 returns:
             E_INVALIDARG : if empty name, or path with dots only: "." , ".."
             HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) : if there is no such file
        */
        LPITEMIDLIST pidl = NULL;
        RINOK(ShellFolder_ParseDisplayName(parentFolder, parentHWND_for_ParseDisplayName, fileName,
                                           &pidl))
        if (!pidl)
            return E_FAIL;
        pidls.items.AddInReserved(pidl);
    }

    ODS("==== CPanel::CreateShellContextMenu pidls END");
    // Get IContextMenu for items
    RINOK(parentFolder->GetUIObjectOf(parentHWND_for_ParseDisplayName, pidls.items.Size(),
                                      (LPCITEMIDLIST *)(void *)pidls.items.ConstData(),
                                      IID_IContextMenu, NULL, (void **)&systemContextMenu))
    ODS("==== CPanel::CreateShellContextMenu GetUIObjectOf finished");
    if (!systemContextMenu) {
        // ShowMessage("Unable to get context menu interface");
        return E_FAIL;
    }
    return S_OK;
}
void CPanel::CreateSystemMenu(HMENU menuSpec, bool showExtendedVerbs,
                              const CRecordVector<UInt32> &operatedIndices,
                              CMyComPtr<IContextMenu> &systemContextMenu) {
    systemContextMenu.Release();

    CreateShellContextMenu(operatedIndices, systemContextMenu);

    if (!systemContextMenu)
        return;

    /*
    // Set up a CMINVOKECOMMANDINFO structure.
    CMINVOKECOMMANDINFO ci;
    ZeroMemory(&ci, sizeof(ci));
    ci.cbSize = sizeof(CMINVOKECOMMANDINFO);
    ci.hwnd = GetParent();
    */

    /*
    if (Sender == GoBtn)
    {
      // Verbs that can be used are cut, paste,
      // properties, delete, and so on.
      String action;
      if (CutRb->Checked)
        action = "cut";
      else if (CopyRb->Checked)
        action = "copy";
      else if (DeleteRb->Checked)
        action = "delete";
      else if (PropertiesRb->Checked)
        action = "properties";

      ci.lpVerb = action.c_str();
      result = cm->InvokeCommand(&ci);
      if (result)
        ShowMessage(
        "Error copying file to clipboard.");

    }
    else
    */
    {
        // HMENU hMenu = CreatePopupMenu();
        CMenu popupMenu;
        CMenuDestroyer menuDestroyer(popupMenu);
        if (!popupMenu.CreatePopup())
            throw 210503;
        const HMENU hMenu = popupMenu;
        DWORD flags = CMF_EXPLORE;
        if (showExtendedVerbs)
            flags |= 0x00000100;
        ODS("=== systemContextMenu->QueryContextMenu START");
        const HRESULT res =
            systemContextMenu->QueryContextMenu(hMenu, 0, kSystemStartMenuID, 0x7FFF, flags);
        ODS("=== systemContextMenu->QueryContextMenu END");
        if (SUCCEEDED(res)) {
#ifdef SHOW_DEBUG_FM_CTX_MENU
            PrintAllContextItems(systemContextMenu, (unsigned)res);
#endif

            CMenu menu;
            menu.Attach(menuSpec);
            CMenuItem menuItem;
            menuItem.fMask = MIIM_SUBMENU | MIIM_TYPE | MIIM_ID;
            menuItem.fType = MFT_STRING;
            menuItem.hSubMenu = popupMenu;
            LangString(IDS_SYSTEM, menuItem.StringValue);
            // The receiving menu owns the tree only after insertion succeeds.
            // Keep cleanup armed through label allocation and native refusal.
            if (menu.InsertItem(0, true, menuItem)) {
                popupMenu.Detach();
                menuDestroyer.Disable();
            }
        }
        /*
        if (Cmd < 100 && Cmd != 0)
        {
          ci.lpVerb = MAKEINTRESOURCE(Cmd - 1);
          ci.lpParameters = "";
          ci.lpDirectory = "";
          ci.nShow = SW_SHOWNORMAL;
          cm->InvokeCommand(&ci);
        }
        // If Cmd is > 100 then it's one of our
        // inserted menu items.
        else
          // Find the menu item.
          for (int i = 0; i < popupMenu1->Items->Count; i++)
          {
            TMenuItem* menu = popupMenu1->Items->Items[i];
            // Call its OnClick handler.
            if (menu->Command == Cmd - 100)
              menu->OnClick(this);
          }
          // Release the memory allocated for the menu.
          DestroyMenu(hMenu);
        */
    }
}
#undef ODS
#undef ODS_U

HRESULT CPanel::InvokeShellContextMenu(unsigned offset, IContextMenu *systemContextMenu) {
    if (!systemContextMenu)
        return E_INVALIDARG;
    // Original InvokePluginCommand's native invocation body. Numeric routing,
    // selection clearing and error presentation remain in the shared caller.
    CMINVOKECOMMANDINFOEX commandInfo;
    memset(&commandInfo, 0, sizeof(commandInfo));
    commandInfo.cbSize = sizeof(commandInfo);
    commandInfo.fMask = CMIC_MASK_UNICODE;
    commandInfo.hwnd = GetParent() ? reinterpret_cast<HWND>(GetParent()->winId()) : NULL;
    commandInfo.lpVerb = (LPCSTR)(MAKEINTRESOURCE(offset));
    commandInfo.lpParameters = NULL;
    const AString currentFolderA(GetAnsiString(_currentFolderPrefix));
    commandInfo.lpDirectory = (LPCSTR)(currentFolderA);
    commandInfo.nShow = SW_SHOW;
    commandInfo.lpParametersW = NULL;
    commandInfo.lpTitle = "";
    // The shell dispatcher converts lpVerb only; upstream keeps lpVerbW NULL
    // for compatibility with Explorer's context-menu subhandlers.
    commandInfo.lpVerbW = NULL;
    const UString currentFolderUnicode = _currentFolderPrefix;
    commandInfo.lpDirectoryW = currentFolderUnicode;
    commandInfo.lpTitleW = L"";
    commandInfo.ptInvoke.x = 0;
    commandInfo.ptInvoke.y = 0;
    return systemContextMenu->InvokeCommand(LPCMINVOKECOMMANDINFO(&commandInfo));
}
#endif
