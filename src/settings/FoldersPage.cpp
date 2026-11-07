// Adapted from 7-Zip FoldersPage.cpp (0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Original work-mode/state/apply bodies; Qt replaces property-page controls.
#include "settings/FoldersPage.h"

#include "7zip/UI/FileManager/FoldersPageRes.h"
#include "Common/StringConvert.h"
#include "Windows/ErrorMsg.h"
#include "archive/operations/ZipRegistry.h"
#include "dialogs/browse/BrowseDialog.h"
#include "filemanager/panels/Panel.h"
#include "localization/qtlangcontrols.h"
#include "ui/dialogs/qtdialogunits.h"
#include <QButtonGroup>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>

static const unsigned kWorkModeButtons[] = {IDR_FOLDERS_WORK_SYSTEM, IDR_FOLDERS_WORK_SPECIFIED};
static const unsigned kNumWorkModeButtons = Z7_ARRAY_SIZE(kWorkModeButtons);

CFoldersPage::CFoldersPage(const QString &settingsPath, QWidget *parent)
    : QWidget(parent), _settingsPath(settingsPath) {
    setObjectName("foldersPage");
    auto *layout = new QGridLayout(this);
    // FoldersPage2.rc places the 10-DLU radio buttons at 14-DLU intervals.
    layout->setVerticalSpacing(QtDialogUnits(font()).pixels(QSize(0, 4)).height());
    auto *workingLabel = new QLabel(this);
    QtBindResourceText(workingLabel, IDT_FOLDERS_WORKING_FOLDER, "&Working folder");
    layout->addWidget(workingLabel, 0, 0, 1, 2);
    _workModes = new QButtonGroup(this);
    const QString labels[] = {"&System temp folder", "Specified:"};
    const char *names[] = {"workSystem", "workSpecified"};
    for (unsigned i = 0; i < kNumWorkModeButtons; ++i) {
        auto *button = new QRadioButton(labels[i], this);
        button->setObjectName(names[i]);
        button->setProperty("sourceControlID", kWorkModeButtons[i]);
        QtBindResourceText(button, kWorkModeButtons[i], labels[i]);
        _workModes->addButton(button, int(kWorkModeButtons[i]));
        layout->addWidget(button, int(i) + 1, 0, 1, 2);
    }
    m_WorkPath = new QLineEdit(this);
    workingLabel->setBuddy(_workModes->button(IDR_FOLDERS_WORK_SYSTEM));
    m_WorkPath->setObjectName("workPath");
    QtBindLangText(m_WorkPath, IDR_FOLDERS_WORK_SPECIFIED, "Specified working folder", true);
    _workBrowse = new QPushButton("…", this);
    _workBrowse->setObjectName("workPathBrowse");
    QtBindLangText(_workBrowse, IDT_FOLDERS_WORKING_FOLDER, "Working folder", true);
    _workBrowse->setFixedWidth(_workBrowse->sizeHint().height() * 2);
    layout->addWidget(m_WorkPath, 3, 0);
    layout->addWidget(_workBrowse, 3, 1);
    layout->setColumnStretch(0, 1);
    layout->setRowStretch(4, 1);
    connect(_workModes, &QButtonGroup::idClicked, this,
            [this](int id) { OnButtonClicked(unsigned(id)); });
    connect(_workBrowse, &QPushButton::clicked, this,
            [this] { OnButtonClicked(IDB_FOLDERS_WORK_PATH); });
    connect(m_WorkPath, &QLineEdit::textChanged, this, &CFoldersPage::ModifiedEvent);
    OnInit();
}

bool CFoldersPage::OnInit() {
    _initMode = true;
    _needSave = false;
    NWorkDir::CInfo workDirInfo;
    workDirInfo.Load(_settingsPath);
    _workModes
        ->button(workDirInfo.Mode == NWorkDir::NMode::kSpecified ? IDR_FOLDERS_WORK_SPECIFIED
                                                                 : IDR_FOLDERS_WORK_SYSTEM)
        ->setChecked(true);
    m_WorkPath->setText(ToQString(fs2us(workDirInfo.Path)));
    MyEnableControls();
    _initMode = false;
    return true;
}

bool CFoldersPage::UsesSpecifiedPath() const {
    return _workModes->checkedId() == IDR_FOLDERS_WORK_SPECIFIED;
}

void CFoldersPage::MyEnableControls() {
    const bool enablePath = UsesSpecifiedPath();
    m_WorkPath->setEnabled(enablePath);
    _workBrowse->setEnabled(enablePath);
}

void CFoldersPage::ModifiedEvent() {
    if (!_initMode) {
        _needSave = true;
        Changed();
    }
}

bool CFoldersPage::OnButtonClicked(unsigned buttonID) {
    for (unsigned i = 0; i < kNumWorkModeButtons; i++)
        if (buttonID == kWorkModeButtons[i]) {
            MyEnableControls();
            ModifiedEvent();
            return true;
        }
    switch (buttonID) {
    case IDB_FOLDERS_WORK_PATH:
        OnFoldersWorkButtonPath();
        return true;
    default:
        return false;
    }
}

void CFoldersPage::OnFoldersWorkButtonPath() {
    UString currentPath;
    if (!ReadPath(currentPath))
        return;
    QString title = QtLangText(IDS_FOLDERS_SET_WORK_PATH_TITLE);
    UString resultPath;
    if (MyBrowseForFolder(this, ToUString(title), currentPath, resultPath))
        m_WorkPath->setText(ToQString(resultPath));
}

bool CFoldersPage::ReadPath(UString &path) {
    const QString text = m_WorkPath->text();
    if (text.contains(QChar::Null)) {
        QMessageBox::critical(this, QtLangText(IDS_FOLDERS_SET_WORK_PATH_TITLE),
                              ToQString(NWindows::NError::MyFormatMessage(E_INVALIDARG)));
        m_WorkPath->setFocus();
        return false;
    }
    path = ToUString(text);
    return true;
}

long CFoldersPage::OnApply() {
    if (_needSave) {
        NWorkDir::CInfo workDirInfo;
        UString s;
        if (!ReadPath(s))
            return 1;
        workDirInfo.Path = us2fs(s);
        workDirInfo.Mode =
            UsesSpecifiedPath() ? NWorkDir::NMode::kSpecified : NWorkDir::NMode::kSystem;
        workDirInfo.Save(_settingsPath);
        _needSave = false;
    }
    return 0;
}
