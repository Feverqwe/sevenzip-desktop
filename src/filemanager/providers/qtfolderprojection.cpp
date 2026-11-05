#include "filemanager/providers/qtfolderprojection.h"
#include "Common/MyCom.h"
#include <QElapsedTimer>
#include <QEventLoop>
#include <QList>
#include <QPointer>
#include <utility>

struct QtFolderProjection::Completion {
    HRESULT result = S_OK;
    bool done = false;
    QList<QPointer<QEventLoop>> waiters;
    void settle(HRESULT value) {
        if (done)
            return;
        result = value;
        done = true;
        for (const auto &waiter : waiters)
            if (waiter)
                waiter->quit();
    }
};

struct QtFolderProjection::Request {
    Request(IFolderFolder *source, quint32 size, std::function<HRESULT(quint32)> appendRow,
            std::function<void(HRESULT)> finished)
        : folder(source), count(size), append(std::move(appendRow)),
          completed(std::move(finished)) {}
    CMyComPtr<IFolderFolder> folder;
    const quint32 count;
    quint32 next = 0;
    std::function<HRESULT(quint32)> append;
    std::function<void(HRESULT)> completed;
    std::shared_ptr<Completion> completion = std::make_shared<Completion>();
};

QtFolderProjection::QtFolderProjection(QObject *parent) : QObject(parent) {
    timer_.setInterval(0);
    connect(&timer_, &QTimer::timeout, this, &QtFolderProjection::batch);
}

void QtFolderProjection::cancel() {
    timer_.stop();
    const auto request = std::exchange(request_, nullptr);
    if (request)
        request->completion->settle(E_ABORT);
}

HRESULT QtFolderProjection::waitForFinished() {
    const auto completion = completion_;
    if (!completion)
        return S_OK;
    if (!completion->done) {
        QEventLoop loop;
        completion->waiters.append(&loop);
        connect(this, &QObject::destroyed, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return completion->done ? completion->result : E_ABORT;
}

void QtFolderProjection::start(IFolderFolder *folder, quint32 count,
                               std::function<HRESULT(quint32)> append,
                               std::function<void(HRESULT)> completed) {
    auto request =
        folder ? std::make_shared<Request>(folder, count, std::move(append), std::move(completed))
               : nullptr;
    cancel();
    request_ = std::move(request);
    completion_ = request_ ? request_->completion : nullptr;
    if (request_)
        timer_.start();
}

void QtFolderProjection::batch() {
    const auto request = request_;
    if (!request)
        return;
    const QPointer<QtFolderProjection> guard(this);
    QElapsedTimer elapsed;
    elapsed.start();
    HRESULT result = S_OK;
    // Include property projection and native icon/row creation in the budget.
    for (unsigned rows = 0; rows < 32 && elapsed.elapsed() < 8 && request->next < request->count;
         ++rows) {
        result = request->append(request->next++);
        if (!guard || request != request_)
            return;
        if (result != S_OK)
            break;
    }
    if (result == S_OK && request->next < request->count)
        return;
    timer_.stop();
    request_.reset();
    if (request->completed)
        request->completed(result);
    request->completion->settle(result);
}
