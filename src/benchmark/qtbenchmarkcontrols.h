// Qt presentation services for the original BenchmarkDialog control flow.
#pragma once
#include "Common/MyString.h"
#include <QComboBox>
#include <QDialog>
#include <QElapsedTimer>
#include <QHash>
#include <QPlainTextEdit>
#include <QTimer>
#include <atomic>

class QLabel;
class QGridLayout;
class QHBoxLayout;
class QDialogButtonBox;

class CQtBenchmarkCombo {
    QComboBox *view_ = nullptr;

  public:
    void Attach(QWidget *view) { view_ = qobject_cast<QComboBox *>(view); }
    int AddString(const UString &text) {
        if (!view_)
            return 0;
        view_->addItem(QString::fromWCharArray(text.Ptr()));
        return view_->count() - 1;
    }
    int AddString_SetItemData(const UString &text, qintptr data) {
        const int index = AddString(text);
        SetItemData(index, data);
        return index;
    }
    void SetItemData(int index, qintptr data) {
        if (view_)
            view_->setItemData(index, qlonglong(data));
    }
    qintptr GetItemData_of_CurSel() const {
        return view_ ? qintptr(view_->currentData().toLongLong()) : 0;
    }
    void SetCurSel(int index) {
        if (view_)
            view_->setCurrentIndex(index);
    }
};

class CQtBenchmarkEdit {
    QPlainTextEdit *view_ = nullptr;

  public:
    void Attach(QWidget *view) { view_ = qobject_cast<QPlainTextEdit *>(view); }
    void SetText(const UString &text) { view_->setPlainText(QString::fromWCharArray(text.Ptr())); }
    void SetText(const AString &text) { view_->setPlainText(QString::fromUtf8(text.Ptr())); }
};

class CQtBenchmarkWindow : public QDialog {
    QHash<unsigned, QWidget *> controls_;
    QTimer timer_;
    QElapsedTimer clock_;
    std::atomic_bool postsEnabled_{true};
    void BuildControls(bool totalMode);
    void BuildNormalControls(QHBoxLayout *body);
    QGridLayout *BuildParameters();
    QDialogButtonBox *BuildFooter(bool totalMode);
    void CopyReport();
    void UpdateWrappedWidths(int width);
    QGridLayout *metrics_ = nullptr;
    QGridLayout *BuildMetrics();
    QGridLayout *BuildSummary();
    QLabel *label(unsigned id, const char *name, const QString &text = {});
    QLabel *rightLabel(unsigned id, const char *name, const QString &text = {});
    QComboBox *combo(unsigned id, const char *name);
    QWidget *Register(unsigned id, const char *name, QWidget *widget);
    static QString ResourceCaption(unsigned id);
    static QString QtLangText(unsigned id);

  protected:
    virtual void OnInit() {}
    virtual void OnDestroy() {}
    virtual void OnMessage(unsigned, quintptr) {}
    virtual void OnCommand(unsigned, unsigned) {}
    virtual void OnButtonClicked(unsigned) {}
    virtual void OnTimer(quintptr) {}
    virtual void OnHelp() {}
    virtual void OnCancel();
    QWidget *GetItem(unsigned id) const { return controls_.value(id); }
    void SetItemText(unsigned id, const UString &text);
    void SetItemTextA(unsigned id, const AString &text);
    void SetItemText_Empty(unsigned id) { SetItemText(id, UString()); }
    void GetItemText(unsigned id, UString &text) const;
    void EnableItem(unsigned id, bool enabled);
    QWidget *GetFocus() const;
    UInt32 GetTickCount() const;
    void SendMsg_NextDlgCtl_CtlId(unsigned id);
    void NormalizeSize(bool fullNormalize);
    void NormalizePosition();
    quintptr SetTimer(quintptr id, unsigned milliseconds);
    void KillTimer(quintptr);
    void Attach(QWidget *) { postsEnabled_ = false; }
    qintptr Create(unsigned resource, QWidget *parent);
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *object, QEvent *event) override;

  public:
    bool PostMsg(unsigned message, quintptr wparam);
    explicit CQtBenchmarkWindow(QWidget *parent = nullptr);
    void reject() override;
};
