#pragma once
#include "Windows/PropVariant.h"
#include <QLocale>
#include <optional>

// Only unsigned native scalars are byte counts. Missing, signed and unrelated
// variants remain unavailable rather than reaching the throwing converter.
std::optional<UInt64> QtUnsignedPropertyValue(const PROPVARIANT &value) noexcept;

struct QtPropertyTimeText {
    QString display;
    QString detail;
};
// Native FILETIME is already normalized by the provider. Never reinterpret a
// formatted compatibility string or apply a second local-to-UTC conversion.
QtPropertyTimeText QtFormatPropertyTime(const PROPVARIANT &value);
QtPropertyTimeText QtFormatPropertyTime(const PROPVARIANT &value, const QLocale &locale);
