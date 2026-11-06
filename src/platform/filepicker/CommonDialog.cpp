// Adapted from 7-Zip CPP/Windows/CommonDialog.cpp (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Qt native picker replaces OPENFILENAME/ANSI/SDK transport. Source state,
// filter ordering, mode, flags and accepted-only publication remain.
#include "platform/filepicker/CommonDialog.h"
#include "ui/dialogs/qtdialoglayout.h"
#include "ui/lifecycle/qttaskpresentation.h"
#include <QFileDialog>

namespace NWindows {
bool CCommonDialogInfo::CommonDlg_BrowseForFile(const wchar_t *lpstrInitialDir,
                                                const UStringVector &filters) {
    const QtTaskPresentation::QuestionScope question;
    QStringList names;
    for (unsigned i = 0; i + 1 < filters.Size(); i += 2) {
        const QString description = QString::fromWCharArray(filters[i].Ptr());
        QString masks = QString::fromWCharArray(filters[i + 1].Ptr());
        masks.replace(';', ' ');
        names.append(description + " (" + masks + ')');
    }
    QFileDialog dialog(hwndOwner, lpstrTitle ? QString::fromWCharArray(lpstrTitle) : QString(),
                       lpstrInitialDir ? QString::fromWCharArray(lpstrInitialDir) : QString(),
                       names.join(";;"));
    QtUseContextDialogModality(dialog);
    // Source descriptions can already include their reviewed extension list.
    dialog.setOption(QFileDialog::HideNameFilterDetails, true);
    // Source flags omit OFN_FILEMUSTEXIST and OFN_OVERWRITEPROMPT. Callers
    // perform their own open/update validation after the picker returns.
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setOption(QFileDialog::DontConfirmOverwrite, true);
    dialog.setAcceptMode(SaveMode ? QFileDialog::AcceptSave : QFileDialog::AcceptOpen);
    if (FilterIndex >= 0 && FilterIndex < dialog.nameFilters().size())
        dialog.selectNameFilter(dialog.nameFilters()[FilterIndex]);
    if (!FilePath.IsEmpty())
        dialog.selectFile(QString::fromWCharArray(FilePath.Ptr()));
    if (dialog.exec() != QDialog::Accepted)
        return false;
    const QStringList selected = dialog.selectedFiles();
    if (selected.isEmpty())
        return false;
    const UString tempPath(selected.front().toStdWString().c_str());
    FilePath = tempPath;
    FilterIndex =
        filters.IsEmpty() ? -1 : dialog.nameFilters().indexOf(dialog.selectedNameFilter());
    return true;
}
} // namespace NWindows
