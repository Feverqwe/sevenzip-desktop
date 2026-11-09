#pragma once
#include <QObject>
#include <QTimer>
#include <functional>
#include <memory>

class MainWindow;
class CPanel;
class QAbstractItemView;

// Owns one inline editor through commit/cancel and deferred source delivery.
class QtRenameController final : public QObject {
  public:
    explicit QtRenameController(MainWindow &window);
    void Attach(QAbstractItemView &view, unsigned panel);
    void Begin(int row);
    void Cancel(unsigned panel);
    void Stop();
    bool Pending() const;
    bool PendingFor(unsigned panel) const;
    void AfterEdit(unsigned panel, std::function<void()> refresh);

  private:
    struct Request;
    void Complete();
    void Apply(const Request &request);
    void Finish(const Request &request, CPanel &panel);
    MainWindow &window_;
    QTimer completion_;
    std::shared_ptr<Request> request_;
};
