#pragma once
#include "dialogs/delete/qtdeleteconfirmation.h"
#include <QPushButton>
#include <QtTest>

inline void ReplyDelete(QtDeleteConfirmation &dialog, bool approve = true) {
    auto *button = dialog.findChild<QPushButton *>(approve ? "deleteConfirm" : "deleteCancel");
    QVERIFY(button && button->isEnabled());
    button->click();
}
