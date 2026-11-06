#pragma once
#include <QWidget>

class QCheckBox;
class QtWrappingLabel;

// Native checkbox with a wrapping, clickable caption for narrow forms.
class QtWrappingCheckBox final : public QWidget {
  public:
    explicit QtWrappingCheckBox(QWidget *parent = nullptr);
    QCheckBox *checkBox() const { return check_; }
    QtWrappingLabel *label() const { return label_; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

  private:
    QCheckBox *check_;
    QtWrappingLabel *label_;
    bool pressed_ = false;
};
