// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2017 Simon Gander (apfs-fuse).
// Adapted 2026-11-03: native streams, bounded parsing/reads, SDK crypto,
// callback passwords/cancellation, strict padding, modern wrapping, no diagnostics.
// Baseline and omitted scope: docs/upstream-port-audit/dmg-encryption.md.
#include "formats/encrypted-dmg/dmgdecryptstream.h"
#include "7zip/Common/StreamObjects.h"
#include "7zip/Common/StreamUtils.h"
#include "7zip/Crypto/HmacSha1.h"
#include "7zip/IPassword.h"
#include "Common/ComTry.h"
#include "Common/UTFConvert.h"
#include "archive/callbacks/archivepasswordretry.h"
#include "dependencies/apfscrypto/TripleDes.h"
#include "formats/encrypted-dmg/dmgcrypto.h"
#include "formats/encrypted-dmg/dmgheaders.h"
#include <algorithm>
#include <cstring>

namespace NArchive::NEncryptedDmg {
namespace {
// Keep ciphertext transport separate from the format's independent CBC chunks.
// The SDK cache supplies seek, eviction and failed-fill invalidation unchanged.
class CachedInput final : public CCachedInStream {
    CMyComPtr<IInStream> input_;
    static constexpr unsigned BlockBits = 19;
    HRESULT ReadBlock(UInt64 index, Byte *data, size_t size) override {
        RINOK(InStream_SeekSet(input_, index << BlockBits))
        return ReadStream_FALSE(input_, data, size);
    }

  public:
    explicit CachedInput(IInStream *input) : input_(input) {}
    HRESULT Open(UInt64 size) {
        if (!Alloc(BlockBits, 3))
            return E_OUTOFMEMORY;
        Init(size);
        return S_OK;
    }
};
constexpr HRESULT UnexpectedEnd = static_cast<HRESULT>(0x80070026L);
constexpr UInt32 MaxIterations = 10000000;
bool KdfValid(UInt32 algorithm, UInt32 prng, UInt32 iterations, UInt32 saltSize) {
    return algorithm == 103 && prng == 0 && iterations && iterations <= MaxIterations && saltSize &&
           saltSize <= 32;
}
HRESULT Password(IArchiveOpenCallback *callback, AString &password) {
    CMyComPtr<ICryptoGetTextPassword> crypto;
    if (callback)
        callback->QueryInterface(IID_ICryptoGetTextPassword, (void **)&crypto);
    if (!crypto)
        return E_NOINTERFACE;
    CMyComBSTR value;
    RINOK(crypto->CryptoGetTextPassword(&value))
    ConvertUnicodeToUTF8(UString(value), password);
    return S_OK;
}
struct PasswordWiper {
    AString &value;
    ~PasswordWiper() { Wipe(value.GetBuf(), value.Len()); }
};
bool UnwrapV1(TripleDES &des, const Byte *wrapped, size_t size, Byte *key, size_t keySize) {
    static const Byte iv[8] = {0x4A, 0xDD, 0xA2, 0x2C, 0x79, 0xE8, 0x21, 0x05};
    Secret first, reversed, plain;
    des.SetIV(iv);
    des.DecryptCBC(first.bytes, wrapped, size);
    if (!Unpad(first.bytes, size, 8) || size % 8 || size < 8)
        return false;
    for (size_t n = 0; n < size; ++n)
        reversed.bytes[size - n - 1] = first.bytes[n];
    des.SetIV(nullptr);
    des.DecryptCBC(plain.bytes, reversed.bytes, size);
    if (!Unpad(plain.bytes, size, 8) || size < 12 + keySize)
        return false;
    memcpy(key, plain.bytes + 12, keySize);
    return true;
}
} // namespace

DecryptStream::~DecryptStream() {
    Wipe(key_.data(), key_.size());
    Wipe(hmac_.data(), hmac_.size());
}
HRESULT DecryptStream::ReadAt(UInt64 offset, void *data, size_t size) {
    if (offset > physicalSize_ || size > physicalSize_ - offset)
        return UnexpectedEnd;
    RINOK(InStream_SeekSet(input_, offset))
    return ReadStream_FALSE(input_, data, size);
}
HRESULT DecryptStream::ValidateRange(UInt64 available) const {
    // The IV is keyed by a 32-bit block number. Never wrap it or allocate an
    // untrusted multi-gigabyte block. Physical padding is AES, not chunk sized.
    if (blockSize_ < 16 || blockSize_ > (1u << 20) || (blockSize_ & (blockSize_ - 1)) || !size_ ||
        size_ > UInt64(blockSize_) * (UInt64(1) << 32))
        return E_NOTIMPL;
    const UInt64 padded = (size_ + 15) & ~UInt64(15);
    if (offset_ > available || padded > available - offset_ || available > INT64_MAX)
        return UnexpectedEnd;
    return S_OK;
}
HRESULT DecryptStream::Open(IInStream *input, IArchiveOpenCallback *callback) {
    COM_TRY_BEGIN
    Wipe(key_.data(), key_.size());
    Wipe(hmac_.data(), hmac_.size());
    keySize_ = 0;
    size_ = offset_ = 0;
    input_ = input;
    position_ = 0;
    if (!input)
        return E_INVALIDARG;
    RINOK(InStream_AtBegin_GetSize(input, physicalSize_))
    if (physicalSize_ < 8 || physicalSize_ > INT64_MAX)
        return S_FALSE;
    Byte signature[8];
    RINOK(ReadAt(0, signature, sizeof(signature)))
    HRESULT result;
    if (!memcmp(signature, "encrcdsa", 8)) {
        result = SetupV2(callback);
    } else {
        RINOK(ReadAt(physicalSize_ - 8, signature, sizeof(signature)))
        if (memcmp(signature, "cdsaencr", 8))
            return S_FALSE;
        result = SetupV1(callback);
    }
    if (result != S_OK)
        return result;
    CMyComPtr2<IInStream, CachedInput> cached;
    cached.SetFromCls(new CachedInput(input_));
    RINOK(cached->Open(physicalSize_))
    input_ = cached;
    return S_OK;
    COM_TRY_END
}
HRESULT DecryptStream::SetupV2(IArchiveOpenCallback *callback) {
    DmgCryptHeaderV2 hdr;
    RINOK(ReadAt(0, &hdr, sizeof(hdr)))
    if (hdr.maybe_version != 2 || hdr.unk_0C != 16 || hdr.unk_10 != 5 ||
        hdr.unk_14 != 0x80000001u || hdr.unk_1C != 91 || hdr.unk_20 != 160 ||
        (hdr.key_bits != 128 && hdr.key_bits != 256))
        return E_NOTIMPL;
    offset_ = hdr.encrypted_data_offset;
    size_ = hdr.encrypted_data_length;
    blockSize_ = hdr.block_size;
    RINOK(ValidateRange(physicalSize_))
    const UInt32 count = hdr.no_of_keys;
    if (!count || count > 64 || offset_ < sizeof(hdr) + UInt64(count) * sizeof(DmgKeyPointer))
        return E_NOTIMPL;
    std::vector<DmgKeyPointer> pointers(count);
    RINOK(ReadAt(sizeof(hdr), pointers.data(), pointers.size() * sizeof(DmgKeyPointer)))
    // Parse all candidate records before asking for a password. Unsupported
    // certificate/keybag records cannot hide a usable password slot.
    std::vector<DmgKeyData> records;
    for (const auto &pointer : pointers) {
        const UInt64 begin = pointer.key_offset, length = pointer.key_length;
        if (begin < sizeof(hdr) + UInt64(count) * sizeof(DmgKeyPointer) || begin > offset_ ||
            length > offset_ - begin)
            return E_INVALIDARG;
        if (pointer.key_type != 1)
            continue;
        if (length < 104)
            return E_INVALIDARG;
        DmgKeyData record{};
        RINOK(ReadAt(begin, &record, size_t(std::min<UInt64>(length, sizeof(record)))))
        const UInt32 wrap = record.blob_enc_algorithm;
        const UInt32 bits = record.blob_enc_key_bits;
        const size_t unit = wrap == 17 ? 8 : 16;
        const UInt32 blobSize = record.encr_key_blob_size;
        if (!KdfValid(record.kdf_algorithm, record.prng_algorithm, record.iteration_count,
                      record.salt_len) ||
            !((wrap == 17 && bits == 192) ||
              (wrap == 0x80000001u && (bits == 128 || bits == 192 || bits == 256))) ||
            record.blob_unk_5C != 7 || record.blob_enc_mode != 6 || record.blob_enc_iv_size > unit)
            continue;
        if (!blobSize || blobSize > sizeof(record.encr_key_blob) || blobSize % unit ||
            blobSize > length - 104)
            return E_INVALIDARG;
        records.push_back(record);
    }
    if (records.empty())
        return E_NOTIMPL;
    AString password;
    const PasswordWiper wipe{password};
    RINOK(Password(callback, password))
    for (const auto &record : records) {
        Secret derived, blob, iv;
        const unsigned wrapSize = record.blob_enc_key_bits / 8;
        RINOK(Derive(callback, reinterpret_cast<const Byte *>(password.Ptr()), password.Len(),
                     record.salt, record.salt_len, record.iteration_count, derived.bytes, wrapSize))
        size_t length = record.encr_key_blob_size;
        if (record.blob_enc_algorithm == 17) {
            TripleDES des;
            des.SetKey(derived.bytes);
            memcpy(iv.bytes, record.blob_enc_iv, record.blob_enc_iv_size);
            des.SetIV(iv.bytes);
            des.DecryptCBC(blob.bytes, record.encr_key_blob, length);
        } else {
            memcpy(blob.bytes, record.encr_key_blob, length);
            memcpy(iv.bytes, record.blob_enc_iv, record.blob_enc_iv_size);
            AesDecrypt(derived.bytes, wrapSize, iv.bytes, blob.bytes, length);
        }
        const size_t unit = record.blob_enc_algorithm == 17 ? 8 : 16;
        if (!Unpad(blob.bytes, length, unit))
            continue;
        const unsigned keySize = hdr.key_bits / 8;
        if (length != keySize + hmac_.size() + 5 || memcmp(blob.bytes + length - 5, "CKIE\0", 5))
            continue;
        keySize_ = keySize;
        memcpy(key_.data(), blob.bytes, keySize_);
        memcpy(hmac_.data(), blob.bytes + keySize_, hmac_.size());
        return S_OK;
    }
    return kArchiveWrongPassword;
}
HRESULT DecryptStream::SetupV1(IArchiveOpenCallback *callback) {
    DmgCryptHeaderV1 hdr;
    if (physicalSize_ < sizeof(hdr))
        return UnexpectedEnd;
    RINOK(ReadAt(physicalSize_ - sizeof(hdr), &hdr, sizeof(hdr)))
    if (hdr.unk_4F0 != 1)
        return E_NOTIMPL;
    size_ = hdr.decrypted_data_length;
    blockSize_ = hdr.block_size;
    offset_ = 0;
    RINOK(ValidateRange(physicalSize_ - sizeof(hdr)))
    if (!KdfValid(hdr.kdf_algorithm, hdr.kdf_prng_algorithm, hdr.kdf_iteration_count,
                  hdr.kdf_salt_len))
        return E_NOTIMPL;
    for (UInt32 length : {UInt32(hdr.wrapped_aes_key_len), UInt32(hdr.wrapped_hmac_sha1_key_len)})
        if (length < 16 || length > 256 || length % 8)
            return E_INVALIDARG;
    AString password;
    const PasswordWiper wipe{password};
    RINOK(Password(callback, password))
    Secret derived;
    RINOK(Derive(callback, reinterpret_cast<const Byte *>(password.Ptr()), password.Len(),
                 hdr.kdf_salt, hdr.kdf_salt_len, hdr.kdf_iteration_count, derived.bytes, 24))
    TripleDES des;
    des.SetKey(derived.bytes);
    if (!UnwrapV1(des, hdr.wrapped_aes_key, hdr.wrapped_aes_key_len, key_.data(), 16) ||
        !UnwrapV1(des, hdr.wrapped_hmac_sha1_key, hdr.wrapped_hmac_sha1_key_len, hmac_.data(), 20))
        return kArchiveWrongPassword;
    keySize_ = 16;
    return S_OK;
}
Z7_COM7F_IMF(DecryptStream::Seek(Int64 offset, UInt32 origin, UInt64 *position)) {
    const UInt64 base = origin == STREAM_SEEK_SET   ? 0
                        : origin == STREAM_SEEK_CUR ? position_
                                                    : size_;
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
Z7_COM7F_IMF(DecryptStream::Read(void *data, UInt32 size, UInt32 *processed)) {
    COM_TRY_BEGIN
    if (processed)
        *processed = 0;
    if (!size || position_ >= size_)
        return S_OK;
    if (!keySize_ || !data)
        return E_FAIL;
    const UInt64 block = position_ / blockSize_;
    const UInt64 begin = block * blockSize_;
    const size_t count = size_t(std::min<UInt64>(blockSize_, size_ - begin));
    // One chunk per Read gives the caller's progress/cancellation boundary
    // control, including when it requests the entire image in one operation.
    std::vector<Byte> storage(((count + 15) & ~size_t(15)) + 15);
    Byte *buffer = reinterpret_cast<Byte *>((reinterpret_cast<uintptr_t>(storage.data()) + 15) &
                                            ~uintptr_t(15));
    const size_t encrypted = (count + 15) & ~size_t(15);
    RINOK(ReadAt(offset_ + begin, buffer, encrypted))
    Secret iv;
    Byte number[4];
    SetBe32(number, UInt32(block));
    NCrypto::NSha1::CHmac hmac;
    hmac.SetKey(hmac_.data(), hmac_.size());
    hmac.Update(number, sizeof(number));
    hmac.Final(iv.bytes);
    AesDecrypt(key_.data(), keySize_, iv.bytes, buffer, encrypted);
    const size_t within = size_t(position_ - begin);
    const UInt32 result = UInt32(std::min<size_t>(size, count - within));
    memcpy(data, buffer + within, result);
    Wipe(buffer, encrypted);
    Wipe(&hmac, sizeof(hmac));
    position_ += result;
    if (processed)
        *processed = result;
    return S_OK;
    COM_TRY_END
}
} // namespace NArchive::NEncryptedDmg
