// Extracted from the corresponding 7-Zip handler; retained LGPL notices apply.
#include "7zip/Archive/StdAfx.h"

#include "7zip/Archive/IArchive.h"
#include "7zip/Common/StreamObjects.h"
#include "7zip/Common/StreamUtils.h"
#include "Windows/PropVariantUtils.h"
#include "platform/cpu/CpuArch.h"

#include "formats/hfs/HfsHandler.h"
#define Get16(p) GetBe16(p)
#define Get32(p) GetBe32(p)

namespace NArchive {
namespace NHfs {
// static const UInt32 kMethod_1_NO_COMPRESSION = 1; // in xattr
static const UInt32 kMethod_ZLIB_ATTR = 3;
static const UInt32 kMethod_ZLIB_RSRC = 4;
// static const UInt32 kMethod_DEDUP = 5; // de-dup within the generation store
// macos 10.10
static const UInt32 kMethod_LZVN_ATTR = 7;
static const UInt32 kMethod_LZVN_RSRC = 8;
static const UInt32 kMethod_COPY_ATTR = 9;
static const UInt32 kMethod_COPY_RSRC = 10;
// macos 10.11
// static const UInt32 kMethod_LZFSE_ATTR = 11;
static const UInt32 kMethod_LZFSE_RSRC = 12;

// static const UInt32 kMethod_ZBM_RSRC = 14;

static const char *const g_Methods[] = {
    NULL,        NULL,        NULL,        "ZLIB-attr",  "ZLIB-rsrc",  NULL, NULL,      "LZVN-attr",
    "LZVN-rsrc", "COPY-attr", "COPY-rsrc", "LZFSE-attr", "LZFSE-rsrc", NULL, "ZBM-rsrc"};

static const Byte k_COPY_Uncompressed_Marker = 0xcc;
static const Byte k_LZVN_Uncompressed_Marker = 6;

void CCompressHeader::Parse(const Byte *p, size_t dataSize) {
    Clear();

    if (dataSize < k_decmpfs_HeaderSize)
        return;
    if (GetUi32(p) != 0x636D7066) // magic == "fpmc"
        return;
    Method = GetUi32(p + 4);
    UnpackSize = GetUi64(p + 8);
    dataSize -= k_decmpfs_HeaderSize;
    IsCorrect = true;

    if (Method == kMethod_ZLIB_RSRC || Method == kMethod_COPY_RSRC || Method == kMethod_LZVN_RSRC ||
        Method == kMethod_LZFSE_RSRC
        // || Method == kMethod_ZBM_RSRC // for debug
    ) {
        IsResource = true;
        if (dataSize == 0)
            IsSupported = (Method != kMethod_LZFSE_RSRC && Method != kMethod_COPY_RSRC);
        return;
    }

    if (Method == kMethod_ZLIB_ATTR || Method == kMethod_COPY_ATTR || Method == kMethod_LZVN_ATTR
        // || Method == kMethod_LZFSE_ATTR
    ) {
        if (dataSize == 0)
            return;
        const Byte b = p[k_decmpfs_HeaderSize];
        if ((Method == kMethod_ZLIB_ATTR && (b & 0xf) == 0xf) ||
            (Method == kMethod_COPY_ATTR && b == k_COPY_Uncompressed_Marker) ||
            (Method == kMethod_LZVN_ATTR && b == k_LZVN_Uncompressed_Marker)) {
            dataSize--;
            // if (UnpackSize > dataSize)
            if (UnpackSize != dataSize)
                return;
            DataPos = k_decmpfs_HeaderSize + 1;
            IsSupported = true;
        } else {
            if (Method != kMethod_COPY_ATTR)
                IsSupported = true;
            DataPos = k_decmpfs_HeaderSize;
        }
    }
}

void CCompressHeader::MethodToProp(NWindows::NCOM::CPropVariant &prop) const {
    if (!IsCorrect)
        return;
    const UInt32 method = Method;
    const char *p = NULL;
    if (method < Z7_ARRAY_SIZE(g_Methods))
        p = g_Methods[method];
    AString s;
    if (p)
        s = p;
    else
        s.Add_UInt32(method);
    // if (!IsSupported) s += "-unsuported";
    prop = s;
}

void MethodsMaskToProp(UInt32 methodsMask, NWindows::NCOM::CPropVariant &prop) {
    FLAGS_TO_PROP(g_Methods, methodsMask, prop);
}

CDecoder::CDecoder(bool IsAdlerOptional) {
    /* Some new hfs files contain zlib resource fork without Adler checksum.
       We do not know how we must detect case where there is Adler
       checksum or there is no Adler checksum.
    */
    _zlibDecoder->IsAdlerOptional = IsAdlerOptional;
    _lzfseDecoder->LzvnMode = true;
}

// The same block decoder serves sequential extraction and seekable reads.
HRESULT CDecoder::DecodeResourceBlock(const Byte *packed, UInt32 packedSize, UInt32 unpackSize,
                                      bool lzvn, ISequentialOutStream *output) {
    if (!packedSize || packedSize > kCompressionBlockSize + 1)
        return S_FALSE;
    if (lzvn ? packed[0] == k_LZVN_Uncompressed_Marker : (packed[0] & 0xf) == 0xf) {
        if (packedSize - 1 != unpackSize)
            return S_FALSE;
        return output ? WriteStream(output, packed + 1, unpackSize) : S_OK;
    }
    CMyComPtr2_Create<ISequentialInStream, CBufInStream> input;
    input->Init(packed, packedSize);
    const UInt64 expected = unpackSize;
    if (lzvn) {
        const UInt64 size = packedSize;
        return _lzfseDecoder.Interface()->Code(input, output, &size, &expected, nullptr);
    }
    RINOK(_zlibDecoder.Interface()->Code(input, output, nullptr, &expected, nullptr))
    if (_zlibDecoder->GetOutputProcessedSize() != unpackSize ||
        _zlibDecoder->GetInputProcessedSize() > packedSize)
        return S_FALSE;
    // As in the original resource reader, APFS permits trailing block padding.
    return S_OK;
}

HRESULT CDecoder::ExtractResourceFork_ZLIB(ISequentialInStream *inStream,
                                           ISequentialOutStream *outStream, UInt64 forkSize,
                                           UInt64 unpackSize, UInt64 progressStart,
                                           IArchiveExtractCallback *extractCallback) {
    const unsigned kHeaderSize = 0x100 + 8;

    const size_t kBufSize = kCompressionBlockSize;
    _buf.Alloc(kBufSize + 0x10); // we need 1 additional bytes for uncompressed chunk header

    RINOK(ReadStream_FALSE(inStream, _buf, kHeaderSize))
    Byte *buf = _buf;
    const UInt32 dataPos = Get32(buf);
    const UInt32 mapPos = Get32(buf + 4);
    const UInt32 dataSize = Get32(buf + 8);
    const UInt32 mapSize = Get32(buf + 12);

    const UInt32 kResMapSize = 50;

    if (mapSize != kResMapSize || dataPos > mapPos || dataSize != mapPos - dataPos ||
        mapSize > forkSize || mapPos != forkSize - mapSize)
        return S_FALSE;

    const UInt32 dataSize2 = Get32(buf + 0x100);
    if (4 + dataSize2 != dataSize || dataSize2 < 8 || dataSize2 > dataSize)
        return S_FALSE;

    const UInt32 numBlocks = GetUi32(buf + 0x100 + 4);
    if (((dataSize2 - 4) >> 3) < numBlocks)
        return S_FALSE;
    {
        const UInt64 up = unpackSize + kCompressionBlockSize - 1;
        if (up < unpackSize || up / kCompressionBlockSize != numBlocks)
            return S_FALSE;
    }

    const UInt32 tableSize = (numBlocks << 3);

    _tableBuf.AllocAtLeast(tableSize);

    RINOK(ReadStream_FALSE(inStream, _tableBuf, tableSize))
    const Byte *tableBuf = _tableBuf;

    UInt32 prev = 4 + tableSize;

    UInt32 i;
    for (i = 0; i < numBlocks; i++) {
        const UInt32 offs = GetUi32(tableBuf + i * 8);
        const UInt32 size = GetUi32(tableBuf + i * 8 + 4);
        if (size == 0 || prev != offs || offs > dataSize2 || size > dataSize2 - offs)
            return S_FALSE;
        prev = offs + size;
    }

    if (prev != dataSize2)
        return S_FALSE;

    // bool padError = false;
    UInt64 outPos = 0;

    for (i = 0; i < numBlocks; i++) {
        const UInt64 rem = unpackSize - outPos;
        if (rem == 0)
            return S_FALSE;
        UInt32 blockSize = kCompressionBlockSize;
        if (rem < kCompressionBlockSize)
            blockSize = (UInt32)rem;

        const UInt32 size = GetUi32(tableBuf + i * 8 + 4);

        if (size > kCompressionBlockSize + 1)
            return S_FALSE;

        RINOK(ReadStream_FALSE(inStream, buf, size))

        RINOK(DecodeResourceBlock(buf, size, blockSize, false, outStream))

        outPos += blockSize;
        if ((i & 0xFF) == 0) {
            const UInt64 progressPos = progressStart + outPos;
            RINOK(extractCallback->SetCompleted(&progressPos))
        }
    }

    if (outPos != unpackSize)
        return S_FALSE;

    // if (padError) return S_FALSE;

    /* We check Resource Map
       Are there HFS files with another values in Resource Map ??? */

    RINOK(ReadStream_FALSE(inStream, buf, mapSize))
    const UInt32 types = Get16(buf + 24);
    const UInt32 names = Get16(buf + 26);
    const UInt32 numTypes = Get16(buf + 28);
    if (numTypes != 0 || types != 28 || names != kResMapSize)
        return S_FALSE;
    const UInt32 resType = Get32(buf + 30);
    const UInt32 numResources = Get16(buf + 34);
    const UInt32 resListOffset = Get16(buf + 36);
    if (resType != 0x636D7066) // cmpf
        return S_FALSE;
    if (numResources != 0 || resListOffset != 10)
        return S_FALSE;

    const UInt32 entryId = Get16(buf + 38);
    const UInt32 nameOffset = Get16(buf + 40);
    // Byte attrib = buf[42];
    const UInt32 resourceOffset = Get32(buf + 42) & 0xFFFFFF;
    if (entryId != 1 || nameOffset != 0xFFFF || resourceOffset != 0)
        return S_FALSE;

    return S_OK;
}

HRESULT CDecoder::ExtractResourceFork_LZFSE(ISequentialInStream *inStream,
                                            ISequentialOutStream *outStream, UInt64 forkSize,
                                            UInt64 unpackSize, UInt64 progressStart,
                                            IArchiveExtractCallback *extractCallback) {
    const UInt32 kNumBlocksMax = (UInt32)1 << 29;
    if (unpackSize >= (UInt64)kNumBlocksMax * kCompressionBlockSize)
        return S_FALSE;
    const UInt32 numBlocks =
        (UInt32)((unpackSize + kCompressionBlockSize - 1) / kCompressionBlockSize);
    const UInt32 numBlocks2 = numBlocks + 1;
    const UInt32 tableSize = (numBlocks2 << 2);
    if (tableSize > forkSize)
        return S_FALSE;
    _tableBuf.AllocAtLeast(tableSize);
    RINOK(ReadStream_FALSE(inStream, _tableBuf, tableSize))
    const Byte *tableBuf = _tableBuf;

    {
        UInt32 prev = GetUi32(tableBuf);
        if (prev != tableSize)
            return S_FALSE;
        for (UInt32 i = 1; i < numBlocks2; i++) {
            const UInt32 offs = GetUi32(tableBuf + i * 4);
            if (offs <= prev)
                return S_FALSE;
            prev = offs;
        }
        if (prev != forkSize)
            return S_FALSE;
    }

    const size_t kBufSize = kCompressionBlockSize;
    _buf.Alloc(kBufSize + 0x10); // we need 1 additional bytes for uncompressed chunk header

    UInt64 outPos = 0;

    for (UInt32 i = 0; i < numBlocks; i++) {
        const UInt64 rem = unpackSize - outPos;
        if (rem == 0)
            return S_FALSE;
        UInt32 blockSize = kCompressionBlockSize;
        if (rem < kCompressionBlockSize)
            blockSize = (UInt32)rem;

        const UInt32 size = GetUi32(tableBuf + i * 4 + 4) - GetUi32(tableBuf + i * 4);

        if (size > kCompressionBlockSize + 1)
            return S_FALSE;

        RINOK(ReadStream_FALSE(inStream, _buf, size))
        const Byte *buf = _buf;

        RINOK(DecodeResourceBlock(buf, size, blockSize, true, outStream))

        outPos += blockSize;
        if ((i & 0xFF) == 0) {
            const UInt64 progressPos = progressStart + outPos;
            RINOK(extractCallback->SetCompleted(&progressPos))
        }
    }

    return S_OK;
}

/*
static UInt32 GetUi24(const Byte *p)
{
  return p[0] + ((UInt32)p[1] << 8) + ((UInt32)p[2] << 24);
}

HRESULT CDecoder::ExtractResourceFork_ZBM(
    ISequentialInStream *inStream, ISequentialOutStream *outStream,
    UInt64 forkSize, UInt64 unpackSize,
    UInt64 progressStart, IArchiveExtractCallback *extractCallback)
{
  const UInt32 kNumBlocksMax = (UInt32)1 << 29;
  if (unpackSize >= (UInt64)kNumBlocksMax * kCompressionBlockSize)
    return S_FALSE;
  const UInt32 numBlocks = (UInt32)((unpackSize + kCompressionBlockSize - 1) /
kCompressionBlockSize); const UInt32 numBlocks2 = numBlocks + 1; const UInt32 tableSize =
(numBlocks2 << 2); if (tableSize > forkSize) return S_FALSE; _tableBuf.AllocAtLeast(tableSize);
  RINOK(ReadStream_FALSE(inStream, _tableBuf, tableSize));
  const Byte *tableBuf = _tableBuf;

  {
    UInt32 prev = GetUi32(tableBuf);
    if (prev != tableSize)
      return S_FALSE;
    for (UInt32 i = 1; i < numBlocks2; i++)
    {
      const UInt32 offs = GetUi32(tableBuf + i * 4);
      if (offs <= prev)
        return S_FALSE;
      prev = offs;
    }
    if (prev != forkSize)
      return S_FALSE;
  }

  const size_t kBufSize = kCompressionBlockSize;
  _buf.Alloc(kBufSize + 0x10); // we need 1 additional bytes for uncompressed chunk header

  CBufInStream *bufInStream = new CBufInStream;
  CMyComPtr<ISequentialInStream> bufInStream = bufInStream;

  UInt64 outPos = 0;

  for (UInt32 i = 0; i < numBlocks; i++)
  {
    const UInt64 rem = unpackSize - outPos;
    if (rem == 0)
      return S_FALSE;
    UInt32 blockSize = kCompressionBlockSize;
    if (rem < kCompressionBlockSize)
      blockSize = (UInt32)rem;

    const UInt32 size =
        GetUi32(tableBuf + i * 4 + 4) -
        GetUi32(tableBuf + i * 4);

    // if (size > kCompressionBlockSize + 1)
    if (size > blockSize + 1)
      return S_FALSE; // we don't expect it, because encode will use uncompressed chunk

    RINOK(ReadStream_FALSE(inStream, _buf, size));
    const Byte *buf = _buf;

    // (size != 0)
    // if (size == 0) return S_FALSE;

    if (buf[0] == 0xFF) // uncompressed marker
    {
      if (size != blockSize + 1)
        return S_FALSE;
      if (outStream)
      {
        RINOK(WriteStream(outStream, buf + 1, blockSize));
      }
    }
    else
    {
      if (size < 4)
        return S_FALSE;
      if (buf[0] != 'Z' ||
          buf[1] != 'B' ||
          buf[2] != 'M' ||
          buf[3] != 9)
        return S_FALSE;
      // for debug:
      unsigned packPos = 4;
      unsigned unpackPos = 0;
      unsigned packRem = size - packPos;
      for (;;)
      {
        if (packRem < 6)
          return S_FALSE;
        const UInt32 packSize = GetUi24(buf + packPos);
        const UInt32 chunkUnpackSize = GetUi24(buf + packPos + 3);
        if (packSize < 6)
          return S_FALSE;
        if (packSize > packRem)
          return S_FALSE;
        if (chunkUnpackSize > blockSize - unpackPos)
          return S_FALSE;
        packPos += packSize;
        packRem -= packSize;
        unpackPos += chunkUnpackSize;
        if (packSize == 6)
        {
          if (chunkUnpackSize != 0)
            return S_FALSE;
          break;
        }
        if (packSize >= chunkUnpackSize + 6)
        {
          if (packSize > chunkUnpackSize + 6)
            return S_FALSE;
          // uncompressed chunk;
        }
        else
        {
          // compressed chunk
          const Byte *t = buf + packPos - packSize + 6;
          UInt32 r = packSize - 6;
          if (r < 9)
            return S_FALSE;
          const UInt32 v0 = GetUi24(t);
          const UInt32 v1 = GetUi24(t + 3);
          const UInt32 v2 = GetUi24(t + 6);
          if (v0 > v1 || v1 > v2 || v2 > packSize)
            return S_FALSE;
          // here we need the code that will decompress ZBM chunk
        }
      }

      if (unpackPos != blockSize)
        return S_FALSE;

      UInt32 size1 = size;
      if (size1 > kCompressionBlockSize)
      {
        size1 = kCompressionBlockSize;
        // return S_FALSE;
      }
      if (outStream)
      {
        RINOK(WriteStream(outStream, buf, size1))

        const UInt32 kTempSize = 1 << 16;
        Byte temp[kTempSize];
        memset(temp, 0, kTempSize);

        for (UInt32 k = size1; k < kCompressionBlockSize; k++)
        {
          UInt32 cur = kCompressionBlockSize - k;
          if (cur > kTempSize)
            cur = kTempSize;
          RINOK(WriteStream(outStream, temp, cur))
          k += cur;
        }
      }

      // const UInt64 blockSize64 = blockSize;
      // const UInt64 packSize64 = size;
      // bufInStream->Init(buf, size);
      // RINOK(_zbmDecoderSpec->Code(bufInStream, outStream, &packSize64, &blockSize64, NULL));
      // in/out sizes were checked in Code()
    }

    outPos += blockSize;
    if ((i & 0xFF) == 0)
    {
      const UInt64 progressPos = progressStart + outPos;
      RINOK(extractCallback->SetCompleted(&progressPos));
    }
  }

  return S_OK;
}
*/

HRESULT CDecoder::Extract(ISequentialInStream *inStreamFork, ISequentialOutStream *realOutStream,
                          UInt64 forkSize, const CCompressHeader &compressHeader,
                          const CByteBuffer *data, UInt64 progressStart,
                          IArchiveExtractCallback *extractCallback, int &opRes) {
    opRes = NExtract::NOperationResult::kDataError;

    if (compressHeader.IsMethod_Uncompressed_Inline()) {
        const size_t packSize = data->Size() - compressHeader.DataPos;
        if (realOutStream) {
            RINOK(WriteStream(realOutStream, *data + compressHeader.DataPos, packSize))
        }
        opRes = NExtract::NOperationResult::kOK;
        return S_OK;
    }

    if (compressHeader.Method == kMethod_ZLIB_ATTR || compressHeader.Method == kMethod_LZVN_ATTR) {
        CMyComPtr2_Create<ISequentialInStream, CBufInStream> bufInStream;
        const size_t packSize = data->Size() - compressHeader.DataPos;
        bufInStream->Init(*data + compressHeader.DataPos, packSize);

        if (compressHeader.Method == kMethod_ZLIB_ATTR) {
            const HRESULT hres = _zlibDecoder.Interface()->Code(bufInStream, realOutStream, NULL,
                                                                &compressHeader.UnpackSize, NULL);
            if (hres == S_OK)
                if (_zlibDecoder->GetOutputProcessedSize() == compressHeader.UnpackSize &&
                    _zlibDecoder->GetInputProcessedSize() == packSize)
                    opRes = NExtract::NOperationResult::kOK;
            return hres;
        }
        {
            const UInt64 packSize64 = packSize;
            const HRESULT hres = _lzfseDecoder.Interface()->Code(
                bufInStream, realOutStream, &packSize64, &compressHeader.UnpackSize, NULL);
            if (hres == S_OK) {
                // in/out sizes were checked in Code()
                opRes = NExtract::NOperationResult::kOK;
            }
            return hres;
        }
    }

    HRESULT hres;
    if (compressHeader.Method == NHfs::kMethod_ZLIB_RSRC) {
        hres = ExtractResourceFork_ZLIB(inStreamFork, realOutStream, forkSize,
                                        compressHeader.UnpackSize, progressStart, extractCallback);
        // for debug:
        // hres = NCompress::CopyStream(inStreamFork, realOutStream, NULL);
    } else if (compressHeader.Method == NHfs::kMethod_LZVN_RSRC) {
        hres = ExtractResourceFork_LZFSE(inStreamFork, realOutStream, forkSize,
                                         compressHeader.UnpackSize, progressStart, extractCallback);
    }
    /*
    else if (compressHeader.Method == NHfs::kMethod_ZBM_RSRC)
    {
      hres = ExtractResourceFork_ZBM(
          inStreamFork, realOutStream,
          forkSize, compressHeader.UnpackSize,
          progressStart, extractCallback);
    }
    */
    else {
        opRes = NExtract::NOperationResult::kUnsupportedMethod;
        hres = S_FALSE;
    }

    if (hres == S_OK)
        opRes = NExtract::NOperationResult::kOK;
    return hres;
}

} // namespace NHfs
} // namespace NArchive
