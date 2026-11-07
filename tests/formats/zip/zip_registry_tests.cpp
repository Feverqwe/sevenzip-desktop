#include "archive/operations/ZipRegistry.h"
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

class ZipRegistryTests final : public QObject {
    Q_OBJECT
  private slots:
    void undefinedExtractionPreservesDefinedChoices() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("настройки 空.ini");
        {
            QSettings key(path, QSettings::IniFormat);
            for (const QString &name : {"SplitDest", "ElimDup", "Security", "ShowPassword"})
                key.setValue("Extraction/" + name, true);
            key.setValue("unrelated/value", "preserved");
        }
        NExtract::CInfo info;
        info.Load(path);
        for (CBoolPair *pair :
             {&info.SplitDest, &info.ElimDup, &info.NtSecurity, &info.ShowPassword}) {
            pair->Def = false;
            pair->Val = false;
        }
        info.Save(path);
        NExtract::CInfo restored;
        restored.Load(path);
        for (const CBoolPair *pair :
             {&restored.SplitDest, &restored.ElimDup, &restored.NtSecurity, &restored.ShowPassword})
            QVERIFY(pair->Def && pair->Val);
        restored.ElimDup.Def = restored.NtSecurity.Def = true;
        restored.ElimDup.Val = restored.NtSecurity.Val = false;
        restored.Save(path);
        info.Load(path);
        QVERIFY(info.ElimDup.Def && !info.ElimDup.Val);
        QVERIFY(info.NtSecurity.Def && !info.NtSecurity.Val);
        QSettings key(path, QSettings::IniFormat);
        QCOMPARE(key.value("unrelated/value").toString(), QString("preserved"));
    }
    void booleanRepresentations_data() {
        QTest::addColumn<QVariant>("stored");
        QTest::addColumn<bool>("defined");
        QTest::addColumn<bool>("value");
        QTest::newRow("malformed") << QVariant("invalid") << false << false;
        QTest::newRow("empty") << QVariant("") << false << false;
        QTest::newRow("overflow") << QVariant("4294967296") << false << false;
        QTest::newRow("qt-true") << QVariant(true) << true << true;
        QTest::newRow("qt-false") << QVariant(false) << true << false;
        QTest::newRow("dword-zero") << QVariant(0u) << true << false;
        QTest::newRow("dword-nonzero") << QVariant(17u) << true << true;
    }
    void booleanRepresentations() {
        QFETCH(QVariant, stored);
        QFETCH(bool, defined);
        QFETCH(bool, value);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("settings.ini");
        {
            QSettings key(path, QSettings::IniFormat);
            for (const QString &name :
                 {"Extraction/SplitDest", "Extraction/ElimDup", "Extraction/Security",
                  "Extraction/ShowPassword", "Compression/ShowPassword",
                  "Compression/EncryptHeaders", "Compression/Security"})
                key.setValue(name, stored);
        }
        NExtract::CInfo extract;
        extract.Load(path);
        QCOMPARE(extract.SplitDest.Def, defined);
        QCOMPARE(extract.SplitDest.Val, defined ? value : true);
        for (const CBoolPair *pair :
             {&extract.ElimDup, &extract.NtSecurity, &extract.ShowPassword}) {
            QCOMPARE(pair->Def, defined);
            QCOMPARE(pair->Val, defined && value);
        }
        QCOMPARE(NExtract::Read_ShowPassword(path), defined && value);
        NCompression::CInfo compress;
        compress.Load(path);
        QCOMPARE(compress.ShowPassword, defined && value);
        QCOMPARE(compress.EncryptHeaders, defined && value);
        QCOMPARE(compress.NtSecurity.Def, defined);
        QCOMPARE(compress.NtSecurity.Val, defined && value);
    }
    void outOfRangeDwordsKeepSourceDefaults() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("settings.ini");
        {
            QSettings key(path, QSettings::IniFormat);
            for (const QString &name :
                 {"Extraction/ExtractMode", "Extraction/OverwriteMode", "Extraction/MemLimit",
                  "Compression/Level", "Compression/Options/7z/Level", "Options/ContextMenu",
                  "Options/WriteZoneIdExtract", "Options/WorkDirType"})
                key.setValue(name, qulonglong(1) << 32);
        }
        NExtract::CInfo extract;
        extract.Load(path);
        QVERIFY(!extract.PathMode_Force && !extract.OverwriteMode_Force);
        QCOMPARE(extract.PathMode, NExtract::NPathMode::kCurPaths);
        QCOMPARE(extract.OverwriteMode, NExtract::NOverwriteMode::kAsk);
        QCOMPARE(NExtract::Read_LimitGB(path), UInt32(-1));
        NCompression::CInfo compress;
        compress.Load(path);
        QCOMPARE(compress.Level, 5u);
        QCOMPARE(compress.Formats.Size(), 1u);
        QCOMPARE(compress.Formats[0].Level, UInt32(-1));
        CContextMenuInfo context;
        context.Load(path);
        QVERIFY(!context.Flags_Def);
        QCOMPARE(context.Flags, UInt32(-1));
        QCOMPARE(context.WriteZone, UInt32(-1));
        NWorkDir::CInfo work;
        work.Load(path);
        QCOMPARE(work.Mode, NWorkDir::NMode::kSystem);
    }
    void extractionLimitDeletesSentinel() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("settings.ini");
        QCOMPARE(NExtract::Read_LimitGB(path), UInt32(-1));
        NExtract::Save_LimitGB(path, 5);
        QCOMPARE(NExtract::Read_LimitGB(path), 5u);
        NExtract::Save_LimitGB(path, UInt32(-1));
        QCOMPARE(NExtract::Read_LimitGB(path), UInt32(-1));
        QSettings key(path, QSettings::IniFormat);
        QVERIFY(!key.contains("Extraction/MemLimit"));
    }
    void compressionDefaultsAndDeletionPolicies_data() {
        QTest::addColumn<QVariant>("storedLevel");
        QTest::addColumn<UInt32>("expectedLevel");
        QTest::newRow("absent") << QVariant() << UInt32(5);
        QTest::newRow("invalid") << QVariant("invalid") << UInt32(5);
        QTest::newRow("explicit") << QVariant(7u) << UInt32(7);
    }
    void compressionDefaultsAndDeletionPolicies() {
        QFETCH(QVariant, storedLevel);
        QFETCH(UInt32, expectedLevel);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("settings.ini");
        {
            QSettings key(path, QSettings::IniFormat);
            if (storedLevel.isValid())
                key.setValue("Compression/Level", storedLevel);
            key.setValue("Compression/Security", true);
            key.setValue("Compression/Options/obsolete/Method", "obsolete");
            key.setValue("unrelated/value", 17);
        }
        NCompression::CInfo info;
        info.Load(path);
        info.NtSecurity.Def = false;
        info.Formats.Clear();
        NCompression::CFormatOptions format;
        format.FormatID = "7z";
        format.Level = 7;
        info.Formats.Add(format);
        info.Save(path);
        QSettings key(path, QSettings::IniFormat);
        QVERIFY(key.contains("Compression/Level"));
        QCOMPARE(key.value("Compression/Level").toUInt(), expectedLevel);
        QVERIFY(!key.contains("Compression/Security"));
        QVERIFY(!key.contains("Compression/Options/obsolete/Method"));
        for (const QString &name : {"Method", "Options", "EncryptionMethod", "MemUse32", "MemUse64",
                                    "Dictionary", "NumThreads", "MTime"})
            QVERIFY2(!key.contains("Compression/Options/7z/" + name), qPrintable(name));
        QCOMPARE(key.value("Compression/Options/7z/Level").toUInt(), 7u);
        QCOMPARE(key.value("unrelated/value").toInt(), 17);
    }
};
QTEST_GUILESS_MAIN(ZipRegistryTests)
#include "zip_registry_tests.moc"
