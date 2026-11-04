// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "7zip/Archive/IArchive.h"
#include <cstddef>

namespace NArchive::NEncryptedDmg {
void Wipe(void *data, size_t size);
struct Secret {
    alignas(16) Byte bytes[1024]{};
    ~Secret() { Wipe(bytes, sizeof(bytes)); }
};
HRESULT Derive(IArchiveOpenCallback *callback, const Byte *password, size_t passwordSize,
               const Byte *salt, size_t saltSize, UInt32 iterations, Byte *key, size_t keySize);
void AesDecrypt(const Byte *key, unsigned keySize, const Byte *iv, Byte *data, size_t size);
bool Unpad(const Byte *data, size_t &size, size_t blockSize);
} // namespace NArchive::NEncryptedDmg
