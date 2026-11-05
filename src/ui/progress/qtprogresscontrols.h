// Qt widget/platform services for the source ProgressDialog2 presentation.
#pragma once

#include "Common/MyString.h"
#include <QDialog>
#include <QElapsedTimer>
#include <QHash>
#include <QIcon>

class QProgressBar;

struct CQtProgressRect {
    int left;
    int top;
    int right;
    int bottom;
};

class CQtProgressBar {
    QProgressBar *_view = nullptr;

  public:
    void Attach(QWidget *widget);
    void SetRange32(int minimum, int maximum);
    void SetPos(int position);
};

class CQtProgressDialog : public QDialog {
    QHash<unsigned, QWidget *> _controls;
    QElapsedTimer _clock;
    double _unitX;
    void AddControl(unsigned id, const char *name, QWidget *widget);
    void BuildPresentation();

  protected:
    QWidget *GetItem(unsigned id) const;
    void SetItemText(unsigned id, const UString &text);
    void GetItemText(unsigned id, UString &text) const;
    void SetText(const UString &text);
    void ShowItem_Bool(unsigned id, bool visible);
    void HideItem(unsigned id);
    void GetItemSizes(unsigned id, int &width, int &height) const;
    void GetClientRectOfItem(unsigned id, CQtProgressRect &rect) const;
    void MoveItem(unsigned id, int x, int y, int width, int height);
    int Units_To_Pixels_X(int units) const;
    UInt32 GetTickCount() const;
    void NormalizeSize(bool fullNormalize);
    static QIcon LoadIcon(int id);
    void SetIcon(const QIcon &icon);
    static void ChangeSubWindowSizeX(QWidget *widget, int width);
    static void ClipboardSetText(const UString &text);
    qintptr CreateModal(QWidget *parent);
    void End(qintptr result);
    void SetDefaultButton(unsigned id);
    static void MessageBox(QWidget *parent, const UString &message, const UString &title,
                           bool error);
    static int AskCancel(QWidget *parent, const UString &message, const UString &title);
#ifdef _WIN32
    static HWND GetNativeTaskbarWindow(QWidget *widget);
#endif

  public:
    explicit CQtProgressDialog(QWidget *parent = nullptr);
};
