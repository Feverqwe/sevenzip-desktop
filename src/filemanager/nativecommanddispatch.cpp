// Command dispatch adapted from 7-Zip MyLoadMenu.cpp.
// Original baseline: b88c6bc09339de32f7d18bbab900686b244f42ba.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "7zip/PropID.h"
#include "7zip/UI/FileManager/resource.h"
#include "Common/MyWindows.h"
#include "filemanager/panels/panelmenuids.h"
#include "filemanager/qtfilemanagercommandservices.h"

#ifndef IDCLOSE
static const unsigned IDCLOSE = 8;
#endif
static const unsigned k_MenuID_OpenBookmark = 830;
static const unsigned k_MenuID_SetBookmark = 810;
static const unsigned k_MenuID_Time = IDM_VIEW_TIME;

const char *GetHashMethodForCommand(unsigned id) {
    switch (id) {
    case IDM_HASH_ALL:
        return "*";
    case IDM_CRC32:
        return "CRC32";
    case IDM_CRC64:
        return "CRC64";
    case IDM_XXH64:
        return "XXH64";
    case IDM_MD5:
        return "MD5";
    case IDM_SHA1:
        return "SHA1";
    case IDM_SHA256:
        return "SHA256";
    case IDM_SHA384:
        return "SHA384";
    case IDM_SHA512:
        return "SHA512";
    case IDM_SHA3_256:
        return "SHA3-256";
    case IDM_BLAKE2SP:
        return "BLAKE2sp";
    default:
        return nullptr;
    }
}

bool ExecuteFileCommand(unsigned id, IQtFileManagerCommandServices &g_App) {
    // Qt can deliver a queued shortcut after capabilities changed. Recompute
    // the same application policy used by menus before any source dispatch.
    if (!g_App.CanExecuteCommand(id))
        return false;
    if (id >= kMenuCmdID_Plugin_Start) {
        g_App.InvokePluginCommand(id);
        g_App.ReleasePluginMenus();
        return true;
    }
    switch (id) {
    case IDM_OPEN:
        g_App.OpenItem();
        break;
    case IDM_OPEN_INSIDE:
        g_App.OpenItemInside(NULL);
        break;
    case IDM_OPEN_INSIDE_ONE:
        g_App.OpenItemInside(L"*");
        break;
    case IDM_OPEN_INSIDE_PARSER:
        g_App.OpenItemInside(L"#");
        break;
    case IDM_OPEN_OUTSIDE:
        g_App.OpenItemOutside();
        break;
    case IDM_FILE_VIEW:
        g_App.EditItem(false);
        break;
    case IDM_FILE_EDIT:
        g_App.EditItem(true);
        break;
    case IDM_RENAME:
        g_App.Rename();
        break;
    case IDM_COPY_TO:
        g_App.CopyTo();
        break;
    case IDM_MOVE_TO:
        g_App.MoveTo();
        break;
    case IDM_DELETE:
        g_App.Delete(!g_App.IsShiftDown());
        break;
    case IDM_HASH_ALL:
    case IDM_CRC32:
    case IDM_CRC64:
    case IDM_XXH64:
    case IDM_MD5:
    case IDM_SHA1:
    case IDM_SHA256:
    case IDM_SHA384:
    case IDM_SHA512:
    case IDM_SHA3_256:
    case IDM_BLAKE2SP:
        g_App.CalculateCrc(GetHashMethodForCommand(id));
        break;
    case IDM_SPLIT:
        g_App.Split();
        break;
    case IDM_COMBINE:
        g_App.Combine();
        break;
    case IDM_PROPERTIES:
        g_App.Properties();
        break;
    case IDM_COMMENT:
        g_App.Comment();
        break;
    case IDM_CREATE_FOLDER:
        g_App.CreateFolder();
        break;
    case IDM_CREATE_FILE:
        g_App.CreateFile();
        break;
#ifndef UNDER_CE
    case IDM_LINK:
        g_App.Link();
        break;
    case IDM_ALT_STREAMS:
        g_App.OpenAltStreams();
        break;
#endif
    default:
        return false;
    }
    return true;
}

bool OnMenuCommand(unsigned id, IQtFileManagerMenuCommandServices &g_App) {
    if (!g_App.CanExecuteCommand(id))
        return false;
    if (ExecuteFileCommand(id, g_App))
        return true;
    switch (id) {
    case IDCLOSE:
        g_App.CloseWindow();
        break;
    case IDM_SELECT_ALL:
        g_App.SelectAll(true);
        g_App.Refresh_StatusBar();
        break;
    case IDM_DESELECT_ALL:
        g_App.SelectAll(false);
        g_App.Refresh_StatusBar();
        break;
    case IDM_INVERT_SELECTION:
        g_App.InvertSelection();
        g_App.Refresh_StatusBar();
        break;
    case IDM_SELECT:
        g_App.SelectSpec(true);
        g_App.Refresh_StatusBar();
        break;
    case IDM_DESELECT:
        g_App.SelectSpec(false);
        g_App.Refresh_StatusBar();
        break;
    case IDM_SELECT_BY_TYPE:
        g_App.SelectByType(true);
        g_App.Refresh_StatusBar();
        break;
    case IDM_DESELECT_BY_TYPE:
        g_App.SelectByType(false);
        g_App.Refresh_StatusBar();
        break;
    case IDM_VIEW_LARGE_ICONS:
    case IDM_VIEW_DETAILS:
        g_App.SetListViewMode(id - IDM_VIEW_LARGE_ICONS);
        break;
    case IDM_VIEW_ARANGE_BY_NAME:
        g_App.SortItemsWithPropID(kpidName);
        break;
    case IDM_VIEW_ARANGE_BY_TYPE:
        g_App.SortItemsWithPropID(kpidExtension);
        break;
    case IDM_VIEW_ARANGE_BY_DATE:
        g_App.SortItemsWithPropID(kpidMTime);
        break;
    case IDM_VIEW_ARANGE_BY_SIZE:
        g_App.SortItemsWithPropID(kpidSize);
        break;
    case IDM_OPEN_ROOT_FOLDER:
        g_App.OpenRootFolder();
        break;
    case IDM_OPEN_PARENT_FOLDER:
        g_App.OpenParentFolder();
        break;
    case IDM_FOLDERS_HISTORY:
        g_App.FoldersHistory();
        break;
    case IDM_VIEW_FLAT_VIEW:
        g_App.ChangeFlatMode();
        break;
    case IDM_VIEW_REFRESH:
        g_App.RefreshView();
        break;
    case IDM_VIEW_AUTO_REFRESH:
        g_App.Change_AutoRefresh_Mode();
        break;
    case IDM_VIEW_TWO_PANELS:
        g_App.SwitchOnOffOnePanel();
        break;
    case IDM_VIEW_TIME_UTC:
        g_App.TimestampShowUTC() = !g_App.TimestampShowUTC();
        g_App.RedrawListItems_InPanels();
        break;
    case IDM_OPTIONS:
        g_App.OptionsDialog();
        break;
    case IDM_ABOUT:
        g_App.AboutDialog();
        break;
    case IDM_TEMP_DIR:
        g_App.MyBrowseForTempFolder();
        break;
    default: {
        if (id >= k_MenuID_OpenBookmark && id <= k_MenuID_OpenBookmark + 9) {
            g_App.OpenBookmark(id - k_MenuID_OpenBookmark);
            return true;
        } else if (id >= k_MenuID_SetBookmark && id <= k_MenuID_SetBookmark + 9) {
            g_App.SetBookmark(id - k_MenuID_SetBookmark);
            return true;
        } else if (id >= k_MenuID_Time &&
                   (unsigned)id < k_MenuID_Time + g_App.TimestampLevelCount()) {
            g_App.SetTimestampLevel(g_App.TimestampLevel(id - k_MenuID_Time));
            return true;
        }
        return false;
    }
    }
    return true;
}
