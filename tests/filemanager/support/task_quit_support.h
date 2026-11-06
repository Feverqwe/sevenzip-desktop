#pragma once
#include "filemanager/window/qtworkspaceapplication.h"

// Quit returns to the existing operation owner without closing file windows.
inline void ShowTasksOnQuit(QtWorkspaceApplication &application) { application.Quit(); }
