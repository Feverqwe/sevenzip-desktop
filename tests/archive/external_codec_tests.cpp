#include "7zip/Common/StreamObjects.h"
#include "archive/backend/archivebackend.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/support/source_creation_fixture.h"
#ifdef Q_OS_WIN
#include "Windows/FileIO.h"
#endif
#include <QFile>
#include <QScopeGuard>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <array>
#include <future>

namespace {
#ifdef Q_OS_WIN
constexpr const char *kEngineName = "7z.exe";
constexpr const char *kModuleName = "7z.dll";
constexpr const char *kModuleSuffix = ".dll";
#else
constexpr const char *kEngineName = "7z";
constexpr const char *kModuleName = "7z.so";
constexpr const char *kModuleSuffix = ".so";
#endif
class CExtractToMemory final : public IArchiveExtractCallback, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_1(IArchiveExtractCallback)
  public:
    CMyComPtr2<ISequentialOutStream, CDynBufSeqOutStream> Stream;
    Int32 OperationResult = -1;
    CExtractToMemory() { Stream.Create_if_Empty(); }
    Z7_COM7F_IMF(SetTotal(UInt64)) override { return S_OK; }
    Z7_COM7F_IMF(SetCompleted(const UInt64 *)) override { return S_OK; }
    Z7_COM7F_IMF(GetStream(UInt32, ISequentialOutStream **stream, Int32)) override {
        *stream = Stream.Interface();
        (*stream)->AddRef();
        return S_OK;
    }
    Z7_COM7F_IMF(PrepareOperation(Int32)) override { return S_OK; }
    Z7_COM7F_IMF(SetOperationResult(Int32 result)) override {
        OperationResult = result;
        return S_OK;
    }
};
QByteArray read(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
} // namespace

class ExternalCodecTests final : public QObject {
    Q_OBJECT
    QTemporaryDir runtime_;
    QString engine_, module_;
    std::shared_ptr<const ArchiveCodecCatalog> catalog_;
  private slots:
    void initTestCase() {
        QVERIFY(runtime_.isValid());
        const QDir prepared(QString::fromUtf8(SEVENZIP_EXTERNAL_RUNTIME));
        if (!QFileInfo::exists(prepared.filePath(kEngineName)))
            QSKIP("Run scripts/build-codec-runtime.sh for the official external-codec engine");
        engine_ = runtime_.filePath(kEngineName);
        module_ = runtime_.filePath(kModuleName);
        QVERIFY(QFile::copy(prepared.filePath(kEngineName), engine_));
        QVERIFY(QFile::copy(prepared.filePath(kModuleName), module_));
        QVERIFY(QDir(runtime_.path()).mkdir("Codecs"));
        QVERIFY(QFile::copy(
            QString::fromUtf8(SEVENZIP_EXTERNAL_FIXTURE),
            runtime_.filePath(QStringLiteral("Codecs/кодек 空 with spaces") + kModuleSuffix)));
        ArchiveBackend backend(nullptr, engine_);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(module_));
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY2(ready[0][0].toBool(), qPrintable(ready[0][1].toString()));
        catalog_ = backend.codecCatalog();
        QCOMPARE(catalog_->nativeCodecs().Libs.Size(), 2u);
        QVERIFY(catalog_->nativeCodecs().NeedSetLibCodecs);
    }
    void realEngineAndLeasedNativeHandler_data() {
        QTest::addColumn<bool>("globalOnly");
        QTest::newRow("official-handler") << false;
        QTest::newRow("global-only-external-handler") << true;
    }
    void concurrentReadersRetainSharedCodecProviders() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QByteArray bytes("concurrent native readers\0payload", 33);
        const QString source = fixture.filePath(QStringLiteral("имя 空 with spaces.bin"));
        QFile input(source);
        QVERIFY(input.open(QIODevice::WriteOnly));
        QCOMPARE(input.write(bytes), qint64(bytes.size()));
        input.close();
        const QString path = fixture.filePath("shared.zip");
        QProcess creator;
        creator.start(engine_, {"a", "-tzip", "--", path, source});
        QVERIFY(creator.waitForFinished(10000));
        QCOMPARE(creator.exitCode(), 0);
        QSemaphore start;
        std::array<std::future<bool>, 2> readers;
        for (auto &reader : readers)
            reader = std::async(std::launch::async, [&, catalog = catalog_] {
                start.acquire();
                for (unsigned repeat = 0; repeat < 16; ++repeat) {
                    CArchiveLink archive;
                    if (catalog->openArchive(archive, path, "zip", nullptr) != S_OK)
                        return false;
                    CMyComPtr2_Create<IArchiveExtractCallback, CExtractToMemory> callback;
                    if (archive.GetArc()->Archive->Extract(nullptr, UInt32(-1), 0, callback) !=
                            S_OK ||
                        callback->OperationResult != NArchive::NExtract::NOperationResult::kOK ||
                        QByteArray(reinterpret_cast<const char *>(callback->Stream->GetBuffer()),
                                   int(callback->Stream->GetSize())) != bytes)
                        return false;
                }
                return true;
            });
        start.release(int(readers.size()));
        for (auto &reader : readers)
            QVERIFY(reader.get());
        // The same retained providers remain usable after both workers release
        // their readers, as when independent File Manager windows finish work.
        CArchiveLink finalReader;
        QCOMPARE(catalog_->openArchive(finalReader, path, "zip", nullptr), S_OK);
    }
    void realEngineAndLeasedNativeHandler() {
        QFETCH(bool, globalOnly);
        const QString facade = runtime_.filePath(QString("Formats/global-only") + kModuleSuffix);
        if (globalOnly) {
            QVERIFY(QDir(runtime_.path()).mkpath("Formats/resources/fixture"));
            QVERIFY(QFile::copy(QString::fromUtf8(SEVENZIP_EXTERNAL_FORMAT_FIXTURE), facade));
            QFile marker(runtime_.filePath("Formats/resources/fixture/marker.txt"));
            QVERIFY(marker.open(QIODevice::WriteOnly));
            QCOMPARE(marker.write("fixture resource"), qint64(16));
        }
        const auto remove = qScopeGuard([&] {
            if (globalOnly) {
                QFile::remove(facade);
                QFile::remove(runtime_.filePath("Formats/resources/fixture/marker.txt"));
                QDir(runtime_.path()).rmdir("Formats/resources/fixture");
                QDir(runtime_.path()).rmdir("Formats/resources");
            }
        });
        QTemporaryDir fixture;
        const QByteArray bytes("external coder\0Unicode fixture", 30);
        const QString source = fixture.filePath(QStringLiteral("имя 空 with spaces.bin"));
        QFile input(source);
        QVERIFY(input.open(QIODevice::WriteOnly));
        QCOMPARE(input.write(bytes), qint64(bytes.size()));
        input.close();
        const QString archive = fixture.filePath("external method.7z");
        ArchiveBackend backend(nullptr, engine_);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(module_));
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 15000);
        QVERIFY2(ready[0][0].toBool(), qPrintable(ready[0][1].toString()));
        const auto retained = backend.codecCatalog();
        const QString type = globalOnly ? "SevenzipFixture7z" : "7z";
        if (globalOnly) {
            QVERIFY(retained->formatIndex(type) >= 0);
            QVERIFY(!retained->canUpdate(type));
        }
        CompressionOptions options;
        options.format = "7z";
        options.Options = L"0=SevenzipFixtureCopy";
        QSignalSpy finished(&backend, &ArchiveBackend::finished);
        QCOMPARE(SourceCreationFixture::Create(backend, archive, {source}, {}, false, options),
                 S_OK);
        CArchiveLink leased;
        QCOMPARE(retained->openArchive(leased, archive, type, nullptr), S_OK);
        if (globalOnly) {
            CMyComPtr<ISetCompressCodecsInfo> perInstance;
            QCOMPARE(
                leased.GetArc()->Archive.QueryInterface(IID_ISetCompressCodecsInfo, &perInstance),
                E_NOINTERFACE);
        }
        // A second catalog can disappear without resetting the first handler's
        // external codecs. Each confirmed deployment owns its DLL images and
        // the source SetCodecs context, while this retained catalog owns them.
        ready.clear();
        QVERIFY(backend.discoverCodecCatalog(module_));
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY2(ready[0][0].toBool(), qPrintable(ready[0][1].toString()));
        QVERIFY(backend.codecCatalog() != retained);
        backend.loadCodecCatalog(module_, catalog_->version());
        const QString resource = runtime_.filePath("Formats/resources/fixture/marker.txt");
        if (globalOnly)
            QVERIFY(QFile::remove(resource));
        CArchiveLink resourceConsumer;
        if (globalOnly)
            QCOMPARE(retained->openArchive(resourceConsumer, archive, type, nullptr), S_OK);
        CMyComPtr2_Create<IArchiveExtractCallback, CExtractToMemory> callback;
        QCOMPARE(leased.GetArc()->Archive->Extract(nullptr, UInt32(-1), 0, callback), S_OK);
        QCOMPARE(callback->OperationResult, Int32(NArchive::NExtract::NOperationResult::kOK));
        QCOMPARE(QByteArray(reinterpret_cast<const char *>(callback->Stream->GetBuffer()),
                            int(callback->Stream->GetSize())),
                 bytes);
        if (globalOnly) {
            QFile marker(resource);
            QVERIFY(marker.open(QIODevice::WriteOnly));
            QCOMPARE(marker.write("fixture resource"), qint64(16));
        }

        ArchiveCodecCatalog plain(module_, catalog_->version());
        CArchiveLink unsupported;
        QCOMPARE(plain.openArchive(unsupported, archive, "7z", nullptr), S_OK);
        CMyComPtr2_Create<IArchiveExtractCallback, CExtractToMemory> missing;
        QCOMPARE(unsupported.GetArc()->Archive->Extract(nullptr, UInt32(-1), 0, missing), S_OK);
        QCOMPARE(missing->OperationResult,
                 Int32(NArchive::NExtract::NOperationResult::kUnsupportedMethod));

        // Restore the backend's deployment catalog after the
        // deliberate catalog replacement used to check the retained handler.
        ready.clear();
        QVERIFY(backend.discoverCodecCatalog(module_));
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY2(ready[0][0].toBool(), qPrintable(ready[0][1].toString()));
        QSignalSpy listed(&backend, &ArchiveBackend::entriesReady);
        backend.openFolder(archive, type);
        QTRY_COMPARE_WITH_TIMEOUT(listed.size(), 1, 10000);
        finished.clear();
        backend.test(archive);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
        QVERIFY2(finished[0][0].toBool(), qPrintable(finished[0][1].toString()));
        finished.clear();
        const QString output = fixture.filePath("output");
        backend.extract(archive, output);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
        QVERIFY2(finished[0][0].toBool(), qPrintable(finished[0][1].toString()));
        QCOMPARE(read(QDir(output).filePath(QFileInfo(source).fileName())), bytes);
        QCOMPARE(read(source), bytes);
    }
    void cyclicResourceDirectoryRefusesSnapshot() {
        const QString loop = runtime_.filePath("Codecs/resource-loop");
#ifdef Q_OS_WIN
        const auto target = QDir::toNativeSeparators(runtime_.filePath("Codecs")).toStdWString();
        const auto nativeLoop = QDir::toNativeSeparators(loop).toStdWString();
        CByteBuffer reparse;
        NWindows::NFile::FillLinkData_WinLink(reparse, target.c_str(), false);
        QVERIFY(NWindows::NFile::NIO::SetReparseData(nativeLoop.c_str(), true, reparse,
                                                     DWORD(reparse.Size())));
        QVERIFY(QFileInfo(loop).isDir());
        QVERIFY(!QDir(loop).entryList(QDir::Files).isEmpty());
        const auto remove = qScopeGuard([&] { QDir().rmdir(loop); });
#else
        QVERIFY(QFile::link(runtime_.filePath("Codecs"), loop));
        const auto remove = qScopeGuard([&] { QFile::remove(loop); });
#endif
        ArchiveBackend backend(nullptr, engine_);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(module_));
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY(!ready[0][0].toBool());
        QVERIFY(!ready[0][1].toString().isEmpty());
        QCOMPARE(catalog_->result(), S_OK);
#ifdef Q_OS_WIN
        CByteBuffer retainedReparse;
        QVERIFY(NWindows::NFile::NIO::GetReparseData(nativeLoop.c_str(), retainedReparse));
        QVERIFY(retainedReparse.Size() > 0);
#else
        QVERIFY(QFileInfo(loop).isSymLink());
#endif
    }
    void sourceDirectoryWarningKeepsWorkingModule() {
        QVERIFY(QDir(runtime_.path()).mkpath("Formats"));
        const QString unusableName = QStringLiteral("unusable module") + kModuleSuffix;
        const QString path = runtime_.filePath("Formats/" + unusableName);
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("not a shared library"), qint64(20));
        file.close();
        const auto remove = qScopeGuard([&] { QFile::remove(path); });
        ArchiveBackend backend(nullptr, engine_);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(module_));
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY2(ready[0][0].toBool(), qPrintable(ready[0][1].toString()));
#ifdef Q_OS_WIN
        // Original LoadDll deliberately ignores ERROR_BAD_EXE_FORMAT for datafile probes.
        QVERIFY(!ready[0][1].toString().contains(unusableName));
#else
        QVERIFY(ready[0][1].toString().contains(unusableName));
#endif
        QCOMPARE(backend.codecCatalog()->nativeCodecs().Libs.Size(), 2u);
        QVERIFY(backend.codecCatalog()->canUpdate("7z"));
        QCOMPARE(read(path), QByteArray("not a shared library"));
    }
    void copiedZipFactoryReadsMainOnlyDeployment() {
        QTemporaryDir runtime, fixture;
        const QString engine = runtime.filePath(kEngineName);
        const QString module = runtime.filePath(kModuleName);
        QVERIFY(QFile::copy(engine_, engine));
        QVERIFY(QFile::copy(module_, module));
        ArchiveBackend backend(nullptr, engine);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(module));
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY(ready[0][0].toBool());
        QCOMPARE(backend.codecCatalog()->nativeCodecs().Libs.Size(), 1u);
        const QByteArray bytes = QByteArray("decoder fixture").repeated(100);
        const QString input = fixture.filePath("source.txt");
        QFile file(input);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), qint64(bytes.size()));
        file.close();
        const QString archive = fixture.filePath("bzip2.zip");
        CompressionOptions options;
        options.format = "zip";
        options.Method = "BZip2";
        QSignalSpy finished(&backend, &ArchiveBackend::finished);
        QCOMPARE(SourceCreationFixture::Create(backend, archive, {input}, {}, false, options),
                 S_OK);
        CArchiveLink link;
        QCOMPARE(backend.codecCatalog()->openArchive(link, archive, "zip", nullptr), S_OK);
        CMyComPtr2_Create<IArchiveExtractCallback, CExtractToMemory> callback;
        QCOMPARE(link.GetArc()->Archive->Extract(nullptr, UInt32(-1), 0, callback), S_OK);
        QCOMPARE(callback->OperationResult, Int32(NArchive::NExtract::NOperationResult::kOK));
        QCOMPARE(QByteArray(reinterpret_cast<const char *>(callback->Stream->GetBuffer()),
                            int(callback->Stream->GetSize())),
                 bytes);
    }
    void externalFormatWithoutMainModule_data() {
        QTest::addColumn<bool>("console");
        QTest::newRow("with legacy console") << true;
        QTest::newRow("modules only") << false;
    }
    void externalFormatWithoutMainModule() {
        QFETCH(bool, console);
        QTemporaryDir deployment;
        QTemporaryDir fixture;
        QVERIFY(deployment.isValid());
        QVERIFY(fixture.isValid());
        const QString engine = deployment.filePath(kEngineName);
        const QString missingMain = deployment.filePath(kModuleName);
        if (console)
            QVERIFY(QFile::copy(engine_, engine));
        QVERIFY(QDir(deployment.path()).mkdir("Formats"));
        const QString formatModule = deployment.filePath(QString("Formats/only") + kModuleSuffix);
        QVERIFY(QFile::copy(module_, formatModule));
        ArchiveBackend backend(nullptr, engine);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog());
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY2(ready[0][0].toBool(), qPrintable(ready[0][1].toString()));
        const auto catalog = backend.codecCatalog();
        QCOMPARE(catalog->nativeCodecs().Libs.Size(), 1u);
        QVERIFY(!ready[0][1].toString().isEmpty());
        QVERIFY(catalog->canUpdate("7z"));
        const QByteArray bytes("external format without main module");
        const QString source = fixture.filePath(QStringLiteral("имя 空 with spaces.txt"));
        QFile input(source);
        QVERIFY(input.open(QIODevice::WriteOnly));
        QCOMPARE(input.write(bytes), qint64(bytes.size()));
        input.close();
        const QString archive = fixture.filePath("format.7z");
        CompressionOptions options;
        options.format = "7z";
        QSignalSpy finished(&backend, &ArchiveBackend::finished);
        if (console) {
            QCOMPARE(SourceCreationFixture::Create(backend, archive, {source}, {}, false, options),
                     S_OK);
        } else {
            QProcess creator;
            creator.start(engine_, {"a", "-t7z", "--", archive, source});
            QVERIFY(creator.waitForFinished(10000));
            QCOMPARE(creator.exitCode(), 0);
        }
        CArchiveLink native;
        QCOMPARE(catalog->openArchive(native, archive, "7z", nullptr), S_OK);
        CMyComPtr2_Create<IArchiveExtractCallback, CExtractToMemory> callback;
        QCOMPARE(native.GetArc()->Archive->Extract(nullptr, UInt32(-1), 0, callback), S_OK);
        QCOMPARE(callback->OperationResult, Int32(NArchive::NExtract::NOperationResult::kOK));
        QCOMPARE(QByteArray(reinterpret_cast<const char *>(callback->Stream->GetBuffer()),
                            int(callback->Stream->GetSize())),
                 bytes);
        {
            QSignalSpy listed(&backend, &ArchiveBackend::entriesReady);
            finished.clear();
            backend.list(archive);
            QTRY_COMPARE(finished.size(), 1);
            QVERIFY2(finished[0][0].toBool(), qPrintable(finished[0][1].toString()));
            const auto entries = qvariant_cast<QList<ArchiveEntry>>(listed[0][0]);
            QCOMPARE(entries.size(), 1);
            finished.clear();
            backend.testSourceArchive(archive, {});
            QTRY_COMPARE(finished.size(), 1);
            QVERIFY2(finished[0][0].toBool(), qPrintable(finished[0][1].toString()));
            const QString output = fixture.filePath("native output.bin");
            finished.clear();
            backend.extractSourceEntry(archive, entries[0].path, output, "7z", {});
            QTRY_COMPARE(finished.size(), 1);
            QVERIFY2(finished[0][0].toBool(), qPrintable(finished[0][1].toString()));
            QCOMPARE(read(output), bytes);
        }
        if (console) {
            finished.clear();
            const QString output = fixture.filePath("output");
            backend.extract(archive, output);
            QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
            QVERIFY(finished[0][0].toBool());
            QCOMPARE(read(QDir(output).filePath(QFileInfo(source).fileName())), bytes);
        }
        QCOMPARE(read(source), bytes);
        QVERIFY(QFileInfo::exists(formatModule));
        QVERIFY(!QFileInfo::exists(missingMain));
    }
    void explicitModuleSelectionRetainsPreviousLease() {
        const QString otherModule = runtime_.filePath(QString("different module") + kModuleSuffix);
        QVERIFY(QFile::copy(module_, otherModule));
        const auto remove = qScopeGuard([&] { QFile::remove(otherModule); });
        ArchiveBackend backend(nullptr, engine_);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(otherModule));
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY2(ready[0][0].toBool(), qPrintable(ready[0][1].toString()));
        QVERIFY(backend.codecCatalog()->canUpdate("zip"));
        QCOMPARE(backend.codecCatalog()->nativeCodecs().Libs.Size(), 2u);
        QCOMPARE(catalog_->result(), S_OK);
        QCOMPARE(catalog_->nativeCodecs().Libs.Size(), 2u);
    }
    void incompatibleModuleIsSkippedWithSourceError() {
        const QString path = runtime_.filePath(QString("Codecs/incompatible") + kModuleSuffix);
        QVERIFY(QFile::copy(QString::fromUtf8(SEVENZIP_INCOMPATIBLE_FIXTURE), path));
        const auto remove = qScopeGuard([&] { QFile::remove(path); });
        ArchiveBackend backend(nullptr, engine_);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(module_));
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY2(ready[0][0].toBool(), qPrintable(ready[0][1].toString()));
        QVERIFY(ready[0][1].toString().contains("not compatible"));
        QCOMPARE(backend.codecCatalog()->nativeCodecs().Libs.Size(), 2u);
        QVERIFY(QFileInfo::exists(path));
    }
    void discoveryCancellation() {
        ArchiveBackend backend(nullptr, engine_);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(module_));
        backend.cancelCodecDiscovery();
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY(!ready[0][0].toBool());
        QVERIFY(!backend.codecCatalog());
        QCOMPARE(catalog_->result(), S_OK);
    }
    void defaultBackendUsesSelectedRuntime_data() {
        QTest::addColumn<bool>("missing");
        QTest::newRow("selected") << false;
        QTest::newRow("missing") << true;
    }
    void defaultBackendUsesSelectedRuntime() {
        QFETCH(bool, missing);
        const QByteArray previous = qgetenv("SEVENZIP_CODEC_MODULE_PATH");
        const auto restore = qScopeGuard([&] {
            if (previous.isNull())
                qunsetenv("SEVENZIP_CODEC_MODULE_PATH");
            else
                qputenv("SEVENZIP_CODEC_MODULE_PATH", previous);
        });
        qputenv("SEVENZIP_CODEC_MODULE_PATH",
                (missing ? runtime_.filePath("missing module") : module_).toUtf8());
        ArchiveBackend backend;
        backend.deferCodecDiscovery();
        // A new window can be instantiated before the first codec demand.
        const std::unique_ptr<ArchiveBackend> independent(backend.createIndependentBackend());
        QString error;
        QCOMPARE(independent->waitForCodecCatalog(error), !missing);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog());
        QTRY_COMPARE(ready.size(), 1);
        QCOMPARE(ready[0][0].toBool(), !missing);
        if (!missing)
            QCOMPARE(backend.codecCatalog()->nativeCodecs().Libs.Size(), 2u);
    }
};
QTEST_MAIN(ExternalCodecTests)
#include "external_codec_tests.moc"
