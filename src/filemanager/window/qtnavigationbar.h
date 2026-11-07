#pragma once

#include "filemanager/panels/qtaddresssegment.h"
#include <QCoreApplication>
#include <QWidget>
#include <functional>
#include <vector>

class QComboBox;
class QAction;
class QHBoxLayout;
class QStackedWidget;
class QToolButton;

class QtNavigationRow final : public QWidget {
  public:
    explicit QtNavigationRow(QWidget *parent = nullptr);
    void SetActivePane(bool active);

  protected:
    void paintEvent(QPaintEvent *event) override;

  private:
    bool active_ = false;
};

// Presentation of the existing source address control. Native binding and
// address validation remain with CPanel and QtPanelAddressCombo.
class QtNavigationBar final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(QtNavigationBar)
  public:
    QtNavigationBar(QComboBox &address, std::function<void(const QString &)> navigate,
                    QWidget *parent = nullptr, std::function<void()> beginEdit = {});
    void Edit();
    void ResumeEditing();
    void FinishEditing();
    void SetLocation(const QString &path, const std::vector<QtAddressSegment> &segments = {});

  protected:
    bool eventFilter(QObject *object, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

  private:
    void Rebuild();
    void FitCrumbs();
    class CrumbButton;
    struct Crumb {
        CrumbButton *button;
        QWidget *separator;
        QAction *ancestor;
    };
    QComboBox &address_;
    QStackedWidget *stack_;
    QWidget *crumbs_;
    QHBoxLayout *layout_;
    QString path_;
    std::vector<QtAddressSegment> segments_;
    QString rootLabel_;
    std::vector<Crumb> crumbButtons_;
    QToolButton *ancestors_ = nullptr;
    QToolButton *edit_ = nullptr;
    std::function<void(const QString &)> navigate_;
    std::function<void()> beginEdit_;
};
