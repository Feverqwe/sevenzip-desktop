// Adapted from 7-Zip CPP/7zip/UI/FileManager/MyLoadMenu.cpp (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Menu activation, captions and file-menu policy use Qt presentation services.
// Command execution lives with the typed command-services boundary.
#include "filemanager/MyLoadMenu.h"
#include "7zip/PropID.h"
#include "7zip/UI/FileManager/PropertyNameRes.h"
#include "7zip/UI/FileManager/resource.h"
#include "Windows/FileFind.h"
#include "filemanager/App.h"
#include "filemanager/panels/qtpanelpluginservices.h"
#include "filemanager/qtfilemenuservices.h"
#include "localization/LangUtils.h"
#include "ui/controls/qtmenutext.h"
#include <QAction>
#include <QMenu>
#include <QMenuBar>
#include <QSignalBlocker>
#include <QVariant>

// Qt supplies the actual menu bar and source projection instead of HWND/HMENU.
void OnMenuActivating(QMenuBar &mainMenu, QMenu &hMenu, int position, CApp &g_App,
                      IQtFileMenuServices &projection, IQtPanelPluginMenuServices &plugins) {
    if (position < 0 || position >= mainMenu.actions().size() ||
        mainMenu.actions()[position]->menu() != &hMenu)
        return;
    if (position == 0) {
        for (QAction *action : hMenu.actions())
            hMenu.removeAction(action);
        g_App.GetFocusedPanel().CreateFileMenu(projection, true, &plugins);
    } else
        OnMenuActivating(mainMenu, hMenu, position, g_App);
}

#ifndef IDCLOSE
static const unsigned IDCLOSE = 8;
#endif
unsigned GetSortControlID(PROPID propID) {
    switch (propID) {
    case kpidName:
        return IDM_VIEW_ARANGE_BY_NAME;
    case kpidExtension:
        return IDM_VIEW_ARANGE_BY_TYPE;
    case kpidMTime:
        return IDM_VIEW_ARANGE_BY_DATE;
    case kpidSize:
        return IDM_VIEW_ARANGE_BY_SIZE;
    }
    return 0; // A native metadata column need not have a matching workspace menu item.
}

static QAction *MenuItemByID(QMenu &menu, unsigned id) {
    for (QAction *item : menu.actions()) {
        if (item->property("nativeCommandID").toUInt() == id)
            return item;
        if (item->menu())
            if (QAction *found = MenuItemByID(*item->menu(), id))
                return found;
    }
    return nullptr;
}
static void CheckItemByID(QMenu &menu, unsigned id, bool checked) {
    if (QAction *item = MenuItemByID(menu, id)) {
        const QSignalBlocker block(item);
        item->setChecked(checked);
    }
}
static void CheckRadioItem(QMenu &menu, unsigned first, unsigned last, unsigned selected) {
    for (unsigned id = first; id <= last; ++id)
        CheckItemByID(menu, id, id == selected);
}

void OnMenuActivating(QMenuBar &mainMenu, QMenu &hMenu, int position, CApp &g_App) {
    if (position < 0 || position >= mainMenu.actions().size() ||
        mainMenu.actions()[position]->menu() != &hMenu)
        return;
    if (position == 2) { // k_MenuIndex_View in the source resource.
        CheckRadioItem(hMenu, IDM_VIEW_LARGE_ICONS, IDM_VIEW_DETAILS,
                       IDM_VIEW_LARGE_ICONS + g_App.GetListViewMode());
        CheckRadioItem(hMenu, IDM_VIEW_ARANGE_BY_NAME, IDM_VIEW_ARANGE_BY_SIZE,
                       GetSortControlID(g_App.GetSortID()));
        CheckItemByID(hMenu, IDM_VIEW_TWO_PANELS, g_App.NumPanels == 2);
    }
}

// MyChangeMenu retains the upstream popup/ID/special-caption/shortcut flow.
// Qt preserves QAction identity; English resource lookup replaces LoadMenu's
// reset to the original English resource before applying a partial language.
static const UInt32 k_LangID_TopMenuItems[] = {IDM_FILE, IDM_EDIT, IDM_VIEW, IDM_FAVORITES,
                                               IDM_TOOLS};
static const CIDLangPair kIDLangPairs[] = {{IDCLOSE, 557},
                                           {IDM_VIEW_ARANGE_BY_NAME, IDS_PROP_NAME},
                                           {IDM_VIEW_ARANGE_BY_TYPE, IDS_PROP_FILE_TYPE},
                                           {IDM_VIEW_ARANGE_BY_DATE, IDS_PROP_MTIME},
                                           {IDM_VIEW_ARANGE_BY_SIZE, IDS_PROP_SIZE}};
static int FindLangItem(unsigned controlID) {
    for (unsigned i = 0; i < Z7_ARRAY_SIZE(kIDLangPairs); i++)
        if (kIDLangPairs[i].ControlID == controlID)
            return (int)i;
    return -1;
}
static void MenuLangString(UInt32 id, UString &dest) {
    LangString_OnlyFromLangFile(id, dest);
    if (dest.IsEmpty())
        NWindows::MyLoadString(id, dest);
}
static void MyChangeMenu(const QList<QAction *> &menu, unsigned menuID, unsigned level,
                         unsigned menuIndex) {
    for (unsigned i = 0; i < unsigned(menu.size()); i++) {
        QAction *item = menu[int(i)];
        const UInt32 id = item->property("nativeCommandID").toUInt();
        const UString oldString(item->text().toStdWString().c_str());
        UString newString;
        if (QMenu *subMenu = item->menu()) {
            UInt32 langID = id;
            if (langID >= (1 << 16))
                continue;
            if (langID == 0) {
                if (level == 0) {
                    if (i < Z7_ARRAY_SIZE(k_LangID_TopMenuItems))
                        langID = k_LangID_TopMenuItems[i];
                } else if (level == 1) {
                    if (menuID == IDM_FAVORITES || (menuID == 0 && menuIndex == 3))
                        langID = IDM_ADD_TO_FAVORITES;
                }
            }
            if (langID != IDM_ADD_TO_FAVORITES)
                MyChangeMenu(subMenu->actions(), langID, level + 1, i);
            if (langID == 0)
                continue;
            MenuLangString(langID, newString);
            if (newString.IsEmpty())
                continue;
        } else {
            if (item->isSeparator() || oldString.IsEmpty())
                continue;
            const int langPos = FindLangItem(id);
            const UInt32 langID = langPos >= 0 ? kIDLangPairs[langPos].LangID : id;
            if (langID == 0)
                continue;
            if (id >= kMenuCmdID_Toolbar_Start && id < kMenuCmdID_Toolbar_End) {
                // Toolbar commands also appear in the redesigned Archive menu;
                // their command IDs are not language resource IDs.
                SetButtonText(id, newString);
            } else if (langID == IDM_OPEN_INSIDE_ONE || langID == IDM_OPEN_INSIDE_PARSER) {
                MenuLangString(IDM_OPEN_INSIDE, newString);
                if (newString.IsEmpty())
                    continue;
                newString.Replace(L"&", L"");
                const int tabPos = newString.Find(L'\t');
                if (tabPos >= 0)
                    newString.DeleteFrom(tabPos);
                newString += (langID == IDM_OPEN_INSIDE_ONE ? " *" : " #");
            } else
                MenuLangString(langID, newString);
            if (newString.IsEmpty())
                continue;
            const int tabPos = oldString.ReverseFind(L'\t');
            if (tabPos >= 0)
                newString += oldString.Ptr(tabPos);
        }
        item->setText(QString::fromWCharArray(newString));
    }
}

void MyLoadMenu(QMenuBar &menu, bool needResetMenu) {
    // A reset changes text in place rather than destroying HMENU/QAction
    // objects. This preserves active shortcuts, signals and current state.
    if (needResetMenu || g_LangID != L"-")
        MyChangeMenu(menu.actions(), 0, 0, 0);
}

void MyReloadFileMenu(const QList<QAction *> &sourceActions) {
    MyChangeMenu(sourceActions, IDM_FILE, 1, 0);
}

// Source OnMenuActivating bookmark branch; keep the existing Qt actions.
void MyLoadBookmarks(QAction *const *openActions, QAction *const *setActions, CFastFolders paths) {
    unsigned i;
    for (i = 0; i < 10; i++) {
        UString s = LangString(IDS_BOOKMARK);
        s.Add_Space();
        const char c = (char)(L'0' + i);
        s.Add_Char(c);
        s += "\tAlt+Shift+";
        s.Add_Char(c);
        setActions[i]->setText(QString::fromWCharArray(s));
    }
    for (i = 0; i < 10; i++) {
        const UString fullPath = paths.GetString(i);
        UString s = fullPath;
        const int kMaxSize = 100;
        const int kFirstPartSize = kMaxSize / 2;
        if (s.Len() > kMaxSize) {
            s.Delete(kFirstPartSize, s.Len() - kMaxSize);
            s.Insert(kFirstPartSize, L" ... ");
        }
        if (s.IsEmpty())
            s = '-';
        openActions[i]->setText(QtLiteralMenuText(QString::fromWCharArray(s)) +
                                QStringLiteral("\tAlt+") + QLatin1Char(char('0' + i)));
        openActions[i]->setToolTip(fullPath.IsEmpty() ? QString("-")
                                                      : QString::fromWCharArray(fullPath));
    }
}

void CFileMenu::Load(IQtFileMenuServices &destMenu, unsigned startPos) {
    unsigned numRealItems = startPos;
    const bool isBigScreen = destMenu.IsDialogSizeOK(40, 200);
    for (unsigned i = 0;; i++) {
        QtFileMenuItem item;
        if (!destMenu.GetItem(i, item))
            break;
        {
            if (!programMenu && item.wID == IDCLOSE)
                continue;
            if (item.wID == IDM_DIFF || (item.wID >= IDM_VER_EDIT && item.wID <= IDM_VER_DIFF) ||
                item.wID == IDM_BENCHMARK || item.wID == IDM_BENCHMARK2 || item.wID == IDM_SPLIT ||
                item.wID == IDM_COMBINE || item.wID == IDM_CREATE_FILE ||
                item.wID == IDM_ALT_STREAMS || item.wID == IDM_COMMENT || item.wID == IDM_LINK)
                continue;
            bool disable = false;
            if (readOnly) {
                switch (item.wID) {
                case IDM_RENAME:
                case IDM_MOVE_TO:
                case IDM_DELETE:
                case IDM_CREATE_FOLDER:
                    disable = true;
                }
            }
            if (isHashFolder) {
                switch (item.wID) {
                case IDM_OPEN:
                case IDM_OPEN_INSIDE:
                case IDM_OPEN_INSIDE_ONE:
                case IDM_OPEN_INSIDE_PARSER:
                case IDM_OPEN_OUTSIDE:
                case IDM_FILE_VIEW:
                case IDM_FILE_EDIT:
                // case IDM_RENAME:
                case IDM_COPY_TO:
                case IDM_MOVE_TO:
                // case IDM_DELETE:
                case IDM_CREATE_FOLDER:
                    disable = true;
                }
            }
            if (!isBigScreen && (disable || item.IsSeparator()))
                continue;
            destMenu.CopyPopMenu_IfRequired(item);
            if (destMenu.InsertItem(startPos, item)) {
                if (disable)
                    destMenu.EnableItem(startPos, false);
                startPos++;
            }
            if (!item.IsSeparator())
                numRealItems = startPos;
        }
    }
    destMenu.RemoveAllItemsFrom(numRealItems);
}
