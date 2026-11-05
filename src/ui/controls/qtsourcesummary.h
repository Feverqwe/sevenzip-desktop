#pragma once
#include <QIcon>
#include <QWidget>

class QLabel;

// Shared two-line source presentation; callers retain source values and actions.
class QtSourceSummary final : public QWidget {
  public:
    QtSourceSummary(const QIcon &icon, const QString &name, const QString &path,
                    QWidget *parent = nullptr);
    void SetSelected(bool selected);
    int heightForWidth(int width) const override;
    bool hasHeightForWidth() const override { return true; }

  private:
    QLabel *name_;
    QLabel *path_;
};
