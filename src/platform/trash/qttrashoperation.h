#pragma once
#include "Common/MyString.h"
#include "Common/MyWindows.h"

class IQtTrashService;
class QWidget;

// Joined native-trash batch. An individual platform call is not interruptible.
HRESULT QtMoveItemsToTrash(const UStringVector &paths, IQtTrashService &trash, QWidget *parent);
