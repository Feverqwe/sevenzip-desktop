#pragma once
#include <QIcon>
#include <QWidget>

class QLabel;

// Source identity and optional semantic description; full paths are tooltips.
class QtSourceSummary final : public QWidget {
  public:
    QtSourceSummary(const QIcon &icon, const QString &name, const QString &path,
                    QWidget *parent = nullptr);
    void SetSelected(bool selected);
    void SetSource(const QString &name, const QString &path);
    void SetDescription(const QString &description);
    void SetDetail(const QString &detail, const QString &toolTip = {});
    void SetIdentityPresentation();
    void SetSelectable(bool selectable);
    int heightForWidth(int width) const override;
    bool hasHeightForWidth() const override { return true; }

  private:
    void RefreshIdentity();
    void RefreshAccessibleDescription();
    void changeEvent(QEvent *event) override;
    QIcon icon_;
    QLabel *symbol_;
    bool identity_ = false;
    QLabel *name_;
    QLabel *description_;
    QLabel *detail_;
    QString sourcePath_;
};
