// Adapted from 7-Zip CPP/7zip/UI/FileManager/PanelKey.cpp at 0766b73.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "7zip/PropID.h"
#include "7zip/UI/FileManager/resource.h"
#include "filemanager/App.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/panels/qtpanelkey.h"
#include <QTimer>

struct CVKeyPropIDPair {
    int VKey;
    PROPID PropID;
};

static const CVKeyPropIDPair g_VKeyPropIDPairs[] = {{Qt::Key_F3, kpidName},
                                                    {Qt::Key_F4, kpidExtension},
                                                    {Qt::Key_F5, kpidMTime},
                                                    {Qt::Key_F6, kpidSize}};

static int FindVKeyPropIDPair(int vKey) {
    for (unsigned i = 0; i < Z7_ARRAY_SIZE(g_VKeyPropIDPairs); i++)
        if (g_VKeyPropIDPairs[i].VKey == vKey)
            return (int)i;
    return -1;
}

bool CPanel::OnKeyDown(const QtPanelKeyDown &keyDownInfo, CApp &g_App) {
    if (keyDownInfo.VKey == Qt::Key_Tab) {
        _panelCallback->OnTab();
        return false;
    }
    const bool alt = keyDownInfo.Alt;
    const bool ctrl = keyDownInfo.Control;
    // const bool leftCtrl = IsKeyDown(VK_LCONTROL);
    const bool rightCtrl = keyDownInfo.RightControl;
    const bool shift = keyDownInfo.Shift;

    if (keyDownInfo.VKey >= '0' && keyDownInfo.VKey <= '9' && (rightCtrl || alt)) {
        const unsigned index = (unsigned)(keyDownInfo.VKey - '0');
        if (shift) {
            g_App.ExecuteCommand(810 + index);
            return true;
        } else {
            g_App.ExecuteCommand(830 + index);
            return true;
        }
    }

    if ((keyDownInfo.VKey == Qt::Key_F2 || keyDownInfo.VKey == Qt::Key_F1) && alt && !ctrl &&
        !shift) {
        _panelCallback->SetFocusToPath(keyDownInfo.VKey == Qt::Key_F1 ? 0 : 1);
        return true;
    }

    if ((keyDownInfo.VKey == Qt::Key_F9) && !alt && !ctrl && !shift) {
        g_App.ExecuteCommand(IDM_VIEW_TWO_PANELS);
    }

    if (keyDownInfo.VKey >= Qt::Key_F3 && keyDownInfo.VKey <= Qt::Key_F12 && ctrl) {
        const int index = FindVKeyPropIDPair(keyDownInfo.VKey);
        if (index >= 0)
            SortItemsWithPropID(g_VKeyPropIDPairs[index].PropID);
    }

    switch (keyDownInfo.VKey) {
    case Qt::Key_Shift: {
        _selectionIsDefined = false;
        _prevFocusedItem = _selectionList.GetFocusedItem();
        break;
    }
    case Qt::Key_F2: {
        if (!alt && !ctrl && !shift) {
            g_App.ExecuteCommand(IDM_RENAME);
            return true;
        }
        break;
    }
    case Qt::Key_F3: {
        if (!alt && !ctrl && !shift) {
            g_App.ExecuteCommand(IDM_FILE_VIEW);
            return true;
        }
        break;
    }
    case Qt::Key_F4: {
        if (!alt && !ctrl && !shift) {
            g_App.ExecuteCommand(IDM_FILE_EDIT);
            return true;
        }
        break;
    }
    case Qt::Key_F5: {
        if (!alt && !ctrl) {
            _panelCallback->OnCopy(false, shift);
            return true;
        }
        break;
    }
    case Qt::Key_F6: {
        if (!alt && !ctrl) {
            _panelCallback->OnCopy(true, shift);
            return true;
        }
        break;
    }
    case Qt::Key_F7: {
        if (!alt && !ctrl && !shift) {
            /* we can process F7 via menu ACCELERATOR.
              But menu loading can be slow in case of UNC paths and system menu.
              So we use don't use ACCELERATOR */
            g_App.ExecuteCommand(IDM_CREATE_FOLDER);
            return true;
        }
        break;
    }
    case Qt::Key_Delete:
        if (!alt && !ctrl && !shift)
            g_App.ExecuteCommand(IDM_DELETE);
        return true;
    case Qt::Key_Insert: {
        if (!alt) {
            if (ctrl && !shift) {
                EditCopy();
                return true;
            }
            if (shift && !ctrl) {
                EditPaste();
                return true;
            }
            if (!shift && !ctrl && _mySelectMode) {
                OnInsert();
                return true;
            }
        }
        return false;
    }
    case Qt::Key_Down: {
        if (shift)
            OnArrowWithShift();
        return false;
    }
    case Qt::Key_Up: {
        if (alt)
            _panelCallback->OnSetSameFolder();
        else if (shift)
            OnArrowWithShift();
        return false;
    }
    case Qt::Key_Right: {
        if (alt)
            _panelCallback->OnSetSubFolder();
        else if (shift)
            OnArrowWithShift();
        return false;
    }
    case Qt::Key_Left: {
        if (alt)
            _panelCallback->OnSetSubFolder();
        else if (shift)
            OnArrowWithShift();
        return false;
    }
    case Qt::Key_PageDown: {
        if (ctrl && !alt && !shift) {
            // EnterToFocused();
            return true;
        }
        break;
    }
    case Qt::Key_Plus:
    case Qt::Key_Minus:
        if (shift && !alt && !ctrl) {
            g_App.ExecuteCommand(keyDownInfo.VKey == Qt::Key_Plus ? IDM_SELECT_ALL
                                                                  : IDM_DESELECT_ALL);
            return true;
        }
        return false;
    /*
    case Qt::Key_Delete:
      CommandDelete();
      return 0;
    */
    case Qt::Key_Backspace:
        g_App.ExecuteCommand(IDM_OPEN_PARENT_FOLDER);
        return true;
    /*
    case VK_DIVIDE:
    case '\\':
    case '/':
    case VK_OEM_5:
    {
      // OpenRootFolder();
      OpenDrivesFolder();

      return true;
    }
    */
    case 'A':
        if (ctrl) {
            g_App.ExecuteCommand(IDM_SELECT_ALL);
            return true;
        }
        return false;
    case 'X':
        if (ctrl) {
            EditCut();
            return true;
        }
        return false;
    case 'C':
        if (ctrl) {
            EditCopy();
            return true;
        }
        return false;
    case 'V':
        if (ctrl) {
            EditPaste();
            return true;
        }
        return false;
    case 'R':
        if (ctrl) {
            g_App.ExecuteCommand(IDM_VIEW_REFRESH);
            return true;
        }
        return false;
    case 'W':
        if (ctrl) {
            // SendMessage();
            QTimer::singleShot(0, this, [&g_App] { g_App.ExecuteCommand(8); });
            return true;
        }
        return false;
    case '1':
    case '4':
        if (ctrl) {
            const unsigned styleIndex = (unsigned)(keyDownInfo.VKey - '1');
            g_App.ExecuteCommand(IDM_VIEW_LARGE_ICONS + styleIndex);
            return true;
        }
        return false;
    case Qt::Key_F12:
        if (alt && !ctrl && !shift) {
            g_App.ExecuteCommand(IDM_FOLDERS_HISTORY);
            return true;
        }
    }
    return false;
}
