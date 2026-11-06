#pragma once
#include "7zip/UI/FileManager/ProgressDialog2Res.h"
#include "ui/progress/ProgressDialog2.h"
#include <QAbstractButton>
#include <QPushButton>
#include <QTreeWidget>
#include <QVariant>

namespace TestProgressDialog {
inline bool IsCloseButton(const QAbstractButton *button) {
    return button && button->property("languageID").toUInt() == IDS_CLOSE;
}
inline CProgressDialog *ResultWindow(QWidget *widget) {
    auto *dialog = dynamic_cast<CProgressDialog *>(widget);
    return dialog && IsCloseButton(dialog->findChild<QPushButton *>("IDCANCEL")) ? dialog : nullptr;
}
inline void CloseResult(CProgressDialog &dialog) {
    dialog.findChild<QPushButton *>("IDCANCEL")->click();
}
inline QString ReportText(const CProgressDialog &dialog) {
    QStringList rows;
    auto *messages = dialog.findChild<QTreeWidget *>("IDL_PROGRESS_MESSAGES");
    for (int row = 0; row < messages->topLevelItemCount(); ++row)
        rows.append(messages->topLevelItem(row)->text(1));
    return rows.join('\n');
}
inline bool DismissVisibleResult(QWidget *widget) {
    auto *result = ResultWindow(widget);
    if (!result || !result->isVisible())
        return false;
    CloseResult(*result);
    return true;
}
} // namespace TestProgressDialog
