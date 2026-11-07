#include "ui/formatting/qtregionalformat.h"
#include "platform/locale/qtregionalsettings.h"
#include <cmath>

namespace {
QString withUnit(const QString &number, quint64 scale, const QLocale &locale) {
    // Qt exposes localized units only through the signed formattedDataSize API.
    // A positive carrier (at most 2^60) obtains its presentation suffix without
    // narrowing the actual byte count or reading the displayed number back.
    const QString carrier =
        locale.formattedDataSize(qint64(scale), 0, QLocale::DataSizeTraditionalFormat);
    const QString prefix = scale == 1 ? locale.toString(qint64(1)) : locale.toString(1.0, 'f', 0);
    if (!carrier.startsWith(prefix)) {
        // Preserve the number if a future Qt changes its presentation template.
        // The all-locales regression checks this adapter when Qt is updated.
        qWarning("Unexpected QLocale data-size template");
        return number;
    }
    return number + carrier.mid(prefix.size());
}
QString formatBytes(std::optional<quint64> value, const QLocale &locale, const QLocale &units,
                    bool system = false) {
    if (!value)
        return QString::fromUtf8("—");
    quint64 scale = 1;
    while (scale <= *value / 1024 && scale < (quint64(1) << 60))
        scale *= 1024;
    if (scale == 1)
        return withUnit(system ? QtSystemRegionalNumber(*value)
                               : locale.toString(qulonglong(*value)),
                        scale, units);
    double rounded = std::round(double(*value) / double(scale) * 100) / 100;
    if (rounded >= 1024 && scale < (quint64(1) << 60)) {
        scale *= 1024;
        rounded = std::round(double(*value) / double(scale) * 100) / 100;
    }
    const int precision = rounded == std::floor(rounded)             ? 0
                          : rounded * 10 == std::round(rounded * 10) ? 1
                                                                     : 2;
    return withUnit(system ? QtSystemRegionalNumber(rounded, precision)
                           : locale.toString(rounded, 'f', precision),
                    scale, units);
}
} // namespace

QString QtRegionalFormat::bytes(std::optional<quint64> value, const QLocale &locale) {
    return formatBytes(value, locale, locale);
}

QString QtRegionalFormat::bytes(std::optional<quint64> value) {
    return formatBytes(value, QLocale::system(), QtSystemRegionalUnitsLocale(), true);
}

QString QtRegionalFormat::exactBytes(std::optional<quint64> value) {
    return value ? withUnit(QtSystemRegionalNumber(*value), 1, QtSystemRegionalUnitsLocale())
                 : QString::fromUtf8("—");
}

QString QtRegionalFormat::exactBytes(std::optional<quint64> value, const QLocale &locale) {
    return value ? withUnit(locale.toString(qulonglong(*value)), 1, locale)
                 : QString::fromUtf8("—");
}
