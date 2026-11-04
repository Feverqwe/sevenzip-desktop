// Item/block and memory projections follow UI/Common/ArchiveExtractCallback.cpp
// and UI/FileManager/ExtractCallback.cpp at b88c6bc09339de32f7d18bbab900686b244f42ba.
// Stream ask-mode/output ownership follows UI/Client7z/Client7z.cpp at
// 9128b80e3a471108678e2b3ec3c985861c8d7b0f; see upstream-baseline.json.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "archive/opening/OpenArchive.h"
#include "archive/resources/qtarchivereadcallback_p.h"
#include "archive/resources/qtarchivereadoperation.h"
#include <QMutexLocker>
#include <utility>

HRESULT QtArchiveReadCallback::ReadItem(UInt32 index, Item &item) {
    if (!archive_)
        return E_FAIL;
    CReadArcItem arcItem;
    RINOK(archive_->GetItem(index, arcItem))
    item.Path = QString::fromWCharArray(arcItem.Path);
    item.Index = index;
    return Archive_GetItemBoolProp(archive_->Archive, index, kpidEncrypted, item.Encrypted);
}

QtArchiveReadCallback::QtArchiveReadCallback(QtArchiveReadOperation &operation)
    : operation_(operation) {}

Z7_COM7F_IMF(QtArchiveReadCallback::SetTotal(UInt64 total)) {
    {
        QMutexLocker lock(&stateMutex_);
        total_ = total;
    }
    return operation_.checkpoint();
}

Z7_COM7F_IMF(QtArchiveReadCallback::SetCompleted(const UInt64 *completed)) {
    UInt64 total;
    QString path;
    {
        QMutexLocker lock(&stateMutex_);
        total = total_;
        path = current_.Path;
    }
    if (completed)
        operation_.report(*completed, total, path);
    return operation_.checkpoint();
}

Z7_COM7F_IMF(QtArchiveReadCallback::GetStream(UInt32 index, ISequentialOutStream **stream,
                                              Int32 mode)) {
    if (!stream || !archive_)
        return E_INVALIDARG;
    *stream = nullptr;
    Item item;
    RINOK(ReadItem(index, item)) {
        QMutexLocker lock(&stateMutex_);
        current_ = std::move(item);
    }
    RINOK(operation_.checkpoint())
    // Client7z::CArchiveExtractCallback::GetStream returns file output only
    // in extract mode. A solid handler can also request skipped siblings.
    if (index == outputIndex_ && mode == NArchive::NExtract::NAskMode::kExtract) {
        CMyComPtr<ISequentialOutStream> output = output_;
        *stream = output.Detach();
    }
    return S_OK;
}

Z7_COM7F_IMF(QtArchiveReadCallback::PrepareOperation(Int32)) { return operation_.checkpoint(); }

Z7_COM7F_IMF(QtArchiveReadCallback::SetOperationResult(Int32 result)) {
    {
        QMutexLocker lock(&stateMutex_);
        if (output_ && current_.Index == outputIndex_)
            operation_.OutputOperation = result;
        // Extract's HRESULT alone does not account for CRC/partial item results.
        if (result != NArchive::NExtract::NOperationResult::kOK)
            operation_.Errors.append({current_.Path, result, current_.Encrypted});
    }
    return operation_.checkpoint();
}

Z7_COM7F_IMF(QtArchiveReadCallback::ReportExtractResult(UInt32 indexType, UInt32 index,
                                                        Int32 result)) {
    // ArchiveExtractCallback::ReportExtractResult's item/block projection.
    Item item;
    if (indexType == NArchive::NEventIndexType::kInArcIndex && index != UInt32(-1)) {
        RINOK(ReadItem(index, item))
    } else
        item.Path = '#' + QString::number(index);
    if (result != NArchive::NExtract::NOperationResult::kOK) {
        QMutexLocker lock(&stateMutex_);
        operation_.Errors.append({item.Path, result, item.Encrypted});
        if (output_ && indexType == NArchive::NEventIndexType::kInArcIndex && index == outputIndex_)
            operation_.OutputOperation = result;
    }
    return operation_.checkpoint();
}

Z7_COM7F_IMF(QtArchiveReadCallback::CryptoGetTextPassword(BSTR *value)) {
    return operation_.password(value);
}

Z7_COM7F_IMF(QtArchiveReadCallback::RequestMemoryUse(UInt32 flags, UInt32, UInt32, const wchar_t *,
                                                     UInt64 required, UInt64 *allowed,
                                                     UInt32 *answer)) {
    RINOK(operation_.checkpoint())
    if (!allowed || !answer)
        return E_INVALIDARG;
    // ExtractCallback::RequestMemoryUse's non-dialog fast path: preserve
    // the decoder's already allowed budget, without silently increasing it.
    if ((flags & NRequestMemoryUseFlags::k_IsReport) == 0 && required <= *allowed) {
        *answer = NRequestMemoryAnswerFlags::k_Allow;
        return S_OK;
    }
    return E_OUTOFMEMORY; // This non-dialog API has no memory-limit consent service.
}

HRESULT QtArchiveReadCallback::Open_CheckBreak() { return operation_.checkpoint(); }

HRESULT QtArchiveReadCallback::Open_SetTotal(const UInt64 *, const UInt64 *bytes) {
    return bytes ? SetTotal(*bytes) : operation_.checkpoint();
}

HRESULT QtArchiveReadCallback::Open_SetCompleted(const UInt64 *, const UInt64 *bytes) {
    return SetCompleted(bytes);
}

HRESULT QtArchiveReadCallback::Open_Finished() { return operation_.checkpoint(); }

HRESULT QtArchiveReadCallback::Open_CryptoGetTextPassword(BSTR *value) {
    return operation_.password(value);
}

HRESULT QtArchiveReadCallback::ResetArchivePassword() { return operation_.resetPassword(); }
