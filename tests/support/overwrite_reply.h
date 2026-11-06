#pragma once
#include "dialogs/overwrite/OverwriteDialog.h"
#include <QCheckBox>
#include <QPushButton>
#include <QtTest>

inline void ReplyOverwrite(QDialog &dialog, int result) {
    auto *apply = dialog.findChild<QCheckBox *>("overwriteApplyToRemaining");
    const bool all =
        result == IDB_YES_TO_ALL || result == IDB_NO_TO_ALL || result == IDB_AUTO_RENAME;
    QVERIFY(apply);
    if (all)
        QVERIFY(apply->isEnabled());
    apply->setChecked(all);
    const char *name =
        result == IDYES || result == IDB_YES_TO_ALL ? "overwriteYes"
        : result == IDNO || result == IDB_NO_TO_ALL ? "overwriteNo"
        : result == IDB_AUTO_RENAME || result == NOverwriteDialog::kAutoRenameOnceResult
            ? "overwriteRename"
            : "overwriteCancel";
    auto *button = dialog.findChild<QPushButton *>(name);
    QVERIFY(button && button->isEnabled());
    button->click();
}
