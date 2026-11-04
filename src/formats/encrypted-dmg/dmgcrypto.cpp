// SPDX-License-Identifier: GPL-2.0-or-later
// Existing SDK primitives; cancellation brackets each bounded derivation.
#include "formats/encrypted-dmg/dmgcrypto.h"
#include "7zip/Crypto/Pbkdf2HmacSha1.h"
#include "C/Aes.h"

namespace NArchive::NEncryptedDmg {
void Wipe(void *data, size_t size) {
    volatile Byte *p = static_cast<volatile Byte *>(data);
    while (size--)
        *p++ = 0;
}
HRESULT Derive(IArchiveOpenCallback *callback, const Byte *password, size_t passwordSize,
               const Byte *salt, size_t saltSize, UInt32 iterations, Byte *key, size_t keySize) {
    if (callback) {
        RINOK(callback->SetCompleted(nullptr, nullptr))
    }
    NCrypto::NSha1::Pbkdf2Hmac(password, passwordSize, salt, saltSize, iterations, key, keySize);
    if (callback) {
        RINOK(callback->SetCompleted(nullptr, nullptr))
    }
    return S_OK;
}
void AesDecrypt(const Byte *key, unsigned keySize, const Byte *iv, Byte *data, size_t size) {
    static const bool initialized = [] {
        AesGenTables();
        return true;
    }();
    (void)initialized;
    alignas(16) UInt32 aes[AES_NUM_IVMRK_WORDS];
    Aes_SetKey_Dec(aes + 4, key, keySize);
    AesCbc_Init(aes, iv);
    AesCbc_Decode(aes, data, size / AES_BLOCK_SIZE);
    Wipe(aes, sizeof(aes));
}
bool Unpad(const Byte *data, size_t &size, size_t blockSize) {
    if (!size || size % blockSize)
        return false;
    const unsigned pad = data[size - 1];
    if (!pad || pad > blockSize || pad > size)
        return false;
    for (size_t i = size - pad; i < size; ++i)
        if (data[i] != pad)
            return false;
    size -= pad;
    return true;
}
} // namespace NArchive::NEncryptedDmg
