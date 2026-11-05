#pragma once
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QUuid>
#include <functional>
#include <vector>

class MainWindow;
class QApplication;
class QtTaskResultDialog;
class QWidget;

// One application owner writes the complete window session, so closing one
// window cannot overwrite its neighbours' tabs with legacy panel preferences.
class QtWorkspaceApplication final : public QObject {
  public:
    explicit QtWorkspaceApplication(QApplication &application);
    ~QtWorkspaceApplication() override;
    static QtWorkspaceApplication *Find();
    void Register(MainWindow &window);
    void Closing(MainWindow &window);
    void Restore(MainWindow &first);
    MainWindow *NewWindow(const QString &path, const QJsonObject &session = {},
                          std::function<void(MainWindow &)> ready = {});
    void Save();
    void SetAutoRefresh(bool enabled);
    void Quit();

  private:
    void ApplyWhenReady(MainWindow &window, const QJsonObject &state,
                        std::function<void()> ready = {});
    std::vector<QPointer<MainWindow>> windows_;
    QJsonArray ReadSession() const;
    bool restoring_ = false;
    bool quitting_ = false;
    unsigned pendingRestores_ = 0;
    void InitializeTaskResults();
    void ShowTaskReview(const QUuid &id, bool activate);
    bool eventFilter(QObject *object, QEvent *event) override;
    void PresentTaskReviews();
    QPointer<QWidget> reviewModal_;
    QHash<QUuid, QPointer<QtTaskResultDialog>> taskResults_;
};
