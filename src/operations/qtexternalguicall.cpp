#include "operations/qtexternalguicall.h"
#include "filemanager/external/qtexternalfilesession.h"
#include <QCoreApplication>
#include <QMetaObject>
#include <QSemaphore>
#include <QThread>
#include <QTimer>
#include <QWidget>
#include <atomic>
#include <exception>
#include <memory>

namespace {
enum class Phase { Pending, Delivering, Cancelled };

struct Request {
    Request(const std::function<int()> &body, const std::function<bool()> &canDeliver)
        : body(body), canDeliver(canDeliver) {}
    QSemaphore ready;
    // Delivery retains worker captures through readiness and the callback.
    // Only Pending can be cancelled; modal closure uses CanCloseNow.
    std::atomic<Phase> phase{Phase::Pending};
    const std::function<int()> body;
    const std::function<bool()> canDeliver;
    int result = 0;
    std::exception_ptr error;
};

void Deliver(const std::shared_ptr<Request> &request, QObject *context) {
    auto pending = Phase::Pending;
    if (!request->phase.compare_exchange_strong(pending, Phase::Delivering)) {
        request->ready.release();
        return;
    }
    try {
        if (request->canDeliver && !request->canDeliver()) {
            request->phase.store(Phase::Pending);
            QTimer::singleShot(25, context, [request, context] { Deliver(request, context); });
            return;
        }
        request->result = request->body();
    } catch (...) {
        // Retire delivery even if readiness or rescheduling failed first.
        request->phase.store(Phase::Delivering);
        request->error = std::current_exception();
    }
    request->ready.release();
}
} // namespace

int QtExternalGuiCall(QWidget *window, CExitEventLauncher &launcher,
                      const std::function<int()> &body, const std::function<bool()> &ready) {
    if (launcher.Exiting())
        return 0;
    QObject *context = window ? static_cast<QObject *>(window) : QCoreApplication::instance();
    if (!context)
        return 0;
    if (QThread::currentThread() == context->thread())
        return body();
    const auto request = std::make_shared<Request>(body, ready);
    QMetaObject::invokeMethod(
        context, [request, context] { Deliver(request, context); }, Qt::QueuedConnection);
    while (!request->ready.tryAcquire(1, 25)) {
        if (launcher.Exiting()) {
            auto pending = Phase::Pending;
            if (request->phase.compare_exchange_strong(pending, Phase::Cancelled) ||
                pending == Phase::Cancelled)
                return 0;
        }
    }
    if (request->error)
        std::rethrow_exception(request->error);
    return request->result;
}
