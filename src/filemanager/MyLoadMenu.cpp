// Adapted from 7-Zip CPP/7zip/UI/FileManager/MyLoadMenu.cpp (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Menu activation, captions and file-menu policy use Qt presentation services.
// Command execution lives with the typed command-services boundary.
#include "filemanager/MyLoadMenu.h"
#include "7zip/PropID.h"
#include "7zip/UI/FileManager/PropertyNameRes.h"
#include "7zip/UI/FileManager/resource.h"
#include "Windows/FileFind.h"
#include "Windows/TimeUtils.h"
#include "filemanager/App.h"
#include "filemanager/panels/qtpanelpluginservices.h"
#include "filemanager/qtfilemenuservices.h"
#include "localization/LangUtils.h"
#include <QAction>
#include <QCoreApplication>
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
static const unsigned k_MenuID_Time = IDM_VIEW_TIME;
static const unsigned g_Zvc_IDs[] = {IDM_VER_EDIT, IDM_VER_COMMIT, IDM_VER_REVERT, IDM_VER_DIFF};
static const char *const g_Zvc_Strings[] = {QT_TRANSLATE_NOOP("VersionControl", "Ver Edit (&1)"),
                                            QT_TRANSLATE_NOOP("VersionControl", "Ver Commit"),
                                            QT_TRANSLATE_NOOP("VersionControl", "Ver Revert"),
                                            QT_TRANSLATE_NOOP("VersionControl", "Ver Diff (&0)")};

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
    case kpidNoProperty:
        return IDM_VIEW_ARANGE_NO_SORT;
    }
    return IDM_VIEW_ARANGE_BY_NAME;
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
static bool Is_MenuItem_TimePopup(const QAction &item) {
    return item.property("nativeCommandID").toUInt() == IDM_VIEW_TIME_POPUP;
}

void OnMenuActivating(QMenuBar &mainMenu, QMenu &hMenu, int position, CApp &g_App) {
    if (position < 0 || position >= mainMenu.actions().size() ||
        mainMenu.actions()[position]->menu() != &hMenu)
        return;
    if (position == 2) { // k_MenuIndex_View in the source resource.
        CheckRadioItem(hMenu, IDM_VIEW_LARGE_ICONS, IDM_VIEW_DETAILS,
                       IDM_VIEW_LARGE_ICONS + g_App.GetListViewMode());
        CheckRadioItem(hMenu, IDM_VIEW_ARANGE_BY_NAME, IDM_VIEW_ARANGE_NO_SORT,
                       GetSortControlID(g_App.GetSortID()));
        CheckItemByID(hMenu, IDM_VIEW_TWO_PANELS, g_App.NumPanels == 2);
        CheckItemByID(hMenu, IDM_VIEW_FLAT_VIEW, g_App.GetFlatMode());
        CheckItemByID(hMenu, IDM_VIEW_ARCHIVE_TOOLBAR, g_App.ShowArchiveToolbar);
        CheckItemByID(hMenu, IDM_VIEW_STANDARD_TOOLBAR, g_App.ShowStandardToolbar);
        CheckItemByID(hMenu, IDM_VIEW_TOOLBARS_LARGE_BUTTONS, g_App.LargeButtons);
        CheckItemByID(hMenu, IDM_VIEW_TOOLBARS_SHOW_BUTTONS_TEXT, g_App.ShowButtonsLables);
        CheckItemByID(hMenu, IDM_VIEW_AUTO_REFRESH, g_App.Get_AutoRefresh_Mode());
        for (QAction *item : hMenu.actions()) {
            if (item->menu() && Is_MenuItem_TimePopup(*item)) {
                FILETIME ft;
                NWindows::NTime::GetCurUtcFileTime(ft);
                wchar_t s[64];
                s[0] = 0;
                if (ConvertUtcFileTimeToString(ft, s, kTimestampPrintLevel_DAY))
                    item->setText(QString::fromWCharArray(s));
                item->setProperty("nativeCommandID", IDM_VIEW_TIME_POPUP);
                QMenu &subMenu = *item->menu();
                // Preserve Qt action identity and command bindings instead of
                // removing/recreating native menu items on each activation.
                for (QAction *entry : subMenu.actions()) {
                    const unsigned id = entry->property("nativeCommandID").toUInt();
                    if (id >= k_MenuID_Time && id < k_MenuID_Time + 5)
                        entry->setVisible(false);
                }
                const int k_TimeLevels[] = {kTimestampPrintLevel_DAY, kTimestampPrintLevel_MIN,
                                            kTimestampPrintLevel_SEC, kTimestampPrintLevel_NTFS,
                                            kTimestampPrintLevel_NS};
                unsigned last = k_MenuID_Time;
                unsigned selectedCommand = 0;
                g_App._timestampLevels.Clear();
                unsigned id = k_MenuID_Time;
                for (unsigned k = 0; k < Z7_ARRAY_SIZE(k_TimeLevels); k++) {
                    s[0] = 0;
                    const int timestampLevel = k_TimeLevels[k];
                    if (ConvertUtcFileTimeToString(ft, s, timestampLevel)) {
                        if (QAction *entry = MenuItemByID(subMenu, id)) {
                            entry->setText(QString::fromWCharArray(s));
                            entry->setVisible(true);
                            last = id;
                            g_App._timestampLevels.Add(timestampLevel);
                            if (g_App.GetTimestampLevel() == timestampLevel)
                                selectedCommand = id;
                            id++;
                        }
                    }
                }
                if (selectedCommand != 0)
                    CheckRadioItem(subMenu, k_MenuID_Time, last, selectedCommand);
                CheckItemByID(subMenu, IDM_VIEW_TIME_UTC, g_Timestamp_Show_UTC);
            }
        }
    }
}

// MyChangeMenu retains the upstream popup/ID/special-caption/shortcut flow.
// Qt preserves QAction identity; English resource lookup replaces LoadMenu's
// reset to the original English resource before applying a partial language.
static const UInt32 k_LangID_TopMenuItems[] = {IDM_FILE,      IDM_EDIT,  IDM_VIEW,
                                               IDM_FAVORITES, IDM_TOOLS, IDM_HELP};
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
                    else if (menuID == IDM_VIEW || (menuID == 0 && menuIndex == 2)) {
                        if (subMenu->objectName() == "timestampMenu")
                            langID = IDM_VIEW_TIME_POPUP;
                        else
                            langID = IDM_VIEW_TOOLBARS;
                    }
                }
            }
            if (langID == IDM_VIEW_TIME_POPUP)
                continue;
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
            if (langID == IDM_OPEN_INSIDE_ONE || langID == IDM_OPEN_INSIDE_PARSER) {
                MenuLangString(IDM_OPEN_INSIDE, newString);
                if (newString.IsEmpty())
                    continue;
                newString.Replace(L"&", L"");
                const int tabPos = newString.Find(L'\t');
                if (tabPos >= 0)
                    newString.DeleteFrom(tabPos);
                newString += (langID == IDM_OPEN_INSIDE_ONE ? " *" : " #");
            } else if (langID == IDM_BENCHMARK2) {
                MenuLangString(IDM_BENCHMARK, newString);
                if (newString.IsEmpty())
                    continue;
                newString.Replace(L"&", L"");
                const int tabPos = newString.Find(L'\t');
                if (tabPos >= 0)
                    newString.DeleteFrom(tabPos);
                newString += " 2";
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

// Source OnMenuActivating bookmark branch; keep the existing Qt actions.
void MyLoadBookmarks(QAction *const *openActions, QAction *const *setActions, CApp &app) {
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
        UString s = app.AppState.FastFolders.GetString(i);
        const int kMaxSize = 100;
        const int kFirstPartSize = kMaxSize / 2;
        if (s.Len() > kMaxSize) {
            s.Delete(kFirstPartSize, s.Len() - kMaxSize);
            s.Insert(kFirstPartSize, L" ... ");
        }
        if (s.IsEmpty())
            s = '-';
        s += "\tAlt+";
        s.Add_Char((char)('0' + i));
        openActions[i]->setText(QString::fromWCharArray(s));
        const UString fullPath = app.AppState.FastFolders.GetString(i);
        openActions[i]->setToolTip(fullPath.IsEmpty() ? QString("-")
                                                      : QString::fromWCharArray(fullPath));
    }
}

void CFileMenu::Load(IQtFileMenuServices &destMenu, unsigned startPos) {
    UString diffPath;
    destMenu.ReadRegDiff(diffPath);
    unsigned numRealItems = startPos;
    const bool isBigScreen = destMenu.IsDialogSizeOK(40, 200);
    for (unsigned i = 0;; i++) {
        QtFileMenuItem item;
        if (!destMenu.GetItem(i, item))
            break;
        {
            if (!programMenu && item.wID == IDCLOSE)
                continue;
            if (item.wID == IDM_DIFF && diffPath.IsEmpty())
                continue;
            if (item.wID == IDM_OPEN_INSIDE_ONE || item.wID == IDM_OPEN_INSIDE_PARSER) {
                // We use diff as "super mode" marker for additional commands.
                /* if (diffPath.IsEmpty()) continue; */
            }
            if (item.wID == IDM_BENCHMARK2) {
                // We use diff as "super mode" marker for additional commands.
                if (diffPath.IsEmpty())
                    continue;
            }
            bool isOneFsFile = (isFsFolder && numItems == 1 && allAreFiles);
            bool disable = (!isOneFsFile && (item.wID == IDM_SPLIT || item.wID == IDM_COMBINE));
            if (readOnly) {
                switch (item.wID) {
                case IDM_RENAME:
                case IDM_MOVE_TO:
                case IDM_DELETE:
                case IDM_COMMENT:
                case IDM_CREATE_FOLDER:
                case IDM_CREATE_FILE:
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
                case IDM_COMMENT:
                case IDM_CREATE_FOLDER:
                case IDM_CREATE_FILE:
                case IDM_LINK:
                case IDM_DIFF:
                    disable = true;
                }
            }
            if (item.wID == IDM_LINK && numItems != 1)
                disable = true;
            if (item.wID == IDM_ALT_STREAMS)
                disable = !isAltStreamsSupported;
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
    UString vercPath;
    if (!diffPath.IsEmpty() && isFsFolder && allAreFiles && numItems == 1)
        destMenu.ReadReg_VerCtrlPath(vercPath);
    if (!vercPath.IsEmpty()) {
        NWindows::NFile::NFind::CFileInfo fi;
        if (fi.Find(FilePath) && fi.Size < ((UInt32)1 << 31) && !fi.IsDir()) {
            for (unsigned k = 0; k < Z7_ARRAY_SIZE(g_Zvc_IDs); k++) {
                const unsigned id = g_Zvc_IDs[k];
                if (fi.IsReadOnly()) {
                    if (id == IDM_VER_COMMIT || id == IDM_VER_REVERT || id == IDM_VER_DIFF)
                        continue;
                } else {
                    if (id == IDM_VER_EDIT)
                        continue;
                }
                UString s(QCoreApplication::translate("VersionControl", g_Zvc_Strings[k])
                              .toStdWString()
                              .c_str());
                if (destMenu.AppendItem(id, s)) {
                    startPos++;
                    numRealItems = startPos;
                }
            }
        }
    }
    destMenu.RemoveAllItemsFrom(numRealItems);
}
