#pragma once
#include "archive/callbacks/ArchiveOpenCallback.h"
#include "archive/callbacks/archivepasswordretry.h"
#include "crypto/fscrypt/fscryptkeyrequest.h"
#include <QString>

// Presentation projection only; native detection/volume callbacks stay upstream.
class CNativeOpenProgress final : public IOpenCallbackUI,
                                  public IFscryptKeyUI,
                                  public IArchivePasswordRetryUI {
    IProgress *_progress;
    IOpenCallbackUI *_source;
    IArchiveOpenCallback *_open;

  public:
    QString Password;
    CNativeOpenProgress(IProgress *progress, IOpenCallbackUI *source,
                        IArchiveOpenCallback *open = nullptr)
        : _progress(progress), _source(source), _open(open) {}
    HRESULT Open_CheckBreak() override;
    HRESULT Open_SetTotal(const UInt64 *files, const UInt64 *bytes) override;
    HRESULT Open_SetCompleted(const UInt64 *files, const UInt64 *bytes) override;
    HRESULT Open_Finished() override;
    HRESULT Open_CryptoGetTextPassword(BSTR *password) override;
    HRESULT Open_FscryptKeys(BSTR *keys) override;
    HRESULT ResetArchivePassword() override;
};
