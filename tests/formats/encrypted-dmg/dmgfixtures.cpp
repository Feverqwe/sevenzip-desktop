// Artificial images with fixed test-only keys. No production writer is added.
#include "dmgfixtures.h"
#include "7zip/Crypto/HmacSha1.h"
#include "7zip/Crypto/Pbkdf2HmacSha1.h"
#include "C/Aes.h"
#include "dependencies/apfscrypto/TripleDes.h"
#include "platform/cpu/CpuArch.h"
#include <QtEndian>
#include <algorithm>
#include <cstring>

namespace {
void Put32(QByteArray &out, size_t at, quint32 value) { qToBigEndian(value, out.data() + at); }
void Put64(QByteArray &out, size_t at, quint64 value) { qToBigEndian(value, out.data() + at); }
QByteArray Pad(QByteArray bytes, unsigned unit) {
    const unsigned n = unit - unsigned(bytes.size()) % unit;
    return bytes + QByteArray(n, char(n));
}
void EncryptAes(QByteArray &bytes, const Byte *key, unsigned keySize, const Byte *iv) {
    static const bool initialized = [] {
        AesGenTables();
        return true;
    }();
    (void)initialized;
    alignas(16) UInt32 aes[AES_NUM_IVMRK_WORDS];
    Aes_SetKey_Enc(aes + 4, key, keySize);
    AesCbc_Init(aes, iv);
    // QByteArray is not required to have the SDK's 16-byte alignment.
    for (qsizetype i = 0; i < bytes.size(); i += 16) {
        alignas(16) Byte block[16];
        memcpy(block, bytes.constData() + i, 16);
        AesCbc_Encode(aes, block, 1);
        memcpy(bytes.data() + i, block, 16);
    }
}
QByteArray WrapV1(const Byte *key, size_t size, const Byte *derived) {
    QByteArray bytes(12, '\0');
    bytes.append(reinterpret_cast<const char *>(key), qsizetype(size));
    bytes = Pad(bytes, 8);
    TripleDES des;
    des.SetKey(derived);
    des.SetIV(nullptr);
    des.EncryptCBC(reinterpret_cast<Byte *>(bytes.data()),
                   reinterpret_cast<const Byte *>(bytes.constData()), size_t(bytes.size()));
    std::reverse(bytes.begin(), bytes.end());
    bytes = Pad(bytes, 8);
    const Byte iv[] = {0x4a, 0xdd, 0xa2, 0x2c, 0x79, 0xe8, 0x21, 0x05};
    des.SetIV(iv);
    des.EncryptCBC(reinterpret_cast<Byte *>(bytes.data()),
                   reinterpret_cast<const Byte *>(bytes.constData()), size_t(bytes.size()));
    return bytes;
}
} // namespace

QByteArray DmgFixture(const QByteArray &plain, unsigned keyBits, bool aesWrap, bool version1) {
    Byte key[32], hmacKey[20], salt[20], derived[24], iv[16]{};
    for (unsigned i = 0; i < 32; ++i)
        key[i] = Byte(i + 17);
    for (unsigned i = 0; i < 20; ++i) {
        hmacKey[i] = Byte(i + 53);
        salt[i] = Byte(i + 91);
    }
    const QByteArray password("fixture-password");
    NCrypto::NSha1::Pbkdf2Hmac(reinterpret_cast<const Byte *>(password.constData()),
                               size_t(password.size()), salt, 20, 1000, derived, 24);
    QByteArray encrypted;
    for (qsizetype pos = 0; pos < plain.size(); pos += 512) {
        QByteArray chunk = plain.mid(pos, 512);
        chunk.resize((chunk.size() + 15) & ~qsizetype(15), '\0');
        Byte number[4];
        SetBe32(number, UInt32(pos / 512));
        alignas(16) Byte digest[20];
        NCrypto::NSha1::CHmac hmac;
        hmac.SetKey(hmacKey, sizeof(hmacKey));
        hmac.Update(number, sizeof(number));
        hmac.Final(digest);
        EncryptAes(chunk, key, keyBits / 8, digest);
        encrypted += chunk;
    }
    if (version1) {
        QByteArray header(0x4fc, '\0');
        Put32(header, 0x10, 512);
        Put32(header, 0x28, 103);
        Put32(header, 0x30, 1000);
        Put32(header, 0x34, 20);
        memcpy(header.data() + 0x38, salt, 20);
        const auto aes = WrapV1(key, 16, derived), hmac = WrapV1(hmacKey, 20, derived);
        Put32(header, 0x88, quint32(aes.size()));
        memcpy(header.data() + 0x8c, aes.constData(), size_t(aes.size()));
        Put32(header, 0x1b4, quint32(hmac.size()));
        memcpy(header.data() + 0x1b8, hmac.constData(), size_t(hmac.size()));
        Put64(header, 0x4e8, quint64(plain.size()));
        Put32(header, 0x4f0, 1);
        memcpy(header.data() + 0x4f4, "cdsaencr", 8);
        return encrypted + header;
    }
    QByteArray blob(reinterpret_cast<const char *>(key), keyBits / 8);
    blob.append(reinterpret_cast<const char *>(hmacKey), 20);
    blob.append("CKIE\0", 5);
    blob = Pad(blob, aesWrap ? 16 : 8);
    if (aesWrap)
        EncryptAes(blob, derived, 24, iv);
    else {
        TripleDES des;
        des.SetKey(derived);
        des.SetIV(iv);
        des.EncryptCBC(reinterpret_cast<Byte *>(blob.data()),
                       reinterpret_cast<const Byte *>(blob.constData()), size_t(blob.size()));
    }
    QByteArray header(4096, '\0');
    memcpy(header.data(), "encrcdsa", 8);
    Put32(header, 8, 2);
    Put32(header, 12, 16);
    Put32(header, 16, 5);
    Put32(header, 20, 0x80000001);
    Put32(header, 24, keyBits);
    Put32(header, 28, 91);
    Put32(header, 32, 160);
    Put32(header, 52, 512);
    Put64(header, 56, quint64(plain.size()));
    Put64(header, 64, 4096);
    Put32(header, 72, 1);
    Put32(header, 76, 1);
    Put64(header, 80, 96);
    Put64(header, 88, 104 + quint64(blob.size()));
    Put32(header, 96, 103);
    Put32(header, 104, 1000);
    Put32(header, 108, 20);
    memcpy(header.data() + 112, salt, 20);
    Put32(header, 144, 8);
    Put32(header, 180, 192);
    Put32(header, 184, aesWrap ? 0x80000001 : 17);
    Put32(header, 188, 7);
    Put32(header, 192, 6);
    Put32(header, 196, quint32(blob.size()));
    memcpy(header.data() + 200, blob.constData(), size_t(blob.size()));
    return header + encrypted;
}
