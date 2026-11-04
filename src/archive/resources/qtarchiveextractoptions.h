#pragma once
#include "7zip/UI/Common/ExtractMode.h"
#include "archive/backend/archivebackend.h"
#include <optional>

// RejectExisting is enforced by each entry point's validation/collision policy.
std::optional<NExtract::NOverwriteMode::EEnum>
QtArchiveOverwriteMode(ExtractionOptions::OverwriteMode mode) noexcept;
