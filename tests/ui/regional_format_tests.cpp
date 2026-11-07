#include "ui/formatting/qtregionalformat.h"
#include <QtTest>
#include <limits>

class RegionalFormatTests final : public QObject {
    Q_OBJECT
  private slots:
    void systemExactBytesKeepsEveryUnsignedDigit() {
        QString digits;
        for (QChar character : QtRegionalFormat::exactBytes(std::numeric_limits<quint64>::max()))
            if (character.isDigit())
                digits += QLatin1Char(char('0' + character.digitValue()));
        QCOMPARE(digits, QStringLiteral("18446744073709551615"));
    }

    void unsignedSizesAndUnknown() {
        const QLocale en(QLocale::English, QLocale::UnitedStates);
        QCOMPARE(QtRegionalFormat::bytes(std::nullopt, en), QString::fromUtf8("—"));
        QCOMPARE(QtRegionalFormat::bytes(0, en), QStringLiteral("0 bytes"));
        QCOMPARE(QtRegionalFormat::bytes(1024, en), QStringLiteral("1 kB"));
        QCOMPARE(QtRegionalFormat::bytes(1536, en), QStringLiteral("1.5 kB"));
        QCOMPARE(QtRegionalFormat::bytes(1024 * 1024 - 1, en), QStringLiteral("1 MB"));
        const quint64 maximum = std::numeric_limits<quint64>::max();
        QCOMPARE(QtRegionalFormat::bytes(maximum, en), QStringLiteral("16 EB"));
        QCOMPARE(QtRegionalFormat::exactBytes(maximum, en),
                 QStringLiteral("18,446,744,073,709,551,615 bytes"));
    }

    void localizedUnitsAndFractions() {
        // Exercise every Qt locale: a Qt template change must not silently lose
        // a unit in this unsigned adapter, including non-Latin number systems.
        for (const QLocale &locale : QLocale::matchingLocales(
                 QLocale::AnyLanguage, QLocale::AnyScript, QLocale::AnyTerritory)) {
            for (qint64 scale = 1; scale <= (qint64(1) << 60);) {
                QCOMPARE(QtRegionalFormat::bytes(quint64(scale), locale),
                         locale.formattedDataSize(scale, 0, QLocale::DataSizeTraditionalFormat));
                if (scale == (qint64(1) << 60))
                    break;
                scale *= 1024;
            }
            QCOMPARE(QtRegionalFormat::bytes(1536, locale),
                     locale.formattedDataSize(1536, 1, QLocale::DataSizeTraditionalFormat));
        }
    }
};

QTEST_GUILESS_MAIN(RegionalFormatTests)
#include "regional_format_tests.moc"
