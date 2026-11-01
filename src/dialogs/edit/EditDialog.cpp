// Adapted from 7-Zip CPP/7zip/UI/FileManager/EditDialog.cpp/.rc (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Qt replaces Win32 edit attachment/OnSize with a layout. Resource styles
// ES_READONLY/ES_MULTILINE/WS_HSCROLL/WS_VSCROLL/ES_WANTRETURN are retained.
#include "dialogs/edit/EditDialog.h"
#include "localization/qtlangbuttonbox.h"
#include "localization/qtlangcontrols.h"
#include "settings/ViewSettings.h"
#include "ui/dialogs/qtdialoglayout.h"
#include "ui/dialogs/qtdialogscreenservices.h"
#include "ui/reports/qttextreport.h"
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

CEditDialog::CEditDialog(QWidget *parent) : QDialog(parent) {}

bool CEditDialog::OnInit() {
    setObjectName("editDialog");
    setWindowFlag(Qt::WindowMinMaxButtonsHint);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    _edit = new QPlainTextEdit(this);
    _edit->setObjectName("editText");
    QtConfigureTextReport(*_edit, _edit->font());
    _edit->installEventFilter(this);
    setWindowTitle(ToQString(Title));
    _edit->setPlainText(ToQString(Text));
    auto *buttons = new QtLangButtonBox(QDialogButtonBox::Close, this);
    buttons->setObjectName("editButtons");
    auto *copy = buttons->addButton(QString(), QDialogButtonBox::ActionRole);
    copy->setObjectName("copyReportButton");
    copy->setEnabled(!_edit->toPlainText().isEmpty());
    QtBindLocalText(copy, "DialogRedesign", QT_TRANSLATE_NOOP("DialogRedesign", "Copy"));
    connect(copy, &QPushButton::clicked, this, [this] { QtCopyTextReport(*_edit); });
    auto *close = buttons->button(QDialogButtonBox::Close);
    close->setDefault(true);
    QtCreateDialogLayout(*this, *buttons)->addWidget(_edit, 1);
    QWidget::setTabOrder(_edit, copy);
    QWidget::setTabOrder(copy, close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    resize(QSize(640, 420).expandedTo(minimumSizeHint()));
    QtNormalizeDialogSize(*this);
    close->setFocus();
    return true;
}

bool CEditDialog::eventFilter(QObject *watched, QEvent *event) {
    if (watched == _edit && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        // ES_WANTRETURN keeps Enter in the read-only edit instead of
        // activating the dialog's default Close button.
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
            return true;
    }
    return QDialog::eventFilter(watched, event);
}
