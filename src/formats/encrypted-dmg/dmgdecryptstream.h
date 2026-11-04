// SPDX-License-Identifier: GPL-2.0-or-later
// Adapted from apfs-fuse DiskImageFile, Simon Gander; see the DMG source audit.
#pragma once
#include "7zip/Archive/IArchive.h"
#include "Common/MyCom.h"
#include <array>
#include <vector>

namespace NArchive::NEncryptedDmg {
class DecryptStream final : public IInStream, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_2(IInStream, ISequentialInStream)
    Z7_IFACE_COM7_IMP(IInStream)
    Z7_IFACE_COM7_IMP(ISequentialInStream)
    CMyComPtr<IInStream> input_;
    UInt64 physicalSize_ = 0, offset_ = 0, size_ = 0, position_ = 0;
    UInt32 blockSize_ = 0;
    std::array<Byte, 32> key_{};
    std::array<Byte, 20> hmac_{};
    unsigned keySize_ = 0;
    bool token_ = false;
    HRESULT ReadAt(UInt64 offset, void *data, size_t size);
    HRESULT SetupV1(IArchiveOpenCallback *callback);
    HRESULT SetupV2(IArchiveOpenCallback *callback);
    HRESULT ValidateRange(UInt64 available) const;

  public:
    ~DecryptStream();
    HRESULT Open(IInStream *input, IArchiveOpenCallback *callback, bool token = false);
    HRESULT OpenBand(IInStream *input, IInStream **stream) const;
    UInt32 BlockSize() const { return blockSize_; }
    UInt64 Size() const { return size_; }
    UInt64 PhysicalSize() const { return physicalSize_; }
};
} // namespace NArchive::NEncryptedDmg
