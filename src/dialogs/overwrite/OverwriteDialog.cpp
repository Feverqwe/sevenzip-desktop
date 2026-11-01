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
#include <QEvent>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

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

void AddSizeValue(UString &s, UInt64 value);
void AddSizeValue(UString &s, UInt64 value) {
    {
        s += MyFormatNew(IDS_FILE_SIZE, value);
    }
    if (value >= (1 << 10)) {
        char c;
        if (value >= ((UInt64)10 << 30)) {
            value >>= 30;
            c = 'G';
        } else if (value >= (10 << 20)) {
            value >>= 20;
            c = 'M';
        } else {
            value >>= 10;
            c = 'K';
        }
        s += " : ";
        s.Add_UInt64(value);
        s.Add_Space();
        s.Add_Char(c);
        s += "iB";
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

void COverwriteDialog::BuildPresentation() {
    setObjectName("overwriteDialog");
    QtBindResourceText(this, IDD_OVERWRITE, "Confirm File Replace");
    auto *footer = new QWidget(this);
    auto *layout = QtCreateDialogLayout(*this, *footer);
    const auto label = [&](unsigned id, const char *name, const QString &text) {
        auto *control = new QtWrappingLabel(text, this);
        control->setObjectName(name);
        control->setTextFormat(Qt::PlainText);
        control->setWordWrap(true);
        _controls.insert(id, control);
        if (id == IDT_OVERWRITE_HEADER || id == IDT_OVERWRITE_QUESTION_BEGIN ||
            id == IDT_OVERWRITE_QUESTION_END)
            QtBindResourceText(control, id, text);
        return control;
    };
    layout->addWidget(label(IDT_OVERWRITE_HEADER, "overwriteHeader",
                            "Destination folder already contains processed file."));
    auto *comparison = new QHBoxLayout;
    comparison->setSpacing(20);
    const auto fileRow = [&](unsigned textID, unsigned iconID, unsigned secondID,
                             unsigned headingID, const char *name, const char *headingName,
                             const char *heading) {
        auto *card = new QFrame(this);
        card->setFrameShape(QFrame::StyledPanel);
        auto *column = new QVBoxLayout(card);
        column->setContentsMargins(12, 12, 12, 12);
        column->setSpacing(8);
        auto *title = label(headingID, headingName, {});
        QtBindLocalText(title, "DialogRedesign", heading);
        column->addWidget(title);
        auto *row = new QHBoxLayout;
        auto *icons = new QVBoxLayout;
        icons->addWidget(label(iconID, (QByteArray(name) + "Icon").constData(), {}));
        auto *second = label(secondID, (QByteArray(name) + "SecondIcon").constData(), {});
        second->hide();
        icons->addWidget(second);
        icons->addStretch();
        row->addLayout(icons);
        auto *details = label(textID, name, {});
        details->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        row->addWidget(details, 1);
        column->addLayout(row, 1);
        comparison->addWidget(card, 1);
    };
    fileRow(IDT_OVERWRITE_OLD_FILE_SIZE_TIME, IDI_OVERWRITE_OLD_FILE, IDI_OVERWRITE_OLD_FILE_2,
            IDT_OVERWRITE_QUESTION_BEGIN, "overwriteOldFile", "overwriteQuestionBegin",
            QT_TRANSLATE_NOOP("DialogRedesign", "Existing file"));
    fileRow(IDT_OVERWRITE_NEW_FILE_SIZE_TIME, IDI_OVERWRITE_NEW_FILE, IDI_OVERWRITE_NEW_FILE_2,
            IDT_OVERWRITE_QUESTION_END, "overwriteNewFile", "overwriteQuestionEnd",
            QT_TRANSLATE_NOOP("DialogRedesign", "Incoming file"));
    layout->addLayout(comparison);
    _buttons = new QGridLayout;
    _buttons->setHorizontalSpacing(8);
    _buttons->setVerticalSpacing(8);
    struct Button {
        unsigned Id;
        const char *Name;
        const char *Text;
        int Row;
        int Column;
    };
    for (const auto &spec : {Button{IDYES, "overwriteYes", "&Yes", 0, 0},
                             Button{IDB_YES_TO_ALL, "overwriteYesAll", "Yes to &All", 0, 1},
                             Button{IDB_AUTO_RENAME, "overwriteRename", "A&uto Rename", 0, 2},
                             Button{IDNO, "overwriteNo", "&No", 1, 0},
                             Button{IDB_NO_TO_ALL, "overwriteNoAll", "No to A&ll", 1, 1},
                             Button{IDCANCEL, "overwriteCancel", "&Cancel", 1, 2}}) {
        auto *button = new QPushButton(QString::fromUtf8(spec.Text), this);
        button->setAttribute(Qt::WA_LayoutUsesWidgetRect);
        button->setObjectName(spec.Name);
        const unsigned langID = spec.Id == IDYES      ? 406
                                : spec.Id == IDNO     ? 407
                                : spec.Id == IDCANCEL ? 402
                                                      : spec.Id;
        QtBindResourceText(button, langID, QString::fromUtf8(spec.Text));
        if (spec.Id == IDYES)
            QtBindLocalText(button, "DialogRedesign",
                            QT_TRANSLATE_NOOP("DialogRedesign", "Replace"));
        else if (spec.Id == IDNO)
            QtBindLocalText(button, "DialogRedesign", QT_TRANSLATE_NOOP("DialogRedesign", "Skip"));
        else if (spec.Id == IDB_YES_TO_ALL)
            QtBindLocalText(button, "DialogRedesign",
                            QT_TRANSLATE_NOOP("DialogRedesign", "Replace all"));
        else if (spec.Id == IDB_NO_TO_ALL)
            QtBindLocalText(button, "DialogRedesign",
                            QT_TRANSLATE_NOOP("DialogRedesign", "Skip all"));
        _controls.insert(spec.Id, button);
        _buttons->addWidget(button, spec.Row, spec.Column);
        connect(button, &QPushButton::clicked, this,
                [this, id = spec.Id, button] { OnButtonClicked(id, button); });
    }
    auto *actions = new QHBoxLayout(footer);
    actions->setContentsMargins(0, 0, 0, 0);
    actions->addStretch();
    actions->addLayout(_buttons);
    resize(620, minimumSizeHint().height());
}

void COverwriteDialog::UpdateButtonSizes() {
    if (!_buttons || !_controls.contains(IDCANCEL))
        return;
    const unsigned ids[] = {IDYES, IDB_YES_TO_ALL, IDB_AUTO_RENAME, IDNO, IDB_NO_TO_ALL, IDCANCEL};
    for (int i = 0; i < 6; ++i) {
        auto *button = static_cast<QPushButton *>(_controls.value(ids[i]));
        _buttons->removeWidget(button);
        const bool batch =
            ids[i] == IDB_YES_TO_ALL || ids[i] == IDB_NO_TO_ALL || ids[i] == IDB_AUTO_RENAME;
        button->setVisible(!batch || ShowExtraButtons);
        button->setEnabled(!batch || ShowExtraButtons);
        _buttons->addWidget(button, ShowExtraButtons ? i / 3 : 0,
                            ShowExtraButtons ? i % 3
                                             : (ids[i] == IDYES  ? 0
                                                : ids[i] == IDNO ? 1
                                                                 : 2));
    }
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
