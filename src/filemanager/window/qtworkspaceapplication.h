#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <functional>
#include <vector>

class MainWindow;
class QApplication;

// One application owner writes the complete window session, so closing one
// window cannot overwrite its neighbours' tabs with legacy panel preferences.
class QtWorkspaceApplication final : public QObject {
  public:
    explicit QtWorkspaceApplication(QApplication &application);
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
};
