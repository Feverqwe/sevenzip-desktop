// LGPL-2.1-or-later; Igor Pavlov, 7-Zip.
// Resource-map validation and chunk layout adapted from the complete original
// HfsHandler.cpp CDecoder resource readers; see the recorded source baseline.
// Local seek/cache boundary reads one independent compression block at a time.
#include "7zip/Archive/StdAfx.h"
#include "7zip/Common/StreamObjects.h"
#include "7zip/Common/StreamUtils.h"
#include "Common/ComTry.h"
#include "formats/hfs/HfsHandler.h"
#include "platform/cpu/CpuArch.h"
#include <algorithm>
#include <cstring>

namespace NArchive::NHfs {
namespace {
class CDecmpfsStream final : public IInStream, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_2(IInStream, ISequentialInStream)
    Z7_IFACE_COM7_IMP(IInStream)
    Z7_IFACE_COM7_IMP(ISequentialInStream)
    CMyComPtr<IInStream> source_;
    CCompressHeader header_;
    CByteBuffer table_, packed_, cache_, inline_;
    CDecoder decoder_{true};
    UInt64 forkSize_ = 0, position_ = 0;
    UInt32 cached_ = UINT32_MAX;
    bool lzvn_ = false;
    HRESULT ReadAt(UInt64 offset, void *data, size_t size) {
        if (offset > forkSize_ || size > forkSize_ - offset)
            return S_FALSE;
        RINOK(InStream_SeekSet(source_, offset))
        return ReadStream_FALSE(source_, data, size);
    }
    HRESULT ReadMap();
    HRESULT Decode(UInt32 block);

  public:
    HRESULT Init(ISequentialInStream *source, UInt64 forkSize, const CCompressHeader &header,
                 const CByteBuffer *data);
};

HRESULT CDecmpfsStream::Init(ISequentialInStream *source, UInt64 forkSize,
                             const CCompressHeader &header, const CByteBuffer *data) {
    header_ = header;
    if (!header.IsCorrect || !header.IsSupported || header.UnpackSize > INT64_MAX)
        return S_FALSE;
    cache_.Alloc(kCompressionBlockSize);
    if (!header.IsMethod_Resource()) {
        // Inline xattrs hold one small compression unit. Decoding is deferred
        // until Read, including when the mount merely probes stream support.
        if (!data || header.UnpackSize > kCompressionBlockSize || data->Size() < header.DataPos)
            return S_FALSE;
        inline_.CopyFrom(*data, data->Size());
        return S_OK;
    }
    if (!source || source->QueryInterface(IID_IInStream, (void **)&source_) != S_OK || !source_)
        return S_FALSE;
    forkSize_ = forkSize;
    UInt64 available;
    RINOK(InStream_AtBegin_GetSize(source_, available))
    if (forkSize_ > available)
        return S_FALSE;
    lzvn_ = header.Method == 8;
    if (!lzvn_ && header.Method != 4)
        return S_FALSE;
    packed_.Alloc(kCompressionBlockSize + 1);
    return ReadMap();
}

HRESULT CDecmpfsStream::ReadMap() {
    const UInt64 blocks = (header_.UnpackSize + kCompressionBlockSize - 1) / kCompressionBlockSize;
    if (lzvn_) {
        // Original LZVN resource reader: n + 1 little-endian offsets.
        if (blocks >= (UInt64(1) << 29))
            return S_FALSE;
        const size_t tableSize = size_t(blocks + 1) * 4;
        if (tableSize > forkSize_)
            return S_FALSE;
        table_.Alloc(tableSize);
        RINOK(ReadAt(0, table_, tableSize))
        UInt32 previous = GetUi32(table_);
        if (previous != tableSize)
            return S_FALSE;
        for (UInt64 i = 1; i <= blocks; ++i) {
            const UInt32 offset = GetUi32(table_ + size_t(i) * 4);
            if (offset <= previous || offset - previous > kCompressionBlockSize + 1)
                return S_FALSE;
            previous = offset;
        }
        return previous == forkSize_ ? S_OK : S_FALSE;
    }
    Byte header[0x108];
    RINOK(ReadAt(0, header, sizeof(header)))
    const UInt32 dataPos = GetBe32(header), mapPos = GetBe32(header + 4);
    const UInt32 dataSize = GetBe32(header + 8), mapSize = GetBe32(header + 12);
    const UInt32 dataSize2 = GetBe32(header + 0x100);
    const UInt32 count = GetUi32(header + 0x104);
    if (mapSize != 50 || dataPos > mapPos || dataSize != mapPos - dataPos || mapSize > forkSize_ ||
        mapPos != forkSize_ - mapSize || UInt64(dataSize2) + 4 != dataSize || dataSize2 < 8 ||
        dataSize2 > dataSize || ((dataSize2 - 4) >> 3) < count || blocks != count ||
        UInt64(0x104) + dataSize2 != mapPos)
        return S_FALSE;
    const size_t tableSize = size_t(count) * 8;
    table_.Alloc(tableSize);
    RINOK(ReadAt(sizeof(header), table_, tableSize))
    UInt64 previous = 4 + UInt64(tableSize);
    for (UInt32 i = 0; i < count; ++i) {
        const UInt32 offset = GetUi32(table_ + size_t(i) * 8);
        const UInt32 size = GetUi32(table_ + size_t(i) * 8 + 4);
        if (!size || size > kCompressionBlockSize + 1 || previous != offset || offset > dataSize2 ||
            size > dataSize2 - offset)
            return S_FALSE;
        previous = UInt64(offset) + size;
    }
    if (previous != dataSize2)
        return S_FALSE;
    Byte map[50];
    RINOK(ReadAt(mapPos, map, sizeof(map)))
    if (GetBe16(map + 24) != 28 || GetBe16(map + 26) != 50 || GetBe16(map + 28) != 0 ||
        GetBe32(map + 30) != 0x636d7066 || GetBe16(map + 34) != 0 || GetBe16(map + 36) != 10 ||
        GetBe16(map + 38) != 1 || GetBe16(map + 40) != 0xffff ||
        (GetBe32(map + 42) & 0xffffff) != 0)
        return S_FALSE;
    return S_OK;
}

HRESULT CDecmpfsStream::Decode(UInt32 block) {
    cached_ = UINT32_MAX;
    const UInt32 unpacked = UInt32(std::min<UInt64>(
        kCompressionBlockSize, header_.UnpackSize - UInt64(block) * kCompressionBlockSize));
    CMyComPtr2_Create<ISequentialOutStream, CBufPtrSeqOutStream> output;
    output->Init(cache_, unpacked);
    if (!header_.IsMethod_Resource()) {
        int operation;
        RINOK(decoder_.Extract(nullptr, output, 0, header_, &inline_, 0, nullptr, operation))
        if (operation != NExtract::NOperationResult::kOK)
            return S_FALSE;
    } else {
        const UInt64 offset = lzvn_ ? GetUi32(table_ + size_t(block) * 4)
                                    : UInt64(0x104) + GetUi32(table_ + size_t(block) * 8);
        const UInt32 size = lzvn_ ? GetUi32(table_ + size_t(block + 1) * 4) - UInt32(offset)
                                  : GetUi32(table_ + size_t(block) * 8 + 4);
        RINOK(ReadAt(offset, packed_, size))
        RINOK(decoder_.DecodeResourceBlock(packed_, size, unpacked, lzvn_, output))
    }
    if (output->GetPos() != unpacked)
        return S_FALSE;
    cached_ = block;
    return S_OK;
}
Z7_COM7F_IMF(CDecmpfsStream::Read(void *data, UInt32 size, UInt32 *processed)) {
    COM_TRY_BEGIN
    if (processed)
        *processed = 0;
    if (!size || position_ >= header_.UnpackSize)
        return S_OK;
    if (!data)
        return E_INVALIDARG;
    const UInt32 block = UInt32(position_ / kCompressionBlockSize);
    if (cached_ != block) {
        RINOK(Decode(block))
    }
    const size_t within = size_t(position_ % kCompressionBlockSize);
    const UInt32 count = UInt32(std::min<UInt64>(
        std::min<size_t>(size, kCompressionBlockSize - within), header_.UnpackSize - position_));
    memcpy(data, cache_ + within, count);
    position_ += count;
    if (processed)
        *processed = count;
    return S_OK;
    COM_TRY_END
}
Z7_COM7F_IMF(CDecmpfsStream::Seek(Int64 offset, UInt32 origin, UInt64 *position)) {
    const UInt64 base = origin == STREAM_SEEK_SET   ? 0
                        : origin == STREAM_SEEK_CUR ? position_
                                                    : header_.UnpackSize;
    if (origin > STREAM_SEEK_END)
        return STG_E_INVALIDFUNCTION;
    if ((offset < 0 && UInt64(-(offset + 1)) + 1 > base) ||
        (offset >= 0 && UInt64(offset) > UInt64(INT64_MAX) - base))
        return HRESULT_WIN32_ERROR_NEGATIVE_SEEK;
    position_ = base + UInt64(offset);
    if (position)
        *position = position_;
    return S_OK;
}
} // namespace

HRESULT CreateDecmpfsStream(ISequentialInStream *fork, UInt64 forkSize,
                            const CCompressHeader &header, const CByteBuffer *data,
                            ISequentialInStream **stream) {
    COM_TRY_BEGIN
    *stream = nullptr;
    CMyComPtr2_Create<ISequentialInStream, CDecmpfsStream> result;
    RINOK(result->Init(fork, forkSize, header, data))
    CMyComPtr<ISequentialInStream> retained = result.Interface();
    *stream = retained.Detach();
    return S_OK;
    COM_TRY_END
}
} // namespace NArchive::NHfs
