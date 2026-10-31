// Adapted from 7-Zip CPP/7zip/UI/FileManager/MessagesDialog.h (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#ifndef ZIP7_INC_MESSAGES_DIALOG_H
#define ZIP7_INC_MESSAGES_DIALOG_H

#include "Common/MyString.h"
#include "ui/dialogs/qtmodalresult.h"
#include "ui/progress/qtprogresscontrols.h"
#include <QDialog>

class CListView;
class QPushButton;

class CMessagesDialog : public QDialog {
    CQtProgressListView _messageList;
    CListView *_list = nullptr;
    QPushButton *_close;

    void AddMessageDirect(const wchar_t *message);
    void AddMessage(const wchar_t *message);
    void OnInit();
    void changeEvent(QEvent *event) override;

  public:
    const UStringVector *Messages = nullptr;
    explicit CMessagesDialog(QWidget *parent = nullptr);
    INT_PTR Create(QWidget *parent = nullptr);
    void reject() override;
};

#endif
