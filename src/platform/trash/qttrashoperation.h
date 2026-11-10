#pragma once
#include "Common/MyString.h"
#include "Common/MyWindows.h"
#include <QStringList>

class IQtTrashService;
class QWidget;

// Joined native-trash batch. An individual platform call is not interruptible.
HRESULT QtMoveItemsToTrash(const UStringVector &paths, const QString &sourceDirectory,
                           IQtTrashService &trash, QWidget *parent, bool deferResultReview = false,
                           QStringList *diagnostics = nullptr, unsigned *movedCount = nullptr);
