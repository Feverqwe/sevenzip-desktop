// Adapted from 7-Zip CPP/7zip/UI/FileManager/MessagesDialog.cpp (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "dialogs/messages/MessagesDialog.h"
#include "7zip/UI/FileManager/ProgressDialog2Res.h"
#include "Common/IntToString.h"
#include "dialogs/messages/MessagesDialogRes.h"
#include "filemanager/views/qtlistview.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "ui/dialogs/qtdialoglayout.h"
#include "ui/dialogs/qtdialogscreenservices.h"
#include "ui/dialogs/qtdialogunits.h"
#include <QDialogButtonBox>
#include <QEvent>
#include <QPushButton>
#include <QVBoxLayout>

void CMessagesDialog::AddMessageDirect(const wchar_t *message) {
    const unsigned i = unsigned(_messageList.GetItemCount());
    wchar_t sz[16];
    ConvertUInt32ToString(i, sz);
    _messageList.InsertItem(i, sz);
    _messageList.SetSubItem(i, 1, message);
}

void CMessagesDialog::AddMessage(const wchar_t *message) {
    UString s = message;
    while (!s.IsEmpty()) {
        const int pos = s.Find(L'\n');
        if (pos < 0)
            break;
        AddMessageDirect(s.Left(unsigned(pos)));
        s.DeleteFrontal(unsigned(pos) + 1);
    }
    AddMessageDirect(s);
}

void CMessagesDialog::OnInit() {
    _list->clear();
    if (Messages)
        FOR_VECTOR(i, *Messages)
    AddMessage((*Messages)[i]);

    _messageList.SetColumnWidthAuto(0);
    _messageList.SetColumnWidthAuto(1);
    QtNormalizeDialogSize(*this);
}

CMessagesDialog::CMessagesDialog(QWidget *parent) : QDialog(parent) {
    setObjectName("operationMessagesDialog");
    setWindowFlag(Qt::WindowMinMaxButtonsHint);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    QtBindResourceText(this, IDD_MESSAGES, "7-Zip: Diagnostic messages");
    const QtDialogUnits units(font());
    _list = new CListView(this);
    _list->setObjectName("IDL_MESSAGE");
    _list->setColumnCount(2);
    _list->setHeaderHidden(false);
    _messageList.Attach(_list);
    _messageList.InsertColumn(0, L"", 30);
    _messageList.InsertColumn(1, LangString(IDS_MESSAGE), 600);
    _close = new QPushButton(this);
    QtBindResourceText(_close, IDS_CLOSE, "&Close");
    _close->setObjectName("IDOK");
    _close->setDefault(true);
    auto *buttons = new QDialogButtonBox(this);
    buttons->addButton(_close, QDialogButtonBox::AcceptRole);
    auto *content = QtCreateDialogLayout(*this, *buttons);
    content->addWidget(_list, 1);
    connect(_close, &QPushButton::clicked, this, &QDialog::accept);
    resize(units.pixels(QSize(456, 176)).expandedTo(minimumSizeHint()));
}

INT_PTR CMessagesDialog::Create(QWidget *parent) {
    if (parent && parentWidget() != parent)
        setParent(parent, windowFlags());
    OnInit();
    _close->setFocus();
    return exec();
}

void CMessagesDialog::reject() { done(IDCANCEL); }

void CMessagesDialog::changeEvent(QEvent *event) {
    if (event->type() == QEvent::LanguageChange && _list)
        _list->headerItem()->setText(1, QtLangText(IDS_MESSAGE));
    QDialog::changeEvent(event);
}
