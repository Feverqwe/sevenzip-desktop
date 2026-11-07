#pragma once
#include <QIcon>
#include <QWidget>

class QLabel;
class QAction;
class QPlainTextEdit;
class QPushButton;
class QtDisclosure;
class QtSourceSummary;

struct QtMountRowData {
    QString name, sourcePath, mountPath, status, error, report;
    bool canOpen = false, canUnmount = false, canForce = false, canRemove = false;
    bool preparing = false;
};

// Presentation only; the manager resolves each action against its live session.
class QtMountRow final : public QWidget {
    Q_OBJECT
  public:
    enum class Action { Open, CopyPath, Unmount, ForceUnmount, Remove };
    Q_ENUM(Action)
    explicit QtMountRow(const QIcon &icon, QWidget *parent = nullptr);
    void SetData(const QtMountRowData &data);
    void FocusPrimaryAction();
    int heightForWidth(int width) const override;
    bool hasHeightForWidth() const override { return true; }

  signals:
    void Requested(QtMountRow::Action action);

  protected:
    void paintEvent(QPaintEvent *event) override;

  private:
    QtSourceSummary *source_;
    QLabel *error_;
    QPlainTextEdit *report_;
    QtDisclosure *details_;
    QPushButton *open_, *copy_, *unmount_, *force_, *remove_;
    QAction *removeAction_;
};
