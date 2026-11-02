// Qt presentation for the original LinkDialog workflow.
#pragma once
#include "Common/MyString.h"
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QHash>

class QVBoxLayout;
class CQtLinkCombo {
    QComboBox *view_ = nullptr;

  public:
    void Attach(QWidget *view) { view_ = qobject_cast<QComboBox *>(view); }
    void SetText(const UString &text) { view_->setEditText(QString::fromWCharArray(text.Ptr())); }
    bool GetText(UString &text) const {
        const QString value = view_->currentText();
        if (value.contains(QChar::Null))
            return false;
        text = QDir::toNativeSeparators(value).toStdWString().c_str();
        return true;
    }
    void SetCurSel(int index) { view_->setCurrentIndex(index); }
};

class CQtLinkWindow : public QDialog {
    QHash<unsigned, QWidget *> controls_;
    QWidget *Register(unsigned id, const char *name, QWidget *view, const QString &caption = {});
    void BuildControls();
    QWidget *BuildLinkTypes();
    void AddPathRow(QVBoxLayout *layout, bool to);

  protected:
    virtual bool OnInit() { return true; }
    virtual bool OnSize(quintptr, int, int) { return false; }
    virtual bool OnButtonClicked(unsigned, QWidget *) { return false; }
    QWidget *GetItem(unsigned id) const { return controls_.value(id); }
    void SetItemText(unsigned id, const UString &text);
    void CheckRadioButton(unsigned first, unsigned last, unsigned checked);
    bool IsButtonCheckedBool(unsigned id) const;
    void NormalizeSize();
    void End(int result) { done(result); }
    qintptr Create(unsigned resource, QWidget *parent);
    void resizeEvent(QResizeEvent *event) override;

  public:
    using QDialog::QDialog;
};
