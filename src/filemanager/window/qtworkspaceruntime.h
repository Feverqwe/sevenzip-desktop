#pragma once
#include "filemanager/window/mainwindow.h"

// In-memory context only. Widgets, providers, archive chains and external
// launchers retain stable owners for the lifetime of an open tab.
struct QtTabRuntime {
    ~QtTabRuntime();
    void ClearPresentation();
    std::shared_ptr<CPanel> source;
    MainWindow::PanelUiState ui;
    bool dirty = false;
};
