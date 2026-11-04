// LinkDialog.h

#ifndef ZIP7_INC_LINK_DIALOG_H
#define ZIP7_INC_LINK_DIALOG_H

#include "fileops/links/qtlinkcontrols.h"

#include "7zip/UI/FileManager/LinkDialogRes.h"

class CLinkDialog : public CQtLinkWindow {
    CQtLinkCombo _pathFromCombo;
    CQtLinkCombo _pathToCombo;

    virtual void OnInit() Z7_override;
    virtual void OnButtonClicked(unsigned buttonID) Z7_override;
    void OnButton_SetPath(bool to);
    void OnButton_Link();

    void ShowLastErrorMessage();
    void ShowError(const wchar_t *s);
    void Set_LinkType_Radio(unsigned idb);

  public:
    UString CurDirPrefix;
    UString FilePath;
    UString AnotherPath;

    qintptr Create(QWidget *parentWindow = nullptr) { return CQtLinkWindow::Create(parentWindow); }
};

#endif
