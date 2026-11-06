#include "ui/progress/archiveprogresstransport.h"
#include "archive/backend/archivebackend.h"
#include "ui/progress/ProgressDialog2.h"

ArchiveProgressTransport::ArchiveProgressTransport(CProgressDialog &dialog, ArchiveBackend &backend,
                                                   bool nativeWorker)
    : _dialog(dialog), _backend(&backend), _nativeWorker(nativeWorker) {
    _dialog.AttachTransport(this);
    connect(&backend, &ArchiveBackend::phaseStarted, this, [this] {
        _dialog.SetTransportPercent(-1);
        if (!_backend)
            return;
        const HRESULT result = _backend->setPaused(_dialog.Sync.Get_Paused());
        if (result != S_OK) {
            _dialog.Sync.AddError_Code_Name(result, nullptr);
            if (_backend)
                _backend->cancel();
        }
    });
    connect(&backend, &ArchiveBackend::progress, this, [this](int percent, const QString &detail) {
        _dialog.SetTransportPercent(percent);
        if (!detail.isEmpty()) {
            const std::wstring name = detail.toStdWString();
            _dialog.Sync.Set_FilePath(name.c_str(), false);
        }
    });
    connect(&backend, &ArchiveBackend::finished, this, [this] { _completed = true; });
}

ArchiveProgressTransport::~ArchiveProgressTransport() { _dialog.AttachTransport(nullptr); }

bool ArchiveProgressTransport::UsesPercentProgress() const { return _backend && _backend->busy(); }

HRESULT ArchiveProgressTransport::SetPaused(bool paused) {
    if (_backend && _backend->busy())
        return _backend->setPaused(paused);
    // Native source callbacks wait on the dialog's Sync pause state. A native
    // phase has no child process to suspend; metadata reopen still uses above.
    return _nativeWorker || _completed ? S_OK : E_FAIL;
}

void ArchiveProgressTransport::Cancel() {
    if (_backend)
        _backend->cancel();
}
