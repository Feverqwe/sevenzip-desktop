#include "filemanager/properties/qtpropertyformat.h"
#include <QDateTime>
#include <QTimeZone>
#include <QtTest>

class RegionalPropertyFormatTests final : public QObject {
    Q_OBJECT
  private slots:
    void nativeUtcTimeUsesRegionalClockAndRetainsNanoseconds() {
        const UInt64 seconds = 15 * 3600 + 4 * 60 + 5;
        const UInt64 ticks = (11644473600ULL + seconds) * 10000000 + 1234567;
        NWindows::NCOM::CPropVariant value;
        const FILETIME time{UInt32(ticks), UInt32(ticks >> 32)};
        value.SetAsTimeFrom_FT_Prec_Ns100(time, k_PropVar_TimePrec_1ns, 89);
        const QDateTime expected =
            QDateTime(QDate(1970, 1, 1), QTime(15, 4, 5, 123), QTimeZone::UTC).toLocalTime();
        for (const QLocale &locale : {QLocale(QLocale::English, QLocale::UnitedStates),
                                      QLocale(QLocale::English, QLocale::UnitedKingdom),
                                      QLocale(QLocale::Russian, QLocale::Russia)}) {
            const auto text = QtFormatPropertyTime(value, locale);
            QCOMPARE(text.display, locale.toString(expected, QLocale::ShortFormat));
            QVERIFY(text.detail.contains(locale.toString(expected, QLocale::LongFormat)));
            QVERIFY(text.detail.endsWith("1970-01-01 15:04:05.123456789 UTC"));
        }
        value.wReserved1 = k_PropVar_TimePrec_Unix;
        const auto secondsOnly = QtFormatPropertyTime(value);
        QVERIFY(secondsOnly.detail.endsWith("1970-01-01 15:04:05 UTC"));
    }

    void absentTimeAndTextDoNotInventAnEpochOrZone() {
        NWindows::NCOM::CPropVariant value;
        QCOMPARE(QtFormatPropertyTime(value).display, QString::fromUtf8("—"));
        value = FILETIME{};
        QCOMPARE(QtFormatPropertyTime(value).display, QString::fromUtf8("—"));
        value = L"2026-09-28 12:34:56";
        QCOMPARE(QtFormatPropertyTime(value).display, QString::fromUtf8("—"));
        value.Set_Int64(-1);
        QVERIFY(!QtUnsignedPropertyValue(value));
        value = UInt64(-1);
        QCOMPARE(*QtUnsignedPropertyValue(value), UInt64(-1));
    }
};

QTEST_GUILESS_MAIN(RegionalPropertyFormatTests)
#include "regional_property_format_tests.moc"
