#include "7zip/PropID.h"
#include "archive/opening/OpenArchive.h"
#include "archive/diagnostics/archiveerrormessages.h"
#include "filemanager/PropertyName.h"

#include <QTest>

using NWindows::NCOM::CPropVariant;

static QString Message(UInt32 flags) {
    return QString::fromWCharArray(GetOpenArcErrorMessage(flags));
}

class ArchiveErrorPropertiesTests : public QObject {
    Q_OBJECT

  private slots:
    void absentVersusDefinedZeroAndWideValues() {
        CPropVariant prop;
        bool defined = true;
        QCOMPARE(GetOpenArcErrorFlags(prop, &defined), UInt32(0));
        QVERIFY(!defined);
        QCOMPARE(GetOpenArcErrorFlags(prop), UInt32(0));
        prop = UInt32(0);
        QCOMPARE(GetOpenArcErrorFlags(prop, &defined), UInt32(0));
        QVERIFY(defined);
        prop = UInt32(kpv_ErrorFlags_CrcError);
        QCOMPARE(GetOpenArcErrorFlags(prop), UInt32(kpv_ErrorFlags_CrcError));
        prop = UInt64(0x100000000ULL | kpv_ErrorFlags_DataError);
        QCOMPARE(GetOpenArcErrorFlags(prop, &defined), UInt32(kpv_ErrorFlags_DataError));
        QVERIFY(defined);
        prop = UInt64(0xFFFFFFFFFFFFFFFFULL);
        QCOMPARE(GetOpenArcErrorFlags(prop), UInt32(0xFFFFFFFF));
    }

    void incompatiblePropertyThrowsAndClearsDefined() {
        CPropVariant signedProp;
        signedProp.Set_Int64(-1);
        const CPropVariant props[] = {CPropVariant(true), CPropVariant(L"й error"), signedProp};
        for (const auto &prop : props) {
            bool defined = true;
            bool threw = false;
            try {
                GetOpenArcErrorFlags(prop, &defined);
            } catch (int) {
                threw = true;
            }
            QVERIFY(threw);
            QVERIFY(!defined);
        }
    }

    void messagesPreserveSourceOrderAndEncryptedHeaderHint() {
        QVERIFY(GetOpenArcErrorMessage(0).IsEmpty());
        QCOMPARE(Message(kpv_ErrorFlags_EncryptedHeadersError),
                 QStringLiteral("Headers Error : Wrong password?"));
        QCOMPARE(Message(kpv_ErrorFlags_CrcError | kpv_ErrorFlags_HeadersError),
                 QStringLiteral("Headers Error\nCRC failed"));
        QCOMPARE(
            Message(0x7FF),
            QStringLiteral(
                "Is not archive\nHeaders Error\n"
                "Headers Error : Wrong password?\n"
                "Unavailable start of archive\nUnconfirmed start of archive\n"
                "Unexpected end of data\nThere are some data after the end of the payload data\n"
                "Unsupported compression method\nUnsupported feature\n"
                "Data error\nCRC failed"));
    }

    void unknownBitsSurviveAfterLocalizedMessages() {
        QCOMPARE(Message(0x80000000), QString("0x80000000"));
        QCOMPARE(Message(0xA0000000 | kpv_ErrorFlags_DataError),
                 QStringLiteral("Data error\n0xA0000000"));
        QVERIFY(Message(0xFFFFFFFF).endsWith("CRC failed\n0xFFFFF800"));
        // Even though resource 3710 exists, external property ID 2710 must
        // bypass the builtin-resource path (the source cutoff is 1000).
        QCOMPARE(QString::fromWCharArray(GetNameOfProperty(2710, nullptr)), QString("2710"));
    }
};

QTEST_GUILESS_MAIN(ArchiveErrorPropertiesTests)
#include "archive_error_properties_tests.moc"
