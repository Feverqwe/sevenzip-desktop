// Adapted from 7-Zip OverwriteDialog.cpp (0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "dialogs/overwrite/OverwriteDialog.h"
#include "7zip/UI/FileManager/PropertyNameRes.h"
#include "Common/IntToString.h"
#include "Windows/PropVariantConv.h"
#include "archive/callbacks/IFileExtractCallback.h"
#include "filemanager/FormatUtils.h"
#include "localization/LangUtils.h"
#include "ui/dialogs/qtdialoglayout.h"
#include <QEvent>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QLabel>
#include <QPushButton>

using namespace NWindows;
static const unsigned kCurrentFileNameSizeLimit = 72;

void COverwriteDialog::SetQuestion(const OverwriteQuestion &question) {
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
        ReduceString(s);
        s.Add_LF();
        {
            UString s2 = path.Ptr((unsigned)(slashPos + 1));
            ReduceString(s2);
            s += s2;
        }
        s.Add_LF();
        if (fileInfo.Size_IsDefined)
            AddSizeValue(s, fileInfo.Size);
        s.Add_LF();
        if (fileInfo.Time_IsDefined) {
            AddLangString(s, IDS_PROP_MTIME);
            s += ": ";
            char t[64];
            ConvertUtcFileTimeToString(fileInfo.Time, t);
            s += t;
        }
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
    SetFileInfoControl(OldFileInfo, IDT_OVERWRITE_OLD_FILE_SIZE_TIME, IDI_OVERWRITE_OLD_FILE,
                       IDI_OVERWRITE_OLD_FILE_2);
    SetFileInfoControl(NewFileInfo, IDT_OVERWRITE_NEW_FILE_SIZE_TIME, IDI_OVERWRITE_NEW_FILE,
                       IDI_OVERWRITE_NEW_FILE_2);
    UpdateButtonSizes();
    auto *button = static_cast<QPushButton *>(_controls.value(DefaultButton_is_NO ? IDNO : IDYES));
    button->setDefault(true);
    button->setFocus();
    return true;
}

bool COverwriteDialog::OnButtonClicked(unsigned buttonID, QWidget *) {
    switch (buttonID) {
    case IDYES:
    case IDNO:
    case IDB_YES_TO_ALL:
    case IDB_NO_TO_ALL:
    case IDB_AUTO_RENAME:
        done(int(buttonID));
        return true;
    case IDCANCEL:
        reject();
        return true;
    default:
        return false;
    }
}

INT_PTR COverwriteDialog::Create(QWidget *parent) {
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
