// SPDX-License-Identifier: GPL-2.0-or-later
// Local COM boundary; file bytes are read by the shared Linux-derived C reader.
#include "formats/exfat/exfatstream.h"
#include "7zip/Common/StreamUtils.h"
#include "Common/ComTry.h"
#include <cerrno>

namespace NArchive::NExfat {
int ReadContext::Poll(void *context) {
    auto &self = *static_cast<ReadContext *>(context);
    if (self.CallbackResult != S_OK || self.TransportResult == E_ABORT ||
        self.TransportResult == E_OUTOFMEMORY)
        return -EIO;
    if (self.OpenCallback)
        self.CallbackResult = self.OpenCallback->SetCompleted(&self.Files, nullptr);
    else if (self.ExtractCallback)
        self.CallbackResult = self.ExtractCallback->SetCompleted(&self.Completed);
    if (self.CallbackResult != S_OK)
        self.TransportResult = self.CallbackResult;
    return self.CallbackResult == S_OK ? 0 : -EIO;
}
int ReadContext::VisitCluster(void *context, uint32_t cluster) {
    auto &self = *static_cast<ReadContext *>(context);
    try {
        return self.VisitedClusters.insert(cluster).second ? 0 : -EIO;
    } catch (...) {
        self.TransportResult = E_OUTOFMEMORY;
        return -ENOMEM;
    }
}
int ReadContext::Read(void *context, uint64_t offset, void *buffer, size_t size) {
    auto &self = *static_cast<ReadContext *>(context);
    if (self.CallbackResult != S_OK || self.TransportResult == E_ABORT ||
        self.TransportResult == E_OUTOFMEMORY)
        return -EIO;
    self.TransportResult = InStream_SeekSet(self.Stream, offset);
    if (self.TransportResult == S_OK)
        self.TransportResult = ReadStream_FALSE(self.Stream, buffer, size);
    return self.TransportResult == S_OK ? 0 : -EIO;
}

namespace {
class ItemStream final : public IInStream, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_2(IInStream, ISequentialInStream)
    Z7_IFACE_COM7_IMP(IInStream)
    Z7_IFACE_COM7_IMP(ISequentialInStream)
    // The callbacks target this retained input, not a closed/reopened handler.
    std::shared_ptr<ReadContext> input_;
    std::shared_ptr<exfat_volume> volume_;
    exfat_item item_;
    exfat_cursor cursor_{};
    std::set<UInt32> visited_;
    std::mutex mutex_;

  public:
    ItemStream(std::shared_ptr<ReadContext> input, std::shared_ptr<exfat_volume> volume,
               const exfat_item &item)
        : input_(std::move(input)), volume_(std::move(volume)), item_(item) {
        exfat_item_cursor(&item_, &cursor_);
    }
};
Z7_COM7F_IMF(ItemStream::Read(void *data, UInt32 size, UInt32 *processed)) {
    COM_TRY_BEGIN
    if (processed)
        *processed = 0;
    std::lock_guard lock(mutex_);
    if (!size || cursor_.position >= item_.size)
        return S_OK;
    if (!data)
        return E_INVALIDARG;
    std::lock_guard inputLock(input_->StreamMutex);
    input_->TransportResult = input_->CallbackResult = S_OK;
    struct VisitScope {
        std::set<UInt32> &input, &stream;
        VisitScope(std::set<UInt32> &a, std::set<UInt32> &b) : input(a), stream(b) {
            input.swap(stream);
        }
        ~VisitScope() { input.swap(stream); }
    } scope(input_->VisitedClusters, visited_);
    size_t actual = 0;
    const int error = exfat_read_item(volume_.get(), &item_, &cursor_, data, size, &actual);
    if (input_->TransportResult != S_OK)
        return input_->TransportResult;
    if (error)
        return error == -ENOMEM ? E_OUTOFMEMORY : E_FAIL;
    if (processed)
        *processed = UInt32(actual);
    return S_OK;
    COM_TRY_END
}
Z7_COM7F_IMF(ItemStream::Seek(Int64 offset, UInt32 origin, UInt64 *position)) {
    COM_TRY_BEGIN
    std::lock_guard lock(mutex_);
    if (origin > STREAM_SEEK_END)
        return STG_E_INVALIDFUNCTION;
    const UInt64 base = origin == STREAM_SEEK_SET   ? 0
                        : origin == STREAM_SEEK_CUR ? cursor_.position
                                                    : item_.size;
    if ((offset < 0 && UInt64(-(offset + 1)) + 1 > base) ||
        (offset >= 0 && UInt64(offset) > UInt64(INT64_MAX) - base))
        return HRESULT_WIN32_ERROR_NEGATIVE_SEEK;
    const UInt64 next = base + UInt64(offset);
    if (next < cursor_.position) {
        exfat_item_cursor(&item_, &cursor_);
        visited_.clear();
    }
    cursor_.position = next;
    if (position)
        *position = next;
    return S_OK;
    COM_TRY_END
}
} // namespace
HRESULT CreateItemStream(std::shared_ptr<ReadContext> input, std::shared_ptr<exfat_volume> volume,
                         const exfat_item &item, ISequentialInStream **stream) {
    COM_TRY_BEGIN
    *stream = nullptr;
    if (!input || !volume || (item.attributes & 0x10) || item.size > INT64_MAX)
        return S_FALSE;
    CMyComPtr<ISequentialInStream> result =
        new ItemStream(std::move(input), std::move(volume), item);
    *stream = result.Detach();
    return S_OK;
    COM_TRY_END
}
} // namespace NArchive::NExfat
