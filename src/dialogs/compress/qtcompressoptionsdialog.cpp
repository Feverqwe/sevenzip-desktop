#include "dialogs/compress/qtcompressoptionsdialog.h"
#include "localization/qtlangbuttonbox.h"
#include "localization/qtlangcontrols.h"
#include "ui/dialogs/qtdialogscreenservices.h"
#include <QDialogButtonBox>
#include <QVBoxLayout>

COptionsDialog::COptionsDialog(CCompressDialog *source) : panel_(new COptionsPanel(source, this)) {
    setObjectName("compressOptionsDialog");
    QtBindLocalText(this, "ArchiveRedesign",
                    QT_TRANSLATE_NOOP("ArchiveRedesign", "Format options"));
    auto *buttons = new QtLangButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->setObjectName("compressOptionsButtons");
    buttons->SetAcceptText("DialogRedesign", QT_TRANSLATE_NOOP("DialogRedesign", "Save"));
    connect(buttons, &QDialogButtonBox::accepted, this, &COptionsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    QtCreateDialogLayout(*this, *buttons)->addWidget(panel_);
}

int COptionsDialog::Create(QWidget *parent) {
    if (parent)
        setParent(parent, windowFlags());
    QtUseContextDialogModality(*this);
    return exec();
}

void COptionsDialog::setVisible(bool visible) {
    const bool initialize = visible && !initialized_;
    if (initialize) {
        initialized_ = true;
        panel_->initialize();
        if (!testAttribute(Qt::WA_Resized))
            adjustSize();
        QtNormalizeDialogPosition(*this);
    }
    QDialog::setVisible(visible);
    if (initialize)
        focusControl(this, true, Qt::OtherFocusReason);
}

void COptionsDialog::accept() {
    panel_->initialize();
    panel_->apply();
    QDialog::accept();
}

bool COptionsDialog::focusControl(QWidget *start, bool next, Qt::FocusReason reason) {
    // Standalone source-dialog traversal includes all visible enabled controls.
    // The inline panel uses its enclosing editor's ordinary focus chain.
    QWidget *control = start;
    do {
        control = next ? control->nextInFocusChain() : control->previousInFocusChain();
        if (isAncestorOf(control) && control->isVisibleTo(this) && control->isEnabled() &&
            (control->focusPolicy() & Qt::TabFocus)) {
            control->setFocus(reason);
            return true;
        }
    } while (control != start);
    return false;
}

bool COptionsDialog::focusNextPrevChild(bool next) {
    return focusControl(focusWidget() ? focusWidget() : this, next,
                        next ? Qt::TabFocusReason : Qt::BacktabFocusReason);
}
