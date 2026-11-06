#pragma once
#include <QToolButton>

class QMenu;

// A projection of independent operation owners, not a task controller.
class QtOperationsButton final : public QToolButton {
  public:
    explicit QtOperationsButton(QWidget *parent = nullptr);
    void SetCompact(bool compact);

  protected:
    void changeEvent(QEvent *event) override;

  private:
    void Refresh();
    void Populate();
    QMenu *menu_;
};
