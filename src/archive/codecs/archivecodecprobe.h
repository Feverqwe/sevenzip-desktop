#pragma once

#include "archive/codecs/archivecodeccatalog.h"
#include <QObject>
#include <QThread>
#include <functional>
#include <memory>

struct ArchiveCodecProbeResult {
    HRESULT result = E_FAIL;
    QString error;
    std::shared_ptr<const ArchiveCodecCatalog> catalog;
};

// Joined native discovery; format semantics and ABI checks remain in CCodecs.
class ArchiveCodecProbe final : public QObject {
    struct Work;
    std::unique_ptr<QThread> loader_;
    std::shared_ptr<Work> work_;
    static int DefaultTimeoutMs();
    static ArchiveCodecProbeResult Discover(const QString &module,
                                            const std::shared_ptr<Work> &work);
    void Complete(ArchiveCodecProbeResult result);

  public:
    // Blocking native loading for joined workers; never call on the GUI thread.
    static std::shared_ptr<const ArchiveCodecCatalog> LoadCatalog(const QString &module = {});
    explicit ArchiveCodecProbe(QObject *parent = nullptr);
    ~ArchiveCodecProbe() override;
    // Reject active or failed startup without publishing a partial operation.
    bool Start(const QString &module, std::function<void(ArchiveCodecProbeResult)> completion,
               int timeoutMs = DefaultTimeoutMs());
    void Cancel();
    bool active() const { return bool(work_); }
};
