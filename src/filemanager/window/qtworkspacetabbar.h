#pragma once
#include <QTabBar>

class QtWindowWorkspace;

class QtWorkspaceTabBar final : public QTabBar {
  public:
    QtWorkspaceTabBar(QtWindowWorkspace &workspace, unsigned panel, QWidget *parent);

  protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

  private:
    QtWindowWorkspace &workspace_;
    unsigned panel_;
    QPoint pressed_;
    QString pressedId_;
};
