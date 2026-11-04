#pragma once
#include "7zip/IStream.h"
#include <memory>
struct IProgress;
class ArchiveNativeOpen;
// Independent cursor over the opened filesystem's already decoded input.
// Retains its source generation and serializes only individual transport reads.
HRESULT CreateArchiveInputStream(IUnknown *resource, std::shared_ptr<ArchiveNativeOpen> reader,
                                 UInt64 containingDepth, IInStream **stream);

// Retain the copied codec image and owned input beside a native item stream.
HRESULT CreateArchiveItemStream(IUnknown *resource, UInt32 index, ISequentialInStream **stream,
                                IProgress *request = nullptr);
// Explicit retained generation, independent of the resource's current panel reader.
HRESULT CreateArchiveItemStream(IUnknown *resource, std::shared_ptr<ArchiveNativeOpen> reader,
                                UInt64 containingDepth, UInt32 index, ISequentialInStream **stream,
                                IProgress *request = nullptr);
