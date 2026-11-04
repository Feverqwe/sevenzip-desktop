// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2017 Simon Gander (apfs-fuse).
// DeviceSparseImage adaptation: native streams, validated index chains and ranges,
// variable band size, propagated I/O errors and bounded sparse reads.
#include "formats/sparse-image/sparseimagestream.h"
#include "7zip/Common/StreamUtils.h"
#include "Common/ComTry.h"
#include "platform/cpu/CpuArch.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <set>

namespace NArchive::NSparseImage {
namespace {
constexpr UInt64 NodeSize = 0x1000;
constexpr HRESULT UnexpectedEnd = static_cast<HRESULT>(0x80070026L);
} // namespace
HRESULT SparseImageStream::ReadAt(UInt64 offset, void *data, size_t size) {
    if (offset > physical_ || size > physical_ - offset)
        return UnexpectedEnd;
    RINOK(InStream_SeekSet(input_, offset))
    return ReadStream_FALSE(input_, data, size);
}
HRESULT SparseImageStream::Open(IInStream *input, IArchiveOpenCallback *callback) {
    COM_TRY_BEGIN
    input_ = input;
    bands_.clear();
    size_ = bandSize_ = position_ = 0;
    if (!input)
        return E_INVALIDARG;
    RINOK(InStream_AtBegin_GetSize(input_, physical_))
    if (physical_ < 4 || physical_ > INT64_MAX)
        return S_FALSE;
    std::array<Byte, NodeSize> node{};
    RINOK(ReadAt(0, node.data(), 4))
    if (memcmp(node.data(), "sprs", 4))
        return S_FALSE;
    RINOK(ReadAt(0, node.data(), node.size()))
    const UInt32 version = GetBe32(node.data() + 4);
    if (version != 1 && version != 3)
        return E_NOTIMPL;
    const UInt64 sectors = GetBe64(node.data() + 28);
    bandSize_ = UInt64(GetBe32(node.data() + 8)) * 512;
    if (!sectors || sectors > UInt64(INT64_MAX) / 512 || !bandSize_)
        return E_INVALIDARG;
    size_ = sectors * 512;
    const UInt64 bandCount = (size_ - 1) / bandSize_ + 1;
    UInt64 current = 0;
    std::set<UInt64> visited;
    std::map<UInt64, UInt64> extents;
    for (;;) {
        if (callback)
            RINOK(callback->SetCompleted(nullptr, nullptr))
        if (!visited.insert(current).second || visited.size() > 1048576)
            return E_INVALIDARG;
        const unsigned start = current ? 56 : 64;
        // The physical slots follow each index node, including unused slots.
        for (unsigned k = 0; k < (NodeSize - start) / 4; ++k) {
            const UInt32 id = GetBe32(node.data() + start + 4 * k);
            if (!id)
                continue;
            if (id > bandCount || bands_.contains(id - 1))
                return E_INVALIDARG;
            if (bands_.size() >= (1u << 20))
                return E_OUTOFMEMORY;
            if (current > physical_ - NodeSize ||
                UInt64(k) > (physical_ - current - NodeSize) / bandSize_)
                return UnexpectedEnd;
            const UInt64 begin = current + NodeSize + bandSize_ * k;
            const UInt64 length = std::min<UInt64>(bandSize_, size_ - UInt64(id - 1) * bandSize_);
            if (length > physical_ - begin)
                return UnexpectedEnd;
            bands_.emplace(id - 1, begin);
            if (!extents.emplace(begin, begin + length).second)
                return E_INVALIDARG;
        }
        const UInt64 next = GetBe64(node.data() + (current ? 12 : 20));
        if (!next)
            break;
        if (next > physical_ || NodeSize > physical_ - next)
            return E_INVALIDARG;
        current = next;
        RINOK(ReadAt(current, node.data(), node.size()))
        if (memcmp(node.data(), "sprs", 4))
            return E_INVALIDARG;
    }
    // Index nodes and populated data slots must not alias one another.
    for (UInt64 index : visited) {
        auto after = extents.lower_bound(index + NodeSize);
        if (after != extents.begin() && std::prev(after)->second > index)
            return E_INVALIDARG;
    }
    UInt64 end = 0;
    for (const auto &[begin, limit] : extents) {
        if (begin < end)
            return E_INVALIDARG;
        end = limit;
    }
    return S_OK;
    COM_TRY_END
}
Z7_COM7F_IMF(SparseImageStream::Seek(Int64 offset, UInt32 origin, UInt64 *position)) {
    if (origin > STREAM_SEEK_END)
        return STG_E_INVALIDFUNCTION;
    const UInt64 base = origin == STREAM_SEEK_SET   ? 0
                        : origin == STREAM_SEEK_CUR ? position_
                                                    : size_;
    if ((offset < 0 && UInt64(-(offset + 1)) + 1 > base) ||
        (offset >= 0 && UInt64(offset) > UInt64(INT64_MAX) - base))
        return HRESULT_WIN32_ERROR_NEGATIVE_SEEK;
    position_ = base + UInt64(offset);
    if (position)
        *position = position_;
    return S_OK;
}
Z7_COM7F_IMF(SparseImageStream::Read(void *data, UInt32 size, UInt32 *processed)) {
    COM_TRY_BEGIN
    if (processed)
        *processed = 0;
    if (!size || position_ >= size_)
        return S_OK;
    if (!data || !bandSize_)
        return E_FAIL;
    const UInt64 band = position_ / bandSize_, within = position_ % bandSize_;
    const UInt32 count = UInt32(std::min<UInt64>({size, size_ - position_, bandSize_ - within}));
    const auto found = band <= UINT32_MAX ? bands_.find(UInt32(band)) : bands_.end();
    if (found == bands_.end())
        memset(data, 0, count);
    else {
        RINOK(ReadAt(found->second + within, data, count))
    }
    position_ += count;
    if (processed)
        *processed = count;
    return S_OK;
    COM_TRY_END
}
} // namespace NArchive::NSparseImage
