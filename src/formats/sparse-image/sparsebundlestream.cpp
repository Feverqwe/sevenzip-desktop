// SPDX-License-Identifier: GPL-2.0-or-later
// Local native stream adapter. Container mapping originates in sparsebundlefs-lib;
// key setup and per-band CBC streams share the authoritative encrypted-DMG owner.
#include "formats/sparse-image/sparsebundlestream.h"
#include "7zip/Common/StreamUtils.h"
#include "Common/ComTry.h"
#include "Common/MyXml.h"
#include "Common/StringToInt.h"
#include "Windows/PropVariant.h"
#include "formats/sparse-image/bandmap.h"
#include <algorithm>
#include <charconv>
#include <cstring>
#include <vector>

namespace NArchive::NSparseImage {
HRESULT SparseBundleStream::Open(IInStream *input, IArchiveOpenCallback *callback) {
    COM_TRY_BEGIN
    volumes_.Release();
    token_.SetFromCls(nullptr);
    bands_.clear();
    prefix_.Empty();
    size_ = bandSize_ = position_ = 0;
    encrypted_ = false;
    damageSeen_ = false;
    if (!input || !callback)
        return S_FALSE;
    callback->QueryInterface(IID_IArchiveOpenVolumeCallback, (void **)&volumes_);
    if (!volumes_)
        return S_FALSE;
    NWindows::NCOM::CPropVariant name;
    RINOK(volumes_->GetProperty(kpidName, &name))
    if (name.vt != VT_BSTR)
        return S_FALSE;
    UString path(name.bstrVal);
    if (path.IsEmpty())
        return S_FALSE;
    if (path.Len() >= 13 && UString(path.RightPtr(13)).IsEqualTo_NoCase(L".sparsebundle")) {
        prefix_ = path;
        prefix_ += L'/';
    } else if (!path.IsEqualTo_NoCase(L"Info.plist"))
        return S_FALSE;
    UInt64 length;
    RINOK(InStream_AtBegin_GetSize(input, length))
    if (!length || length > (1u << 20))
        return S_FALSE;
    std::vector<char> bytes(size_t(length) + 1, 0);
    RINOK(ReadStream_FALSE(input, bytes.data(), size_t(length)))
    if (memchr(bytes.data(), 0, size_t(length)))
        return E_INVALIDARG;
    CXml xml;
    if (!xml.Parse(bytes.data()) || !xml.Root.IsTagged("plist"))
        return S_FALSE;
    const CXmlItem *dict = xml.Root.FindSubTag_GetPtr("dict");
    if (!dict || dict->SubItems.Size() % 2)
        return E_INVALIDARG;
    bool identified = false, haveBand = false, haveSize = false;
    for (unsigned i = 0; i < dict->SubItems.Size(); i += 2) {
        const auto &keyNode = dict->SubItems[i];
        const auto &valueNode = dict->SubItems[i + 1];
        if (!keyNode.IsTagged("key"))
            return E_INVALIDARG;
        const auto key = keyNode.GetSubString();
        auto text = valueNode.GetSubString();
        text.Trim();
        if (key == "diskimage-bundle-type") {
            if (identified || !valueNode.IsTagged("string") ||
                text != "com.apple.diskimage.sparsebundle")
                return S_FALSE;
            identified = true;
        } else if (key == "band-size" || key == "size") {
            const char *end;
            const UInt64 value = ConvertStringToUInt64(text, &end);
            if (!valueNode.IsTagged("integer") || end == text.Ptr() || *end)
                return E_INVALIDARG;
            if (key == "band-size") {
                if (haveBand)
                    return E_INVALIDARG;
                bandSize_ = value;
                haveBand = true;
            } else {
                if (haveSize)
                    return E_INVALIDARG;
                size_ = value;
                haveSize = true;
            }
        }
    }
    if (!identified)
        return S_FALSE;
    if (!size_ || size_ > INT64_MAX || !bandSize_ || bandSize_ > INT64_MAX || bandSize_ % 512)
        return E_INVALIDARG;
    CMyComPtr<IInStream> tokenInput;
    const UString tokenName = prefix_ + L"token";
    RINOK(volumes_->GetStream(tokenName, &tokenInput))
    if (!tokenInput)
        return E_FAIL;
    RINOK(InStream_AtBegin_GetSize(tokenInput, length))
    if (length) {
        if (length > (16u << 20))
            return E_INVALIDARG;
        token_.SetFromCls(new NEncryptedDmg::DecryptStream);
        RINOK(token_->Open(tokenInput, callback, true))
        if (bandSize_ % token_->BlockSize() || bandSize_ / token_->BlockSize() > (UInt64(1) << 32))
            return E_INVALIDARG;
        encrypted_ = true;
    }
    return S_OK;
    COM_TRY_END
}
HRESULT SparseBundleStream::ReadBand(UInt64 number, UInt64 offset, void *data, UInt32 size) {
    auto found = std::find_if(bands_.begin(), bands_.end(),
                              [number](const Band &band) { return band.Number == number; });
    if (found == bands_.end()) {
        Band band;
        band.Number = number;
        char hex[17]{};
        std::to_chars(hex, hex + 16, number, 16);
        UString path = prefix_ + L"bands/";
        path += hex;
        CMyComPtr<IInStream> input;
        const HRESULT result = volumes_->GetStream(path, &input);
        if (result != S_OK && result != S_FALSE)
            return result;
        if ((result == S_OK) != bool(input))
            return E_FAIL;
        if (result == S_OK && input) {
            RINOK(InStream_AtBegin_GetSize(input, band.Length))
            if (band.Length > bandSize_)
                return E_INVALIDARG;
            if (encrypted_) {
                RINOK(token_->OpenBand(input, &band.Stream))
                band.DamagedTail = band.Length % token_->BlockSize() != 0;
                damageSeen_ = damageSeen_ || band.DamagedTail;
                band.Length &= ~UInt64(15);
            } else
                band.Stream = input;
        }
        bands_.push_front(std::move(band));
        // Retain a small number of independent ciphertext caches/handles when
        // filesystem metadata and file reads alternate between remote bands.
        if (bands_.size() > 4)
            bands_.pop_back();
    } else {
        bands_.splice(bands_.begin(), bands_, found);
    }
    const auto &band = bands_.front();
    if (band.DamagedTail && (offset > band.Length || size > band.Length - offset))
        return static_cast<HRESULT>(0x80070026L);
    UInt32 count = 0;
    if (band.Stream && offset < band.Length) {
        count = UInt32(std::min<UInt64>(size, band.Length - offset));
        HRESULT result = InStream_SeekSet(band.Stream, offset);
        if (result == S_OK)
            result = ReadStream_FALSE(band.Stream, data, count);
        if (result != S_OK) {
            bands_.pop_front(); // Permit reopening after a transient transport error.
            return result;
        }
    }
    // Unallocated bands and the unwritten tail of a band are sparse zeroes.
    memset(static_cast<Byte *>(data) + count, 0, size - count);
    return S_OK;
}
int32_t SparseBundleStream::ReadBandCallback(void *context, uint64_t band, uint64_t offset,
                                             void *data, uint32_t size) {
    return static_cast<SparseBundleStream *>(context)->ReadBand(band, offset, data, size);
}
Z7_COM7F_IMF(SparseBundleStream::Read(void *data, UInt32 size, UInt32 *processed)) {
    COM_TRY_BEGIN
    if (processed)
        *processed = 0;
    if (!bandSize_ || (size && !data))
        return E_INVALIDARG;
    UInt32 actual = 0;
    const HRESULT result = sz_sparsebundle_read(this, ReadBandCallback, size_, bandSize_, position_,
                                                data, std::min<UInt32>(size, 1u << 20), &actual);
    position_ += actual;
    if (processed)
        *processed = actual;
    if (result != S_OK && result != E_ABORT && result != E_OUTOFMEMORY)
        damageSeen_ = true;
    return result;
    COM_TRY_END
}
Z7_COM7F_IMF(SparseBundleStream::Seek(Int64 offset, UInt32 origin, UInt64 *position)) {
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
} // namespace NArchive::NSparseImage
