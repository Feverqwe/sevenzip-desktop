#pragma once
#include <QIcon>

class QWidget;

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
    Back,
    Forward,
    EditLocation,
    Archive,
    Folder,
    Computer,
    Network,
    Volumes,
    Operations,
    History
};

// Vector chrome graphics; file-list and application icons stay native.
QIcon QtCommandSymbol(QtCommandIcon command, const QWidget *surface = nullptr);
