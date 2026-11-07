#include "archive/backend/archivebackend.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "benchmark/Bench.h"
#include "localization/LangUtils.h"
#include "settings/RegistryUtils.h"
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

class BenchResults final : public IBenchCallback {
  public:
    unsigned EncCalls = 0, DecCalls = 0;
    bool Cancel = false;
    CBenchInfo Enc, Dec;
    HRESULT SetEncodeResult(const CBenchInfo &info, bool final) override {
        ++EncCalls;
        if (final)
            Enc = info;
        return Cancel ? E_ABORT : S_OK;
    }
    HRESULT SetDecodeResult(const CBenchInfo &info, bool final) override {
        ++DecCalls;
        if (final)
            Dec = info;
        return Cancel ? E_ABORT : S_OK;
    }
};

class NativeBenchmarkTests final : public QObject {
    Q_OBJECT
    ArchiveBackend backend_{nullptr, QStandardPaths::findExecutable("7z")};
    static CObjectVector<CProperty> boundedProperties() {
        CObjectVector<CProperty> props;
        for (const auto &pair : {qMakePair(L"mt", L"1"), qMakePair(L"d18", L""),
                                 qMakePair(L"timems", L"50"), qMakePair(L"tic", L"1")}) {
            CProperty &prop = props.AddNew();
            prop.Name = pair.first;
            prop.Value = pair.second;
        }
        return props;
    }

  private slots:
    void initTestCase() {
#ifndef SEVENZIP_CODEC_MODULE
        QSKIP("Official codec module unavailable; native benchmark remains unverified");
#else
        QSignalSpy ready(&backend_, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend_.discoverCodecCatalog(QString::fromUtf8(SEVENZIP_CODEC_MODULE)));
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY2(ready[0][0].toBool(), qPrintable(ready[0][1].toString()));
#endif
    }
    void sourceCallbacksExposeActualEngineResults_data() {
        QTest::addColumn<QString>("language");
        QTest::newRow("en") << QString("-");
        QTest::newRow("ru") << QString("ru");
    }
    void sourceCallbacksExposeActualEngineResults() {
        QFETCH(QString, language);
        QTemporaryDir languageDirectory;
        QVERIFY(languageDirectory.isValid());
        const QString settings = languageDirectory.filePath("language.ini");
        const auto restore = qScopeGuard([&] {
            SaveRegLang(UString("-"), settings);
            ReloadLang(settings);
        });
        SaveRegLang(UString(language.toStdWString().c_str()), settings);
        ReloadLang(settings);
        class Report final : public IBenchPrintCallback {
          public:
            QString Text;
            void Print(const char *text) override { Text += QString::fromUtf8(text); }
            void NewLine() override { Text += '\n'; }
            HRESULT CheckBreak() override { return S_OK; }
        } report;
        const auto catalog = backend_.codecCatalog();
        CExternalCodecs providers;
        providers.GetCodecs =
            static_cast<ICompressCodecsInfo *>(&const_cast<CCodecs &>(catalog->nativeCodecs()));
        QCOMPARE(providers.Load(), S_OK);
        BenchResults results;
        QCOMPARE(Bench(&providers, &report, &results, boundedProperties(), 1, false), S_OK);
        const QString heading = QString::fromUtf8(language == "ru" ? "1 поток: частота ЦП (МГц):"
                                                                   : "1T CPU Freq (MHz):");
        QVERIFY2(report.Text.contains(heading), qPrintable(report.Text));
        QVERIFY2(!report.Text.contains(QChar::ReplacementCharacter), qPrintable(report.Text));
        QVERIFY(results.EncCalls > 0);
        QVERIFY(results.DecCalls > 0);
        QVERIFY(results.Enc.Get_UnpackSize_Full() > 0);
        QVERIFY(results.Dec.Get_UnpackSize_Full() > 0);
        QVERIFY(results.Enc.GetUnpackSizeSpeed() > 0);
        QVERIFY(results.Dec.GetUnpackSizeSpeed() > 0);
        QVERIFY(GetBenchMemoryUsage(1, 5, UInt64(1) << 18, false) < (UInt64(16) << 20));
    }
    void zeroStartingThreadsRejectsBeforeBenchmarkWork() {
        const auto catalog = backend_.codecCatalog();
        CExternalCodecs providers;
        providers.GetCodecs =
            static_cast<ICompressCodecsInfo *>(&const_cast<CCodecs &>(catalog->nativeCodecs()));
        QCOMPARE(providers.Load(), S_OK);
        auto props = boundedProperties();
        auto &method = props.AddNew();
        method.Name = "m";
        method.Value = "CRC32";
        auto &start = props.AddNew();
        start.Name = "mts";
        start.Value = "0";
        class StopBeforeWork final : public IBenchPrintCallback {
          public:
            void Print(const char *) override {}
            void NewLine() override {}
            // Bound a regression run on broken code before CPU calibration or
            // the non-progressing thread-count loop can consume resources.
            HRESULT CheckBreak() override { return E_ABORT; }
        } report;
        QCOMPARE(Bench(&providers, &report, nullptr, props, 1, false), E_INVALIDARG);
        start.Value = "1";
        QCOMPARE(Bench(&providers, &report, nullptr, props, 1, false), E_ABORT);
    }
    void cancelStopsOriginalEngineCallback() {
        const auto catalog = backend_.codecCatalog();
        CExternalCodecs providers;
        providers.GetCodecs =
            static_cast<ICompressCodecsInfo *>(&const_cast<CCodecs &>(catalog->nativeCodecs()));
        QCOMPARE(providers.Load(), S_OK);
        BenchResults results;
        results.Cancel = true;
        QCOMPARE(Bench(&providers, nullptr, &results, boundedProperties(), 1, false), E_ABORT);
        QVERIFY(results.EncCalls > 0);
    }
};
QTEST_MAIN(NativeBenchmarkTests)
#include "native_benchmark_tests.moc"
