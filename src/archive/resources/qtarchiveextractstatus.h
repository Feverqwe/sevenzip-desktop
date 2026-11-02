#pragma once
#include <QStringList>
#include <optional>

enum class QtArchiveExtractPurpose { Copy, Move, ArchiveStaging };

struct QtArchiveTransferInputs {
    bool SymLinks = false;
    bool HardLinks = false;
    QStringList AltStreamPaths;
};

// Facts from one completed native extraction, separate from its HRESULT.
struct QtArchiveExtractStatus {
    bool Complete = true;
    bool StagingCompatible = true;
    QtArchiveTransferInputs Inputs;
};

struct QtArchiveExtractReport {
    QtArchiveExtractPurpose Purpose = QtArchiveExtractPurpose::Copy;
    std::optional<QtArchiveExtractStatus> Status;
};

class QtArchiveExtractStatusReceiver {
  public:
    virtual ~QtArchiveExtractStatusReceiver() = default;
    QtArchiveExtractReport ExtractReport;
};
