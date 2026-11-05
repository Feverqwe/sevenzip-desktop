#pragma once
#include "7zip/UI/FileManager/IFolder.h"
#include <QObject>
#include <QTimer>
#include <functional>
#include <memory>

// GUI scheduling only. The source folder still owns items and real indices;
// this retains it until all rows have been presented or navigation cancels.
class QtFolderProjection final : public QObject {
  public:
    explicit QtFolderProjection(QObject *parent = nullptr);
    void start(IFolderFolder *folder, quint32 count, std::function<HRESULT(quint32)> append,
               std::function<void(HRESULT)> completed);
    void cancel();
    // Joined callers retain their operation guard while the normal bounded
    // GUI batches finish. No provider/stream ownership escapes the scheduler.
    HRESULT waitForFinished();
    bool active() const { return bool(request_); }

  private:
    void batch();
    struct Request;
    struct Completion;
    std::shared_ptr<Request> request_;
    std::shared_ptr<Completion> completion_;
    QTimer timer_{this};
};
