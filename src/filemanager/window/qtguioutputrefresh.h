#pragma once
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <array>
#include <functional>

// Independent children can finish while the manager is scanning or in a modal
// source call. Retain their affected paths until each current panel can reload.
class QtGuiOutputRefresh final : public QObject {
  public:
    struct Outputs {
        QStringList directories;
        QStringList archives;
        bool empty() const { return directories.isEmpty() && archives.isEmpty(); }
    };
    using Ready = std::function<bool(unsigned)>;
    using Refresh = std::function<void(unsigned, const Outputs &)>;
    QtGuiOutputRefresh(Ready ready, Refresh refresh);
    void Enqueue(const QStringList &directories, const QStringList &archives, int skipPanel = -1);
    void Stop();

  private:
    void Flush();
    Ready ready_;
    Refresh refresh_;
    std::array<Outputs, 2> pending_;
    QTimer timer_;
    bool draining_ = false;
    bool stopped_ = false;
};
