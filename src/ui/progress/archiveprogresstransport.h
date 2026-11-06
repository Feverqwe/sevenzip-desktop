// Owned external-process controls shared by source opening and update workers.
#pragma once
#include "ui/progress/qtprogresstransport.h"
#include <QObject>
#include <QPointer>

class ArchiveBackend;
class ArchiveProgressTransport final : public QObject, public IQtProgressTransport {
    CProgressDialog &_dialog;
    QPointer<ArchiveBackend> _backend;
    bool _completed = false;
    bool _nativeWorker = false;

  public:
    ArchiveProgressTransport(CProgressDialog &dialog, ArchiveBackend &backend,
                             bool nativeWorker = false);
    ~ArchiveProgressTransport() override;
    HRESULT SetPaused(bool paused) override;
    bool UsesPercentProgress() const override;
    void Cancel() override;
};
