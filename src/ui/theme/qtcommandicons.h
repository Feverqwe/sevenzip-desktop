#pragma once
#include <QIcon>

enum class QtCommandIcon {
    Add,
    Extract,
    Test,
    Copy,
    Move,
    Delete,
    Info,
    Parent,
    NewFolder,
    Refresh,
    Cut,
    Paste,
    Rename,
    Sidebar,
    Back,
    Forward,
    EditLocation,
    Archive,
    Folder,
    Computer,
    Network,
    Volumes
};

// Vector chrome graphics; file-list and application icons stay native.
QIcon QtCommandSymbol(QtCommandIcon command);
