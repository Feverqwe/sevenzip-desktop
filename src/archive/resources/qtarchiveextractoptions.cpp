#include "archive/resources/qtarchiveextractoptions.h"

std::optional<NExtract::NOverwriteMode::EEnum>
QtArchiveOverwriteMode(ExtractionOptions::OverwriteMode mode) noexcept {
    switch (mode) {
    case ExtractionOptions::OverwriteMode::Ask:
        return NExtract::NOverwriteMode::kAsk;
    case ExtractionOptions::OverwriteMode::OverwriteExisting:
        return NExtract::NOverwriteMode::kOverwrite;
    case ExtractionOptions::OverwriteMode::SkipExisting:
        return NExtract::NOverwriteMode::kSkip;
    case ExtractionOptions::OverwriteMode::RenameExtracted:
        return NExtract::NOverwriteMode::kRename;
    case ExtractionOptions::OverwriteMode::RenameExisting:
        return NExtract::NOverwriteMode::kRenameExisting;
    case ExtractionOptions::OverwriteMode::RejectExisting:
        return std::nullopt;
    }
    return NExtract::NOverwriteMode::kAsk;
}
