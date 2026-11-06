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
    void SetSource(const QString &name, const QString &path);
    void SetDetail(const QString &detail);
    void SetIdentityPresentation();
    void SetSelectable(bool selectable);
    int heightForWidth(int width) const override;
    bool hasHeightForWidth() const override { return true; }

  private:
    void RefreshIdentity();
    void changeEvent(QEvent *event) override;
    QIcon icon_;
    QLabel *symbol_;
    bool identity_ = false;
    QLabel *name_;
    QLabel *path_;
    QLabel *detail_;
};
