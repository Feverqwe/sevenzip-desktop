#pragma once
#include "Common/MyWindows.h"
#include "archive/resources/archivesourcesnapshot.h"
#include <QStringList>
#include <optional>

// One joined update callback owns this state. A provider that does not report
// publication leaves it unknown; an HRESULT alone cannot prove rollback safe.
class QtArchiveUpdatePublication {
  public:
    virtual ~QtArchiveUpdatePublication() = default;
    std::optional<bool> Published;
    // An external edit targets the generation that supplied its temporary copy.
    std::optional<ArchiveSourceSnapshot> ExpectedSource;
};

struct QtArchiveUpdateResult {
    HRESULT Result;
    std::optional<bool> Published;
    QStringList Diagnostics = {};
};
