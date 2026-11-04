// SPDX-License-Identifier: GPL-2.0-or-later
// Native stream/lifetime boundary for the shared Linux exFAT reader.
#pragma once
#include "7zip/Archive/IArchive.h"
#include "Common/MyCom.h"
#include "formats/exfat/exfatreader.h"
#include <memory>
#include <mutex>
#include <set>

namespace NArchive::NExfat {
struct ReadContext {
    CMyComPtr<IInStream> Stream;
    IArchiveOpenCallback *OpenCallback = nullptr;
    IArchiveExtractCallback *ExtractCallback = nullptr;
    HRESULT TransportResult = S_OK, CallbackResult = S_OK;
    std::set<UInt32> VisitedClusters;
    UInt64 Completed = 0, Files = 0;
    std::mutex StreamMutex;
    static int Read(void *, uint64_t, void *, size_t);
    static int Poll(void *);
    static int VisitCluster(void *, uint32_t);
};
HRESULT CreateItemStream(std::shared_ptr<ReadContext>, std::shared_ptr<exfat_volume>,
                         const exfat_item &, ISequentialInStream **);
} // namespace NArchive::NExfat
