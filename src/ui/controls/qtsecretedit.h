#pragma once
#include <QLineEdit>
#include <QPointer>

class QAbstractButton;

// Presentation only: the dialog retains ownership of the secret and its request.
class QtSecretEdit final : public QLineEdit {
  public:
    explicit QtSecretEdit(QWidget *parent = nullptr);
    void BindRevealControl(QAbstractButton &control);

  protected:
    void focusOutEvent(QFocusEvent *event) override;

  private:
    void SetRevealed(bool revealed);
    QPointer<QAbstractButton> reveal_;
};
