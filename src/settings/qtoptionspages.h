#pragma once
#include <QTabWidget>

// Page activation and focus adaptation for the native options workflow.
class QtOptionsPages final : public QTabWidget {
  public:
    explicit QtOptionsPages(QWidget *parent);
    void focusInitialControl();

  private:
    void keyPressEvent(QKeyEvent *event) override;
};
