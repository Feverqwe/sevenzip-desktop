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
    bool active() const { return bool(request_); }

  private:
    void batch();
    struct Request;
    std::shared_ptr<Request> request_;
    QTimer timer_{this};
};
