// Private callback projections for native archive reading.
// Igor Pavlov, LGPL-2.1-or-later; selected original units in upstream-baseline.json.
#pragma once
#include "7zip/Archive/IArchive.h"
#include "7zip/IPassword.h"
#include "Common/MyCom.h"
#include "archive/callbacks/ArchiveOpenCallback.h"
#include "archive/callbacks/IFileExtractCallback.h"
#include "archive/callbacks/archivepasswordretry.h"
#include <QMutex>
#include <QString>

class CArc;
class QtArchiveReadOperation;

class QtArchiveReadCallback final : public IArchiveExtractCallback,
                                    public IArchiveExtractCallbackMessage2,
                                    public ICryptoGetTextPassword,
                                    public IArchiveRequestMemoryUseCallback,
                                    public IOpenCallbackUI,
                                    public IArchivePasswordRetryUI,
                                    public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_5(IArchiveExtractCallback, IProgress, ICryptoGetTextPassword,
                         IArchiveRequestMemoryUseCallback, IArchiveExtractCallbackMessage2)
    QtArchiveReadOperation &operation_;
    const CArc *archive_ = nullptr;
    UInt64 total_ = 0;
    struct Item {
        QString Path;
        bool Encrypted = false;
        UInt32 Index = UInt32(-1);
    };
    Item current_;
    UInt32 outputIndex_ = UInt32(-1);
    CMyComPtr<ISequentialOutStream> output_;
    QMutex stateMutex_;
    HRESULT ReadItem(UInt32 index, Item &item);

  public:
    explicit QtArchiveReadCallback(QtArchiveReadOperation &operation);
    void Attach(const CArc *archive) { archive_ = archive; }
    void SetOutput(UInt32 index, ISequentialOutStream *output) {
        outputIndex_ = index;
        output_ = output;
    }
    Z7_COM7F_IMF(SetTotal(UInt64 total)) override;
    Z7_COM7F_IMF(SetCompleted(const UInt64 *completed)) override;
    Z7_COM7F_IMF(GetStream(UInt32 index, ISequentialOutStream **stream, Int32 mode)) override;
    Z7_COM7F_IMF(PrepareOperation(Int32)) override;
    Z7_COM7F_IMF(SetOperationResult(Int32 result)) override;
    Z7_COM7F_IMF(ReportExtractResult(UInt32 indexType, UInt32 index, Int32 result)) override;
    Z7_COM7F_IMF(CryptoGetTextPassword(BSTR *value)) override;
    Z7_COM7F_IMF(RequestMemoryUse(UInt32 flags, UInt32, UInt32, const wchar_t *, UInt64 required,
                                  UInt64 *allowed, UInt32 *answer))
    override;
    HRESULT Open_CheckBreak() override;
    HRESULT Open_SetTotal(const UInt64 *, const UInt64 *bytes) override;
    HRESULT Open_SetCompleted(const UInt64 *, const UInt64 *bytes) override;
    HRESULT Open_Finished() override;
    HRESULT Open_CryptoGetTextPassword(BSTR *value) override;
    HRESULT ResetArchivePassword() override;
};
