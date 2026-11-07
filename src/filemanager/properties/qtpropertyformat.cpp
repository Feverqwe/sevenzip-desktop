// FILETIME precision interpretation adapted from 7-Zip
// CPP/7zip/UI/Common/PropIDUtils.cpp, b88c6bc09339de32f7d18bbab900686b244f42ba.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "filemanager/properties/qtpropertyformat.h"
#include "Windows/PropVariantConv.h"
#include "platform/locale/qtregionalsettings.h"
#include <QDateTime>
#include <QTimeZone>

std::optional<UInt64> QtUnsignedPropertyValue(const PROPVARIANT &value) noexcept {
    switch (value.vt) {
    case VT_UI1:
    case VT_UI2:
    case VT_UI4:
    case VT_UI8: {
        UInt64 result = 0;
        ConvertPropVariantToUInt64(value, result);
        return result;
    }
    default:
        return std::nullopt;
    }
}

namespace {
QtPropertyTimeText FormatTime(const PROPVARIANT &value, const QLocale &locale, bool system) {
    const QString unknown = QString::fromUtf8("—");
    if (value.vt != VT_FILETIME)
        return {unknown, unknown};
    const UInt64 ticks =
        (UInt64(value.filetime.dwHighDateTime) << 32) | value.filetime.dwLowDateTime;
    unsigned digits = 7, extraNanoseconds = 0;
    const unsigned precision = value.wReserved1;
    if (precision && precision <= k_PropVar_TimePrec_1ns && value.wReserved2 < 100 &&
        value.wReserved3 == 0) {
        extraNanoseconds = value.wReserved2;
        if (precision == k_PropVar_TimePrec_Unix || precision == k_PropVar_TimePrec_DOS)
            digits = 0;
        else if (precision == k_PropVar_TimePrec_HighPrec)
            digits = 9;
        else if (precision >= k_PropVar_TimePrec_Base)
            digits = precision - k_PropVar_TimePrec_Base;
    }
    if (!ticks && !extraNanoseconds)
        return {unknown, unknown};
    // Division before narrowing also covers every unsigned FILETIME value.
    constexpr qint64 epochMilliseconds = 11644473600000LL;
    const QDateTime utc =
        QDateTime::fromMSecsSinceEpoch(qint64(ticks / 10000) - epochMilliseconds, QTimeZone::UTC);
    const QDateTime local = utc.toLocalTime();
    if (!utc.isValid() || !local.isValid())
        return {unknown, unknown};
    QString exact = utc.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    if (digits) {
        const UInt64 nanoseconds = ticks % 10000000 * 100 + extraNanoseconds;
        exact += '.' + QString::number(nanoseconds).rightJustified(9, '0').left(int(digits));
    }
    exact += QStringLiteral(" UTC");
    const QString shortText = system ? QtSystemRegionalDateTime(local, QLocale::ShortFormat)
                                     : locale.toString(local, QLocale::ShortFormat);
    const QString longText = system ? QtSystemRegionalDateTime(local, QLocale::LongFormat)
                                    : locale.toString(local, QLocale::LongFormat);
    return {shortText.isEmpty() ? unknown : shortText,
            longText.isEmpty() ? exact : longText + '\n' + exact};
}
} // namespace

QtPropertyTimeText QtFormatPropertyTime(const PROPVARIANT &value, const QLocale &locale) {
    return FormatTime(value, locale, false);
}

QtPropertyTimeText QtFormatPropertyTime(const PROPVARIANT &value) {
    return FormatTime(value, QLocale::system(), true);
}
