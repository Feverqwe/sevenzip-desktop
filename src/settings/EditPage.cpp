// Adapted from 7-Zip EditPage.cpp (0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Qt supplies controls/property-page services; original state/apply/Browse flow.
#include "settings/EditPage.h"

#include "7zip/UI/FileManager/EditPageRes.h"
#include "Windows/ErrorMsg.h"
#include "dialogs/browse/BrowseDialog.h"
#include "filemanager/external/qtexternalapplications.h"
#include "localization/qtlangcontrols.h"
#include "settings/RegistryUtils.h"
#include "settings/ViewSettings.h"
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <iterator>

static void Edit_BrowseForFile(QLineEdit &edit, QWidget *hwnd);

static bool ReadCommand(QLineEdit &edit, UString &command) {
    const QString text = edit.text();
    if (text.contains(QChar::Null)) {
        QMessageBox::critical(edit.window(), edit.window()->windowTitle(),
                              ToQString(NWindows::NError::MyFormatMessage(E_INVALIDARG)));
        edit.setFocus();
        return false;
    }
    command = ToUString(text);
    return true;
}

CEditPage::CEditPage(const QString &settingsPath, QWidget *parent)
    : QWidget(parent), _settingsPath(settingsPath) {
    setObjectName("editorPage");
    auto *layout = new QGridLayout(this);
    const QString labels[] = {"&View:", "&Editor:"};
    const unsigned labelIDs[] = {IDT_EDIT_VIEWER, IDT_EDIT_EDITOR};
    const char *names[] = {"viewerCommand", "editorCommand"};
    for (unsigned i = 0; i < std::size(_ctrls); ++i) {
        auto *label = new QLabel(labels[i], this);
        label->setProperty("languageColon", i == 0);
        QtBindResourceText(label, labelIDs[i], labels[i]);
        auto &c = _ctrls[i];
        c.Edit = new QLineEdit(this);
        c.Edit->setObjectName(names[i]);
        QtBindLangText(c.Edit, labelIDs[i], labels[i], true);
        label->setBuddy(c.Edit);
        auto *browse = new QPushButton("…", this);
        browse->setObjectName(QString(names[i]) + "Browse");
        QtBindLangText(browse, labelIDs[i], labels[i], true);
        browse->setFixedWidth(browse->sizeHint().height() * 2);
        layout->addWidget(label, int(i) * 2, 0, 1, 2);
        layout->addWidget(c.Edit, int(i) * 2 + 1, 0);
        layout->addWidget(browse, int(i) * 2 + 1, 1);
        connect(c.Edit, &QLineEdit::textChanged, this, [this, i] {
            if (!_initMode) {
                _ctrls[i].WasChanged = true;
                Changed();
            }
        });
        connect(browse, &QPushButton::clicked, this,
                [this, edit = c.Edit] { Edit_BrowseForFile(*edit, this); });
    }
    layout->setRowStretch(4, 1);
    layout->setColumnStretch(0, 1);
    OnInit();
}

bool CEditPage::OnInit() {
    _initMode = true;
    for (unsigned i = 0; i < std::size(_ctrls); i++) {
        CEditPageCtrl &c = _ctrls[i];
        c.WasChanged = false;
        UString path;
        ReadRegEditor(i > 0, path, _settingsPath);
        c.Edit->setText(ToQString(path));
    }
    _initMode = false;
    return true;
}

long CEditPage::OnApply(QWidget **invalidControl) {
    if (invalidControl)
        *invalidControl = nullptr;
    for (unsigned i = 0; i < std::size(_ctrls); i++) {
        CEditPageCtrl &c = _ctrls[i];
        if (c.WasChanged) {
            UString path;
            if (!ReadCommand(*c.Edit, path)) {
                if (invalidControl)
                    *invalidControl = c.Edit;
                return 1;
            }
            SaveRegEditor(i > 0, path, _settingsPath);
            c.WasChanged = false;
        }
    }
    return 0;
}

static void Edit_BrowseForFile(QLineEdit &edit, QWidget *hwnd) {
    UString cmd;
    if (!ReadCommand(edit, cmd))
        return;
    UString param;
    UString prg;
    SplitCmdLineSmart(cmd, prg, param);
    CBrowseInfo bi;
    bi.FilterIndex = 0;
    bi.FilePath = prg;
    bi.hwndOwner = hwnd;
    CObjectVector<CBrowseFilterInfo> filters;
    CBrowseFilterInfo &filter = filters.AddNew();
#ifdef Q_OS_WIN
    filter.Description = "*.exe";
    filter.Masks.Add(UString("*.exe"));
#else
    filter.Description = "*";
    filter.Masks.Add(UString("*"));
#endif
    if (bi.BrowseForFile(filters)) {
        cmd = bi.FilePath;
        cmd.Trim();
        edit.setText(ToQString(cmd));
    }
}

bool CEditPage::IsModified() const {
    for (const auto &c : _ctrls)
        if (c.WasChanged)
            return true;
    return false;
}
