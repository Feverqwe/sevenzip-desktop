// Qt control construction for the native memory-usage request.
#include "7zip/UI/FileManager/resourceGui.h"
#include "dialogs/mem/MemDialog.h"
#include "localization/qtlangbuttonbox.h"
#include "localization/qtlangcontrols.h"
#include "ui/dialogs/qtdialoglayout.h"
#include "ui/dialogs/qtdialogunits.h"
#include <QCheckBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QToolButton>
#include <QVBoxLayout>

void CMemDialog::BuildPresentation() {
    setObjectName("memDialog");
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    QtBindResourceText(this, IDD_MEM, "Memory usage request");
    auto *buttons =
        new QtLangButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this, IDCONTINUE);
    buttons->setObjectName("memButtons");
    auto *layout = QtCreateDialogLayout(*this, *buttons);
    auto *message = new QtWrappingLabel(this);
    message->setObjectName("memMessage");
    message->setTextFormat(Qt::PlainText);
    message->setWordWrap(true);
    message->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    _controls.insert(IDT_MEM_MESSAGE, message);
    layout->addWidget(message);
    layout->addSpacing(12);
    layout->addWidget(BuildActions());
    layout->addSpacing(12);
    auto *save = new QCheckBox(this);
    QtBindResourceText(save, IDX_MEM_SAVE_LIMIT, "Change allowed limit for next operations");
    save->setObjectName("memSaveLimit");
    _controls.insert(IDX_MEM_SAVE_LIMIT, save);
    layout->addWidget(save);
    connect(save, &QCheckBox::toggled, this, &CMemDialog::EnableSpin);
    layout->addWidget(BuildLimitRow());
    auto *remember = new QCheckBox(this);
    QtBindResourceText(remember, IDX_MEM_REMEMBER, "&Repeat selected action for current operation");
    remember->setObjectName("memRemember");
    _controls.insert(IDX_MEM_REMEMBER, remember);
    layout->addWidget(remember);
    QtBindResourceText(buttons->button(QDialogButtonBox::Ok), 411, "Continue");
    connect(buttons, &QDialogButtonBox::accepted, this, &CMemDialog::OnContinue);
    connect(buttons, &QDialogButtonBox::rejected, this, &CMemDialog::reject);
}

QWidget *CMemDialog::BuildLimitRow() {
    const QtDialogUnits units(font());
    auto *view = new QWidget(this);
    auto *row = new QHBoxLayout(view);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    row->addSpacing(24);
    auto *pair = new QWidget(view);
    pair->setObjectName("memLimitPair");
    auto *fields = new QHBoxLayout(pair);
    fields->setContentsMargins(0, 0, 0, 0);
    fields->setSpacing(0);
    auto *edit = new QLineEdit(pair);
    edit->setObjectName("memLimit");
    QtBindLangText(edit, IDS_MEM_CURRENT_MEM_LIMIT, "Current allowed limit", true);
    edit->setAlignment(Qt::AlignCenter);
    edit->setMinimumHeight(units.pixels(QSize(0, 12)).height());
    edit->installEventFilter(this);
    _controls.insert(IDE_MEM_SPIN_EDIT, edit);
    fields->addWidget(edit, 1);
    auto *spin = new QWidget(pair);
    spin->setObjectName("memSpin");
    auto *arrows = new QVBoxLayout(spin);
    arrows->setContentsMargins(0, 0, 0, 0);
    arrows->setSpacing(0);
    for (bool up : {true, false}) {
        auto *button = new QToolButton(spin);
        button->setFocusPolicy(Qt::NoFocus);
        button->setArrowType(up ? Qt::UpArrow : Qt::DownArrow);
        button->setObjectName(up ? "memSpinUp" : "memSpinDown");
        button->setAutoRepeat(true);
        button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
        arrows->addWidget(button);
        connect(button, &QToolButton::clicked, this, [this, up] { StepSpin(up); });
    }
    _controls.insert(IDC_MEM_SPIN, spin);
    spin->setFixedHeight(edit->sizeHint().height());
    spin->setFixedWidth(qMax(units.pixels(QSize(8, 0)).width(), spin->sizeHint().width()));
    fields->addWidget(spin);
    pair->setFixedWidth(qMax(units.pixels(QSize(50, 0)).width(),
                             edit->minimumSizeHint().width() + spin->sizeHint().width()));
    row->addWidget(pair);
    row->addSpacing(units.pixels(QSize(8, 0)).width());
    auto *gb = new QLabel(view);
    gb->setObjectName("memGB");
    _controls.insert(IDT_MEM_GB, gb);
    row->addWidget(gb);
    row->addStretch();
    return view;
}

QWidget *CMemDialog::BuildActions() {
    const QtDialogUnits units(font());
    auto *group = new QGroupBox(this);
    group->setFlat(true);
    QtBindResourceText(group, IDG_MEM_ACTION, "Action");
    group->setObjectName("memAction");
    auto *actions = new QVBoxLayout(group);
    actions->setSpacing(units.pixels(QSize(0, 4)).height());
    auto *allow = new QRadioButton(group);
    auto *skip = new QRadioButton(group);
    QtBindResourceText(allow, IDR_MEM_ACTION_ALLOW, "&Allow archive unpacking");
    QtBindResourceText(skip, IDR_MEM_ACTION_SKIP_ARC, "&Skip archive unpacking");
    allow->setObjectName("memAllow");
    skip->setObjectName("memSkip");
    for (auto *button : {allow, skip})
        button->setMinimumHeight(units.pixels(QSize(0, 10)).height());
    _controls.insert(IDR_MEM_ACTION_ALLOW, allow);
    _controls.insert(IDR_MEM_ACTION_SKIP_ARC, skip);
    actions->addWidget(allow);
    actions->addWidget(skip);
    return group;
}
