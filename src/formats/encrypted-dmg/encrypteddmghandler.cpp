// SPDX-License-Identifier: GPL-2.0-or-later
// Local archive boundary; extraction and seekable extents reuse CHandlerCont.
#include "7zip/Archive/HandlerCont.h"
#include "7zip/Common/RegisterArc.h"
#include "Common/ComTry.h"
#include "Windows/PropVariant.h"
#include "formats/encrypted-dmg/dmgdecryptstream.h"
#include "formats/sparse-image/sparsebundlestream.h"
#include "formats/sparse-image/sparseimagestream.h"

namespace NArchive::NEncryptedDmg {
class CHandler : public CHandlerCont {
    Z7_IFACE_COM7_IMP(IInArchive_Cont)
    UInt64 size_ = 0, physical_ = 0;
    unsigned kind_;
    bool encrypted_ = false;
    int GetItem_ExtractInfo(UInt32 index, UInt64 &position, UInt64 &size) const override {
        position = 0;
        size = size_;
        return index || !_stream ? NExtract::NOperationResult::kUnavailable
                                 : NExtract::NOperationResult::kOK;
    }

  public:
    explicit CHandler(unsigned kind = 0) : kind_(kind) {}
};
static const Byte kProps[] = {kpidPath, kpidSize, kpidPackSize, kpidEncrypted};
static const CStatProp kArcProps[] = {{nullptr, kpidEncrypted, VT_BOOL},
                                      {nullptr, kpidReadOnly, VT_BOOL},
                                      {nullptr, kpidPhySize, VT_UI8},
                                      {"Read-only limitations", kpidUserDefined, VT_BSTR}};
IMP_IInArchive_Props IMP_IInArchive_ArcProps_WITH_NAME

Z7_COM7F_IMF(CHandler::Open(IInStream *stream, const UInt64 *, IArchiveOpenCallback *callback)) {
    COM_TRY_BEGIN
    Close();
    if (kind_ == 2) {
        CMyComPtr2_Create<IInStream, NSparseImage::SparseBundleStream> bundle;
        RINOK(bundle->Open(stream, callback))
        size_ = bundle->Size();
        physical_ = 0;
        encrypted_ = bundle->Encrypted();
        _stream = bundle;
    } else if (kind_ == 1) {
        CMyComPtr2_Create<IInStream, NSparseImage::SparseImageStream> sparse;
        RINOK(sparse->Open(stream, callback))
        size_ = sparse->Size();
        physical_ = sparse->PhysicalSize();
        _stream = sparse;
        encrypted_ = false;
    } else {
        CMyComPtr2_Create<IInStream, DecryptStream> decrypted;
        RINOK(decrypted->Open(stream, callback))
        size_ = decrypted->Size();
        physical_ = decrypted->PhysicalSize();
        _stream = decrypted;
        encrypted_ = true;
    }
    return S_OK;
    COM_TRY_END
}
Z7_COM7F_IMF(CHandler::Close()) {
    _stream.Release();
    size_ = physical_ = 0;
    encrypted_ = false;
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
        if (kind_ != 2)
            prop = physical_;
        break;
    case kpidEncrypted:
        prop = encrypted_;
        break;
    }
    prop.Detach(value);
    return S_OK;
}
Z7_COM7F_IMF(CHandler::GetArchiveProperty(PROPID id, PROPVARIANT *value)) {
    NWindows::NCOM::CPropVariant prop;
    switch (id) {
    case kpidReadOnly:
        prop = true;
        break;
    case kpidUserDefined:
        if (kind_ == 2 && _stream &&
            static_cast<NSparseImage::SparseBundleStream *>(_stream.Interface())->DamageSeen())
            prop = L"Some parts of the sparsebundle are damaged or unreadable. Readable files can "
                   L"still be copied; files using damaged ranges may be incomplete.\n"
                   L"Missing bands are treated as sparse holes. Deleted bands cannot be "
                   L"distinguished from unused space.\n"
                   L"Decryption does not verify data integrity.";
        break;
    case kpidMainSubfile:
        if (_stream)
            prop = UInt32(0);
        break;
    case kpidPhySize:
        if (_stream && kind_ != 2)
            prop = physical_;
        break;
    case kpidEncrypted:
        prop = encrypted_;
        break;
    }
    prop.Detach(value);
    return S_OK;
}
// No fixed-offset signature: v1 is identified by the footer, v2 by the header.
// Open checks both before requesting a password; ordinary UDIF stays in Dmg.
REGISTER_ARC_I_NO_SIG("EncryptedDmg", "dmg sparseimage", nullptr, 0xF7, 0,
                      NArcInfoFlags::kPureStartOpen, nullptr)
} // namespace NArchive::NEncryptedDmg

namespace NArchive::NSparseImage {
class CHandler final : public NEncryptedDmg::CHandler {
  public:
    CHandler() : NEncryptedDmg::CHandler(true) {}
};
REGISTER_ARC_I_NO_SIG("SparseImage", "sparseimage", nullptr, 0xF8, 0, NArcInfoFlags::kPureStartOpen,
                      nullptr)
} // namespace NArchive::NSparseImage

namespace NArchive::NSparseBundle {
class CHandler final : public NEncryptedDmg::CHandler {
  public:
    CHandler() : NEncryptedDmg::CHandler(2) {}
};
REGISTER_ARC_I_NO_SIG("SparseBundle", "sparsebundle plist", nullptr, 0xF9, 0,
                      NArcInfoFlags::kPureStartOpen, nullptr)
} // namespace NArchive::NSparseBundle
