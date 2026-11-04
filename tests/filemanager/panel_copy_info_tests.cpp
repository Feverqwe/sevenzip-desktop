#include "filemanager/FormatUtils.h"
#include "filemanager/panels/Panel.h"

#include <QDir>
#include <QLocale>
#include <QTest>
#include <limits>

class PanelCopyInfoTests : public QObject {
    Q_OBJECT

  private slots:
    void decimalFormatting_data() {
        QTest::addColumn<quint64>("value");
        QTest::addColumn<QString>("text");
        QTest::newRow("zero") << quint64(0) << QString("0");
        QTest::newRow("two digits") << quint64(12) << QString("12");
        QTest::newRow("three digits") << quint64(999) << QString("999");
        QTest::newRow("four digits") << quint64(1000) << QString("1 000");
        QTest::newRow("five digits") << quint64(12345) << QString("12 345");
        QTest::newRow("six digits") << quint64(123456) << QString("123 456");
        QTest::newRow("seven digits") << quint64(1234567) << QString("1 234 567");
        QTest::newRow("uint32 maximum") << quint64(0xFFFFFFFF) << QString("4 294 967 295");
        QTest::newRow("uint64 branch") << quint64(0x100000000) << QString("4 294 967 296");
        QTest::newRow("uint64 maximum")
            << std::numeric_limits<quint64>::max() << QString("18 446 744 073 709 551 615");
    }

    void decimalFormatting() {
        QFETCH(quint64, value);
        QFETCH(QString, text);
        QCOMPARE(ConvertSizeToString(value), text);
    }

    void mixedSelectionUsesPropertyTotalsAndRelativePaths() {
        CObjectVector<CPanelItem> items;
        items.Add({QStringLiteral("подпапка/папка"), true, true, 1234});
        items.Add({QStringLiteral("имя с пробелом.txt"), false, true, 5});
        items.Add({"empty", false, true, 0});
        CRecordVector<UInt32> indices;
        indices.Add(0);
        indices.Add(1);
        indices.Add(2);
        const CPanel panel(QStringLiteral("archive.7z / вложенная/"), items);
        QCOMPARE(panel.GetItemsInfoString(indices),
                 QStringLiteral("Folders: 1    ( %1 bytes )\n"
                                "Files: 2    ( 5 bytes )\n"
                                "Size: %2 bytes\n\n"
                                "archive.7z / вложенная/\n"
                                "  подпапка/папка")
                         .arg(QLocale().toString(1234), QLocale().toString(1239)) +
                     QDir::separator() + QStringLiteral("\n  имя с пробелом.txt\n  empty"));
        CRecordVector<UInt32> files;
        files.Add(2);
        files.Add(1);
        QCOMPARE(panel.GetItemsInfoString(files),
                 QStringLiteral("Files: 2    ( 5 bytes )\n\n"
                                "archive.7z / вложенная/\n  empty\n  имя с пробелом.txt"));
    }

    void undefinedSizeRemainsUndefinedAndZeroIsDefined() {
        CObjectVector<CPanelItem> items;
        items.Add({"unknown", true, false, 0});
        items.Add({"known", true, true, 9999});
        items.Add({"zero", false, true, 0});
        CRecordVector<UInt32> indices;
        indices.Add(0);
        indices.Add(1);
        indices.Add(2);
        const CPanel panel("/folder/", items);
        const QString text = panel.GetItemsInfoString(indices);
        QVERIFY(text.startsWith("Folders: 2\nFiles: 1    ( 0 bytes )\n\n"));
        QVERIFY(!text.contains(QLocale().toString(9999)));
        QVERIFY(!text.contains("Size:"));
        indices.Clear();
        indices.Add(1);
        indices.Add(0);
        QVERIFY(panel.GetItemsInfoString(indices).startsWith("Folders: 2\n\n"));
        indices.Clear();
        QCOMPARE(panel.GetItemsInfoString(indices), QString("\n/folder/"));
    }

    void fiveNamesThenEllipsisButCountsEntireSelection() {
        CObjectVector<CPanelItem> items;
        CRecordVector<UInt32> indices;
        for (UInt32 i = 0; i < 7; ++i) {
            items.Add({QString::number(i), false, true, 1000});
            indices.Add(i);
        }
        const CPanel panel("/folder/", items);
        QCOMPARE(panel.GetItemsInfoString(indices),
                 QString("Files: 7    ( %1 bytes )\n\n/folder/\n  0\n  1\n  2\n  3\n  4\n  ...")
                     .arg(QLocale().toString(7000)));
        indices.DeleteBack();
        indices.DeleteBack();
        const QString five = panel.GetItemsInfoString(indices);
        QVERIFY(five.startsWith(QString("Files: 5    ( %1 bytes )").arg(QLocale().toString(5000))));
        QVERIFY(five.endsWith("  4"));
        QVERIFY(!five.contains("..."));
    }
};

QTEST_MAIN(PanelCopyInfoTests)
#include "panel_copy_info_tests.moc"
