// SPDX-License-Identifier: GPL-2.0-or-later
// Local archive boundary; extraction and seekable extents reuse CHandlerCont.
#include "7zip/Archive/HandlerCont.h"
#include "7zip/Common/RegisterArc.h"
#include "Common/ComTry.h"
#include "Windows/PropVariant.h"
#include "formats/encrypted-dmg/dmgdecryptstream.h"

namespace NArchive::NEncryptedDmg {
class CHandler final : public CHandlerCont {
    Z7_IFACE_COM7_IMP(IInArchive_Cont)
    UInt64 size_ = 0, physical_ = 0;
    int GetItem_ExtractInfo(UInt32 index, UInt64 &position, UInt64 &size) const override {
        position = 0;
        size = size_;
        return index || !_stream ? NExtract::NOperationResult::kUnavailable
                                 : NExtract::NOperationResult::kOK;
    }
};
static const Byte kProps[] = {kpidPath, kpidSize, kpidPackSize, kpidEncrypted};
static const Byte kArcProps[] = {kpidEncrypted, kpidPhySize};
IMP_IInArchive_Props IMP_IInArchive_ArcProps

Z7_COM7F_IMF(CHandler::Open(IInStream *stream, const UInt64 *, IArchiveOpenCallback *callback)) {
    COM_TRY_BEGIN
    Close();
    CMyComPtr2_Create<IInStream, DecryptStream> decrypted;
    RINOK(decrypted->Open(stream, callback))
    size_ = decrypted->Size();
    physical_ = decrypted->PhysicalSize();
    _stream = decrypted;
    return S_OK;
    COM_TRY_END
}
Z7_COM7F_IMF(CHandler::Close()) {
    _stream.Release();
    size_ = physical_ = 0;
    return S_OK;
}
Z7_COM7F_IMF(CHandler::GetNumberOfItems(UInt32 *count)) {
    *count = _stream ? 1 : 0;
    return S_OK;
}
Z7_COM7F_IMF(CHandler::GetProperty(UInt32 index, PROPID id, PROPVARIANT *value)) {
    if (index || !_stream)
        return E_INVALIDARG;
    NWindows::NCOM::CPropVariant prop;
    switch (id) {
    case kpidPath:
        prop = L"image.dmg";
        break;
    case kpidSize:
        prop = size_;
        break;
    case kpidPackSize:
        prop = physical_;
        break;
    case kpidEncrypted:
        prop = true;
        break;
    }
    prop.Detach(value);
    return S_OK;
}
Z7_COM7F_IMF(CHandler::GetArchiveProperty(PROPID id, PROPVARIANT *value)) {
    NWindows::NCOM::CPropVariant prop;
    switch (id) {
    case kpidMainSubfile:
        if (_stream)
            prop = UInt32(0);
        break;
    case kpidPhySize:
        if (_stream)
            prop = physical_;
        break;
    case kpidEncrypted:
        prop = true;
        break;
    }
    prop.Detach(value);
    return S_OK;
}
// No fixed-offset signature: v1 is identified by the footer, v2 by the header.
// Open checks both before requesting a password; ordinary UDIF stays in Dmg.
REGISTER_ARC_I_NO_SIG("EncryptedDmg", "dmg", nullptr, 0xF7, 0, NArcInfoFlags::kPureStartOpen,
                      nullptr)
} // namespace NArchive::NEncryptedDmg
