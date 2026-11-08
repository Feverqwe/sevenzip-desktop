#pragma once

#include <QWidget>
#include <functional>

class QLineEdit;
class QtWrappingLabel;

class QtInlineFolderName final : public QWidget {
    QLineEdit *name_;
    QtWrappingLabel *error_;
    std::function<void()> cancel_;

    bool eventFilter(QObject *watched, QEvent *event) override;

  public:
    explicit QtInlineFolderName(QWidget *parent);
    void OnCancel(std::function<void()> cancel);
    QLineEdit *NameField() const { return name_; }
    void ClearError();
    void ShowError(const QString &message, bool focusName = true);
};
