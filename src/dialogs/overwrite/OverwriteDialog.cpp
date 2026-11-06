// Adapted from 7-Zip OverwriteDialog.cpp (0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "dialogs/overwrite/OverwriteDialog.h"
#include "7zip/UI/FileManager/PropertyNameRes.h"
#include "Common/IntToString.h"
#include "Windows/PropVariantConv.h"
#include "archive/callbacks/IFileExtractCallback.h"
#include "filemanager/FormatUtils.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "ui/dialogs/qtdialoglayout.h"
#include "ui/dialogs/qtdialogscreenservices.h"
#include "ui/lifecycle/qttaskpresentation.h"
#include <QCheckBox>
#include <QCoreApplication>
#include <QEvent>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

using namespace NWindows;
static const unsigned kCurrentFileNameSizeLimit = 72;

void COverwriteDialog::SetQuestion(const OverwriteQuestion &question) {
    _multipleConflictsPossible = question.MultipleConflictsPossible;
    const auto setFile = [](NOverwriteDialog::CFileInfo &info,
                            const OverwriteQuestion::File &file) {
        info.Path = file.Path.toStdWString().c_str();
        info.Is_FileSystemFile = file.Filesystem;
        info.Is_Directory = file.Directory;
        info.SetSize2(!file.Directory && file.Size ? &*file.Size : nullptr);
        info.SetTime2(file.Modified ? &*file.Modified : nullptr);
    };
    setFile(OldFileInfo, question.Existing);
    setFile(NewFileInfo, question.Incoming);
}

int NOverwriteDialog::MapButtonResult(INT_PTR button) {
    switch (button) {
    case IDYES:
        return NOverwriteAnswer::kYes;
    case IDB_YES_TO_ALL:
        return NOverwriteAnswer::kYesToAll;
    case IDNO:
        return NOverwriteAnswer::kNo;
    case IDB_NO_TO_ALL:
        return NOverwriteAnswer::kNoToAll;
    case IDB_AUTO_RENAME:
        return NOverwriteAnswer::kAutoRename;
    case kAutoRenameOnceResult:
        return NOverwriteAnswer::kAutoRenameOnce;
    default:
        return NOverwriteAnswer::kCancel;
    }
}

void COverwriteDialog::ReduceString(UString &s) {
    const unsigned size = kCurrentFileNameSizeLimit;
    if (s.Len() > size) {
        s.Delete(size / 2, s.Len() - size);
        s.Insert(size / 2, L" ... ");
    }
    if (!s.IsEmpty() && s.Back() == ' ') {
        s.InsertAtFront(L'\"');
        s.Add_Char('\"');
    }
}

void COverwriteDialog::SetFileInfoControl(const NOverwriteDialog::CFileInfo &fileInfo,
                                          unsigned textID, unsigned iconID, unsigned iconID_2) {
    {
        const UString &path = fileInfo.Path;
        const int slashPos = path.ReverseFind_PathSepar();
        UString s = path.Left((unsigned)(slashPos + 1));
        if (!ShowExtraButtons)
            ReduceString(s);
        s.Add_LF();
        {
            UString s2 = path.Ptr((unsigned)(slashPos + 1));
            if (!ShowExtraButtons)
                ReduceString(s2);
            s += s2;
        }
        s.Add_LF();
        if (fileInfo.Size_IsDefined)
            AddSizeValue(s, fileInfo.Size);
        else if (ShowExtraButtons)
            s += UString((QtLangText(IDS_PROP_SIZE) + ": " +
                          QCoreApplication::translate("ConflictRedesign", "Unknown"))
                             .toStdWString()
                             .c_str());
        s.Add_LF();
        if (fileInfo.Time_IsDefined) {
            AddLangString(s, IDS_PROP_MTIME);
            s += ": ";
            char t[64];
            ConvertUtcFileTimeToString(fileInfo.Time, t);
            s += t;
        } else if (ShowExtraButtons)
            s += UString((QtLangText(IDS_PROP_MTIME) + ": " +
                          QCoreApplication::translate("ConflictRedesign", "Unknown"))
                             .toStdWString()
                             .c_str());
        SetItemText(textID, s);
    }
    // Qt owns native file icons. Shell extraction of a second EXE/ICO icon
    // has no portable QFileIconProvider equivalent.
    QFileIconProvider provider;
    const auto path = QString::fromWCharArray(fileInfo.Path);
    const auto icon = fileInfo.Is_FileSystemFile ? provider.icon(QFileInfo(path))
                                                 : provider.icon(QFileIconProvider::File);
    static_cast<QLabel *>(_controls.value(iconID))->setPixmap(icon.pixmap(32, 32));
    _controls.value(iconID_2)->hide();
}

void COverwriteDialog::SetItemText(unsigned id, const UString &text) {
    static_cast<QLabel *>(_controls.value(id))->setText(QString::fromWCharArray(text));
}

bool COverwriteDialog::OnInit() {
    QtUseContextDialogModality(*this);
    if (_controls.isEmpty())
        BuildPresentation();
    _applyToRemaining->setChecked(false);
    SetFileInfoControl(OldFileInfo, IDT_OVERWRITE_OLD_FILE_SIZE_TIME, IDI_OVERWRITE_OLD_FILE,
                       IDI_OVERWRITE_OLD_FILE_2);
    SetFileInfoControl(NewFileInfo, IDT_OVERWRITE_NEW_FILE_SIZE_TIME, IDI_OVERWRITE_NEW_FILE,
                       IDI_OVERWRITE_NEW_FILE_2);
    UpdateButtonSizes();
    const UString &path = NewFileInfo.Path;
    SetItemText(IDT_OVERWRITE_HEADER, path.Ptr((unsigned)(path.ReverseFind_PathSepar() + 1)));
    auto *button = static_cast<QPushButton *>(
        _controls.value(ShowExtraButtons || DefaultButton_is_NO ? IDNO : IDYES));
    button->setDefault(true);
    button->setFocus();
    ensurePolished();
    layout()->activate();
    if (auto *scroll = findChild<QScrollArea *>("dialogContentScroll")) {
        scroll->widget()->layout()->activate();
        const int contentHeight = scroll->widget()->heightForWidth(scroll->viewport()->width());
        resize(width(),
               qMax(minimumSizeHint().height(), contentHeight + height() - scroll->height()));
    }
    QtNormalizeDialogSize(*this, true);
    return true;
}

bool COverwriteDialog::OnButtonClicked(unsigned buttonID, QWidget *) {
    switch (buttonID) {
    case IDYES:
    case IDNO:
    case IDB_AUTO_RENAME: {
        const bool batch =
            _applyToRemaining && _applyToRemaining->isEnabled() && _applyToRemaining->isChecked();
        const int result = buttonID == IDYES  ? (batch ? IDB_YES_TO_ALL : IDYES)
                           : buttonID == IDNO ? (batch ? IDB_NO_TO_ALL : IDNO)
                           : batch            ? IDB_AUTO_RENAME
                                              : NOverwriteDialog::kAutoRenameOnceResult;
        done(result);
        return true;
    }
    case IDCANCEL:
        reject();
        return true;
    default:
        return false;
    }
}

INT_PTR COverwriteDialog::Create(QWidget *parent) {
    QtTaskPresentation::Watch(*this, QtOperationPhase::Question);
    if (parent && parent != parentWidget())
        setParent(parent, windowFlags());
    OnInit();
    return exec();
}

void COverwriteDialog::changeEvent(QEvent *event) {
    if (event->type() == QEvent::LanguageChange && !_controls.isEmpty()) {
        SetFileInfoControl(OldFileInfo, IDT_OVERWRITE_OLD_FILE_SIZE_TIME, IDI_OVERWRITE_OLD_FILE,
                           IDI_OVERWRITE_OLD_FILE_2);
        SetFileInfoControl(NewFileInfo, IDT_OVERWRITE_NEW_FILE_SIZE_TIME, IDI_OVERWRITE_NEW_FILE,
                           IDI_OVERWRITE_NEW_FILE_2);
    }
    if (event->type() == QEvent::LanguageChange || event->type() == QEvent::FontChange ||
        event->type() == QEvent::StyleChange)
        UpdateButtonSizes();
    QDialog::changeEvent(event);
}

void COverwriteDialog::reject() { done(IDCANCEL); }
