#pragma once

#include <QLocale>
#include <QString>
#include <optional>

// Presentation only: callers retain the original unsigned value for operations
// and sorting. An absent value is different from an empty file.
namespace QtRegionalFormat {
QString bytes(std::optional<quint64> value);
QString bytes(std::optional<quint64> value, const QLocale &locale);
QString exactBytes(std::optional<quint64> value);
QString exactBytes(std::optional<quint64> value, const QLocale &locale);
} // namespace QtRegionalFormat
