// SPDX-License-Identifier: GPL-2.0-or-later
// Project native adapter around the BSD C band mapper and shared DMG crypto.
#pragma once
#include "7zip/Archive/IArchive.h"
#include "Common/MyCom.h"
#include "Common/MyString.h"
#include "formats/encrypted-dmg/dmgdecryptstream.h"
#include <list>

namespace NArchive::NSparseImage {
class SparseBundleStream final : public IInStream, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_2(IInStream, ISequentialInStream)
    Z7_IFACE_COM7_IMP(IInStream)
    Z7_IFACE_COM7_IMP(ISequentialInStream)
    CMyComPtr<IArchiveOpenVolumeCallback> volumes_;
    CMyComPtr2<IInStream, NEncryptedDmg::DecryptStream> token_;
    struct Band {
        UInt64 Number = 0, Length = 0;
        bool DamagedTail = false;
        CMyComPtr<IInStream> Stream;
    };
    std::list<Band> bands_;
    UString prefix_;
    UInt64 size_ = 0, bandSize_ = 0, position_ = 0;
    bool encrypted_ = false;
    bool damageSeen_ = false;
    HRESULT ReadBand(UInt64 band, UInt64 offset, void *data, UInt32 size);
    static int32_t ReadBandCallback(void *context, uint64_t band, uint64_t offset, void *data,
                                    uint32_t size);

  public:
    HRESULT Open(IInStream *input, IArchiveOpenCallback *callback);
    UInt64 Size() const { return size_; }
    bool Encrypted() const { return encrypted_; }
    bool DamageSeen() const { return damageSeen_; }
};
} // namespace NArchive::NSparseImage
