#include "dialogs/overwrite/OverwriteDialog.h"
#include "localization/qtlangcontrols.h"
#include "ui/controls/qtresponsiverow.h"
#include "ui/controls/qtwrappingcheckbox.h"
#include "ui/dialogs/qtdialoglayout.h"
#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
struct Choice {
    unsigned Id;
    const char *Name;
    const char *Text;
};
constexpr Choice kChoices[] = {
    {IDYES, "overwriteYes", QT_TRANSLATE_NOOP("DialogRedesign", "Replace")},
    {IDNO, "overwriteNo", QT_TRANSLATE_NOOP("DialogRedesign", "Skip")},
    {IDB_AUTO_RENAME, "overwriteRename", QT_TRANSLATE_NOOP("ConflictRedesign", "Keep both")}};
} // namespace

void COverwriteDialog::BuildPresentation() {
    setObjectName("overwriteDialog");
    auto *footer = new QWidget(this);
    auto *actions = new QHBoxLayout(footer);
    actions->setContentsMargins(0, 0, 0, 0);
    auto *cancel = new QPushButton(footer);
    cancel->setObjectName("overwriteCancel");
    _controls.insert(IDCANCEL, cancel);
    QtBindResourceText(cancel, 402, "Cancel");
    connect(cancel, &QPushButton::clicked, this, &COverwriteDialog::reject);
    actions->addWidget(cancel);
    actions->addStretch();
    auto *layout = QtCreateDialogLayout(*this, *footer, QtDialogContent::Scrollable);
    const auto label = [&](unsigned id, const char *name) {
        auto *control = new QtWrappingLabel(this);
        control->setObjectName(name);
        control->setTextFormat(Qt::PlainText);
        control->setWordWrap(true);
        _controls.insert(id, control);
        return control;
    };
    auto *header = label(IDT_OVERWRITE_HEADER, "overwriteHeader");
    header->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    header->setMinimumWidth(0);
    QFont heading = header->font();
    heading.setWeight(QFont::DemiBold);
    header->setFont(heading);
    layout->addWidget(header);
    const auto card = [&](unsigned textID, unsigned iconID, unsigned secondID, unsigned headingID,
                          const char *name, const char *headingName, const char *headingText) {
        auto *frame = new QFrame(this);
        frame->setMinimumWidth(fontMetrics().averageCharWidth() * 24 + 56);
        frame->setFrameShape(QFrame::StyledPanel);
        auto *column = new QVBoxLayout(frame);
        column->setContentsMargins(12, 12, 12, 12);
        column->setSpacing(8);
        auto *title = label(headingID, headingName);
        QtBindLocalText(title, "ConflictRedesign", headingText);
        column->addWidget(title);
        auto *row = new QHBoxLayout;
        auto *icon = label(iconID, (QByteArray(name) + "Icon").constData());
        icon->setFixedSize(32, 32);
        row->addWidget(icon, 0, Qt::AlignTop);
        label(secondID, (QByteArray(name) + "SecondIcon").constData())->hide();
        auto *details = label(textID, name);
        details->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        details->setMinimumWidth(0);
        details->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        row->addWidget(details, 1);
        column->addLayout(row);
        return frame;
    };
    auto *existing =
        card(IDT_OVERWRITE_OLD_FILE_SIZE_TIME, IDI_OVERWRITE_OLD_FILE, IDI_OVERWRITE_OLD_FILE_2,
             IDT_OVERWRITE_QUESTION_BEGIN, "overwriteOldFile", "overwriteQuestionBegin",
             QT_TRANSLATE_NOOP("ConflictRedesign", "At destination"));
    auto *incoming =
        card(IDT_OVERWRITE_NEW_FILE_SIZE_TIME, IDI_OVERWRITE_NEW_FILE, IDI_OVERWRITE_NEW_FILE_2,
             IDT_OVERWRITE_QUESTION_END, "overwriteNewFile", "overwriteQuestionEnd",
             QT_TRANSLATE_NOOP("ConflictRedesign", "Incoming file"));
    auto *comparison = new QtResponsiveRow({existing, incoming}, this);
    comparison->setObjectName("overwriteComparison");
    layout->addWidget(comparison);
    _renameHint = new QtWrappingLabel(this);
    _renameHint->setWordWrap(true);
    QtBindLocalText(
        _renameHint, "ConflictRedesign",
        QT_TRANSLATE_NOOP(
            "ConflictRedesign",
            "Keep both preserves the existing file and gives the incoming file a free name."));
    layout->addWidget(_renameHint);
    auto *apply = new QtWrappingCheckBox(this);
    _applyRow = apply;
    _applyToRemaining = apply->checkBox();
    _applyToRemaining->setObjectName("overwriteApplyToRemaining");
    QtBindLocalText(
        _applyToRemaining, "ConflictRedesign",
        QT_TRANSLATE_NOOP("ConflictRedesign", "Apply to remaining conflicts in this operation"),
        true);
    QtBindLocalText(
        apply->label(), "ConflictRedesign",
        QT_TRANSLATE_NOOP("ConflictRedesign", "Apply to remaining conflicts in this operation"));
    layout->addWidget(apply);
    for (const auto &spec : kChoices) {
        auto *button = new QPushButton(footer);
        button->setObjectName(spec.Name);
        QtBindLocalText(button, spec.Id == IDB_AUTO_RENAME ? "ConflictRedesign" : "DialogRedesign",
                        spec.Text);
        _controls.insert(spec.Id, button);
        actions->addWidget(button);
        connect(button, &QPushButton::clicked, this,
                [this, id = spec.Id, button] { OnButtonClicked(id, button); });
    }
    resize(620, minimumSizeHint().height());
}

void COverwriteDialog::UpdateButtonSizes() {
    if (!_applyToRemaining)
        return;
    const bool allowBatch = ShowExtraButtons && _multipleConflictsPossible;
    _applyRow->setVisible(allowBatch);
    _applyToRemaining->setEnabled(allowBatch);
    _renameHint->setVisible(ShowExtraButtons);
    _controls.value(IDB_AUTO_RENAME)->setVisible(ShowExtraButtons);
    _controls.value(IDB_AUTO_RENAME)->setEnabled(ShowExtraButtons);
    if (ShowExtraButtons) {
        QtBindLocalText(this, "ConflictRedesign",
                        QT_TRANSLATE_NOOP("ConflictRedesign", "File already exists"));
    } else {
        QtBindResourceText(this, IDD_OVERWRITE, "Confirm File Replace");
    }
}
