#include "dialogs/overwrite/OverwriteDialog.h"
#include "localization/qtlangcontrols.h"
#include "ui/dialogs/qtdialoglayout.h"
#include <QFrame>
#include <QGridLayout>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
struct Button {
    unsigned Id;
    const char *Name;
    const char *Text;
    int Row;
    int Column;
};
const Button kButtons[] = {{IDYES, "overwriteYes", "&Yes", 0, 0},
                           {IDB_YES_TO_ALL, "overwriteYesAll", "Yes to &All", 0, 1},
                           {IDB_AUTO_RENAME, "overwriteRename", "A&uto Rename", 0, 2},
                           {IDNO, "overwriteNo", "&No", 1, 0},
                           {IDB_NO_TO_ALL, "overwriteNoAll", "No to A&ll", 1, 1},
                           {IDCANCEL, "overwriteCancel", "&Cancel", 1, 2}};
} // namespace

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
    for (const auto &spec : kButtons) {
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
    for (const auto &spec : kButtons) {
        auto *button = static_cast<QPushButton *>(_controls.value(spec.Id));
        _buttons->removeWidget(button);
        const bool batch =
            spec.Id == IDB_YES_TO_ALL || spec.Id == IDB_NO_TO_ALL || spec.Id == IDB_AUTO_RENAME;
        button->setVisible(!batch || ShowExtraButtons);
        button->setEnabled(!batch || ShowExtraButtons);
        _buttons->addWidget(button, ShowExtraButtons ? spec.Row : 0,
                            ShowExtraButtons ? spec.Column
                                             : (spec.Id == IDYES  ? 0
                                                : spec.Id == IDNO ? 1
                                                                  : 2));
    }
}
