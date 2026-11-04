#include "fileops/links/qtlinkcontrols.h"
#include "7zip/UI/FileManager/LinkDialogRes.h"
#include "fileops/links/qtlinkservices.h"
#include "localization/qtlangbuttonbox.h"
#include "ui/dialogs/qtdialogscreenservices.h"
#include <QLabel>
#include <QRadioButton>

void CQtLinkWindow::SetItemText(unsigned id, const UString &text) {
    if (auto *label = qobject_cast<QLabel *>(GetItem(id))) {
        label->setText(QString::fromWCharArray(text.Ptr()));
        if (id == IDT_LINK_PATH_TO_CUR)
            label->setVisible(!text.IsEmpty());
    }
}
void CQtLinkWindow::CheckRadioButton(unsigned first, unsigned last, unsigned checked) {
    for (unsigned id = first; id <= last; ++id)
        if (auto *radio = qobject_cast<QRadioButton *>(GetItem(id)))
            radio->setChecked(id == checked);
}
bool CQtLinkWindow::IsButtonCheckedBool(unsigned id) const {
    const auto *radio = qobject_cast<QRadioButton *>(GetItem(id));
    return radio && radio->isEnabled() && radio->isChecked();
}
void CQtLinkWindow::NormalizeSize() { QtNormalizeDialogSize(*this); }
qintptr CQtLinkWindow::Create(QWidget *parent) {
    QtInitializeLinkPlatform();
    if (parent)
        setParent(parent, windowFlags());
    BuildControls();
    OnInit();
    static_cast<QtLangButtonBox *>(findChild<QDialogButtonBox *>("linkButtons"))
        ->SetAcceptText("DialogRedesign", QT_TRANSLATE_NOOP("DialogRedesign", "Create link"));
    controls_.value(IDC_LINK_PATH_FROM)->setFocus();
    return exec();
}
