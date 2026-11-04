// Adapted from 7-Zip CPP/7zip/UI/FileManager/MyLoadMenu.h (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#pragma once
#include "Common/MyString.h"
class IQtFileMenuServices;
class QMenuBar;
class QAction;
class CApp;
class QMenu;
class IQtPanelPluginMenuServices;
void OnMenuActivating(QMenuBar &mainMenu, QMenu &menu, int position, CApp &app);
void OnMenuActivating(QMenuBar &mainMenu, QMenu &menu, int position, CApp &app,
                      IQtFileMenuServices &projection, IQtPanelPluginMenuServices &plugins);
void MyLoadMenu(QMenuBar &menu, bool needResetMenu);
void MyLoadBookmarks(QAction *const *openActions, QAction *const *setActions, CApp &app);
unsigned GetSortControlID(PROPID propID);

struct CFileMenu {
    bool programMenu;
    bool readOnly;
    bool isHashFolder;
    bool isFsFolder;
    bool allAreFiles;
    bool isAltStreamsSupported;
    unsigned numItems;
    FString FilePath;

    CFileMenu()
        : programMenu(false), readOnly(false), isHashFolder(false), isFsFolder(false),
          allAreFiles(false), isAltStreamsSupported(true), numItems(0) {}
    void Load(IQtFileMenuServices &menu, unsigned startPos);
};
