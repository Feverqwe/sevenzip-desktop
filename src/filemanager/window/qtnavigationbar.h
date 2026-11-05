#pragma once

#include <QCoreApplication>
#include <QWidget>
#include <functional>

class QComboBox;
class QHBoxLayout;
class QStackedWidget;

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
                    QWidget *parent = nullptr);
    void Edit();
    void FinishEditing();
    void SetLocation(const QString &path, const QString &container = {});

  protected:
    bool eventFilter(QObject *object, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

  private:
    void Rebuild();
    QComboBox &address_;
    QStackedWidget *stack_;
    QWidget *crumbs_;
    QHBoxLayout *layout_;
    QString path_;
    QString container_;
    QString rootLabel_;
    int visibleCrumbs_ = 3;
    std::function<void(const QString &)> navigate_;
};
