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
    EditLocation
};

// Vector command graphics; file, namespace and application icons stay native.
QIcon QtCommandSymbol(QtCommandIcon command);
