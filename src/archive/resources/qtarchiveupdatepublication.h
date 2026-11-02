#pragma once
#include "Common/MyWindows.h"
#include <optional>

// One joined update callback owns this state. A provider that does not report
// publication leaves it unknown; an HRESULT alone cannot prove rollback safe.
class QtArchiveUpdatePublication {
  public:
    virtual ~QtArchiveUpdatePublication() = default;
    std::optional<bool> Published;
};

struct QtArchiveUpdateResult {
    HRESULT Result;
    std::optional<bool> Published;
};
