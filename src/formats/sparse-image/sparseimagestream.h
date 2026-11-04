// SPDX-License-Identifier: GPL-2.0-or-later
// Adapted from apfs-fuse DeviceSparseImage; see the sparse image source audit.
#pragma once
#include "7zip/Archive/IArchive.h"
#include "Common/MyCom.h"
#include <map>

namespace NArchive::NSparseImage {
class SparseImageStream final : public IInStream, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_2(IInStream, ISequentialInStream)
    Z7_IFACE_COM7_IMP(IInStream)
    Z7_IFACE_COM7_IMP(ISequentialInStream)
    CMyComPtr<IInStream> input_;
    std::map<UInt32, UInt64> bands_;
    UInt64 size_ = 0, bandSize_ = 0, physical_ = 0, position_ = 0;
    HRESULT ReadAt(UInt64 offset, void *data, size_t size);

  public:
    HRESULT Open(IInStream *input, IArchiveOpenCallback *callback);
    UInt64 Size() const { return size_; }
    UInt64 PhysicalSize() const { return physical_; }
};
} // namespace NArchive::NSparseImage
