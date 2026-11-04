// SPDX-License-Identifier: GPL-2.0-or-later
// apfs-fuse DiskImageFile.cpp structures; local endian access avoids platform macros.
#pragma once
#include "platform/cpu/CpuArch.h"
#include <cstdint>
namespace NArchive::NEncryptedDmg {
struct Be32 {
    Byte bytes[4];
    operator UInt32() const { return GetBe32(bytes); }
};
struct Be64 {
    Byte bytes[8];
    operator UInt64() const { return GetBe64(bytes); }
};
#pragma pack(push, 1)

struct DmgCryptHeaderV1 {
    uint8_t uuid[0x10];
    Be32 block_size;
    Be32 unk_14;
    Be32 unk_18;
    Be32 unk_1C;
    Be32 unk_20;
    Be32 unk_24;
    Be32 kdf_algorithm;
    Be32 kdf_prng_algorithm;
    Be32 kdf_iteration_count;
    Be32 kdf_salt_len;
    uint8_t kdf_salt[0x20];
    Be32 unk_58;
    Be32 unk_5C;
    Be32 unk_60;
    Be32 unk_64;
    uint8_t unwrap_iv[0x20];
    Be32 wrapped_aes_key_len;
    uint8_t wrapped_aes_key[0x100];
    Be32 unk_18C;
    Be32 unk_190;
    uint8_t unk_194[0x20];
    Be32 wrapped_hmac_sha1_key_len;
    uint8_t wrapped_hmac_sha1_key[0x100];
    Be32 unk_2B8;
    Be32 unk_2BC;
    uint8_t unk_2C0[0x20];
    Be32 wrapped_integrity_key_len;
    uint8_t wrapped_integrity_key[0x100];
    Be32 unk_3E8_len;
    uint8_t unk_3E8[0x100];
    Be64 decrypted_data_length;
    Be32 unk_4F0;
    char signature[8];
};

static_assert(sizeof(DmgCryptHeaderV1) == 0x4FC, "DmgCryptHeaderV1 invalid length.");

struct DmgCryptHeaderV2 {
    char signature[8];
    Be32 maybe_version;
    Be32 unk_0C;
    Be32 unk_10;
    Be32 unk_14;
    Be32 key_bits;
    Be32 unk_1C;
    Be32 unk_20;
    uint8_t uuid[0x10];
    Be32 block_size;
    Be64 encrypted_data_length;
    Be64 encrypted_data_offset;
    Be32 no_of_keys;
};

struct DmgKeyPointer {
    Be32 key_type;
    Be64 key_offset;
    Be64 key_length;
};

struct DmgKeyData {
    Be32 kdf_algorithm;
    Be32 prng_algorithm;
    Be32 iteration_count;
    Be32 salt_len;
    uint8_t salt[0x20];
    Be32 blob_enc_iv_size;
    uint8_t blob_enc_iv[0x20];
    Be32 blob_enc_key_bits;
    Be32 blob_enc_algorithm;
    Be32 blob_unk_5C;
    Be32 blob_enc_mode;
    Be32 encr_key_blob_size;
    uint8_t encr_key_blob[0x200];
};

#pragma pack(pop)

static_assert(sizeof(DmgCryptHeaderV2) == 76);
static_assert(sizeof(DmgKeyPointer) == 20);
static_assert(sizeof(DmgKeyData) == 616);
} // namespace NArchive::NEncryptedDmg
