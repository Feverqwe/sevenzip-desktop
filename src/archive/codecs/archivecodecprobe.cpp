#include "archive/codecs/archivecodecprobe.h"
#include "Common/StringConvert.h"
#include "archive/codecs/archivecodelocation.h"
#include "localization/qtlanguageservices.h"
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QScopeGuard>
#include <atomic>
#include <new>
#include <utility>

struct ArchiveCodecProbe::Work {
    std::atomic<bool> cancelled{false};
    std::atomic<bool> timedOut{false};
    const QDeadlineTimer deadline;
    ArchiveCodecProbeResult result;
    std::function<void(ArchiveCodecProbeResult)> completion;

    Work(int timeoutMs, std::function<void(ArchiveCodecProbeResult)> callback)
        : deadline(qMax(1, timeoutMs), Qt::PreciseTimer), completion(std::move(callback)) {}

    bool Stopped() {
        if (deadline.hasExpired())
            timedOut = true;
        return cancelled || timedOut;
    }

    void Run(const QString &module) {
        try {
            if (!Stopped()) {
                const auto catalog = LoadCatalog(module);
                result = {catalog->result(), catalog->error(), catalog};
            }
        } catch (const std::bad_alloc &) {
            result.result = E_OUTOFMEMORY;
        } catch (...) {
            result.result = E_FAIL;
        }
        // Native DLL calls cannot be interrupted safely. Account for their
        // elapsed time before queueing the joined result back to the GUI.
        Stopped();
    }
};

std::shared_ptr<const ArchiveCodecCatalog> ArchiveCodecProbe::LoadCatalog(const QString &module) {
    // CCodecs::LoadDll obtains ABI/version metadata through GetModuleProp;
    // original directory loading and SetLibCodecs bind the retained providers.
    return std::make_shared<ArchiveCodecCatalog>(module.isEmpty() ? FindArchiveModule() : module, 0,
                                                 ArchiveCodecCatalog::Scope::Deployment);
}

ArchiveCodecProbe::ArchiveCodecProbe(QObject *parent) : QObject(parent) {}

int ArchiveCodecProbe::DefaultTimeoutMs() {
    // Instrumented format modules incur extra initialization and I/O work.
    // The default must accept a successfully loaded development catalog.
#if defined(__clang__)
    constexpr bool instrumented = __has_feature(address_sanitizer);
#elif defined(__SANITIZE_ADDRESS__)
    constexpr bool instrumented = true;
#else
    constexpr bool instrumented = false;
#endif
    return instrumented ? 30000 : 5000;
}

ArchiveCodecProbe::~ArchiveCodecProbe() {
    Cancel();
    if (loader_) {
        disconnect(loader_.get(), nullptr, this, nullptr);
        // Native DLL entrypoints cannot be interrupted safely. Join retains
        // immutable inputs/results and module leases through that work.
        loader_->wait();
    }
}

bool ArchiveCodecProbe::Start(const QString &module,
                              std::function<void(ArchiveCodecProbeResult)> completion,
                              int timeoutMs) {
    if (work_)
        return false;
    try {
        const auto work = std::make_shared<Work>(timeoutMs, std::move(completion));
        std::unique_ptr<QThread> loader(QThread::create([work, module] { work->Run(module); }));
        const auto stopUnpublished = qScopeGuard([&] {
            if (loader) {
                work->cancelled = true;
                loader->wait();
            }
        });
        auto *thread = loader.get();
        connect(
            thread, &QThread::finished, this,
            [this, work, thread] {
                if (loader_.get() != thread || work_ != work)
                    return;
                loader_->wait();
                loader_.reset();
                Complete();
            },
            Qt::QueuedConnection);
        loader->start();
        if (!loader->isRunning() && !loader->isFinished())
            return false;
        // Publish only fully prepared owners. Queued delivery cannot consume
        // this state until Start has returned to the owning event loop.
        work_ = work;
        loader_ = std::move(loader);
        return true;
    } catch (...) {
        return false;
    }
}

void ArchiveCodecProbe::Cancel() {
    if (work_)
        work_->cancelled = true;
}

void ArchiveCodecProbe::Complete() {
    const auto work = std::move(work_);
    auto &result = work->result;
    if (work->cancelled || work->timedOut) {
        result.catalog.reset();
        result.result = E_ABORT;
        result.error =
            work->timedOut
                ? QCoreApplication::translate("archivecodecprobe", "Codec discovery timed out.")
                : QCoreApplication::translate("archivecodecprobe", "Codec discovery cancelled.");
    }
    const auto completion = std::move(work->completion);
    if (completion)
        completion(std::move(result));
}
