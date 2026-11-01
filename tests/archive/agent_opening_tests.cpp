#include "7zip/Common/StreamObjects.h"
#include "7zip/PropID.h"
#include "Windows/PropVariant.h"
#include "archive/agent/Agent.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/resources/qtarchivefileresource.h"
#include "archive/resources/qtarchivefoldermanager.h"
#include "archive/support/archive_backend_test_support.h"
#include "archive/support/native_archive_fixture.h"
#include <QScopeGuard>
#include <QStandardPaths>
#include <atomic>
#include <functional>

namespace {
class CBindingProgress final : public IProgress, public IArchiveOpenCallback, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_2(IProgress, IArchiveOpenCallback)

  public:
    bool CancelBinding = false;
    bool CancelWaiting = false;
    std::atomic<unsigned> BindingTotals{0};
    std::atomic<unsigned> BindingCompleted{0};
    Z7_COM7F_IMF(SetTotal(UInt64)) override { return S_OK; }
    Z7_COM7F_IMF(SetCompleted(const UInt64 *)) override { return CancelWaiting ? E_ABORT : S_OK; }
    Z7_COM7F_IMF(SetTotal(const UInt64 *files, const UInt64 *)) override {
        if (files)
            ++BindingTotals;
        return CancelBinding ? E_ABORT : S_OK;
    }
    Z7_COM7F_IMF(SetCompleted(const UInt64 *files, const UInt64 *)) override {
        if (files)
            ++BindingCompleted;
        return CancelBinding ? E_ABORT : S_OK;
    }
};

class COpeningBackend final : public ArchiveBackend {
  public:
    unsigned Listings = 0;
    explicit COpeningBackend(const QString &engine) : ArchiveBackend(nullptr, engine) {}
    void openFolder(const QString &path, const QString &format, const QString &password) override {
        ++Listings;
        ArchiveBackend::openFolder(path, format, password);
    }
};

class CListingBackend final : public ArchiveBackend {
  public:
    int Failure = 0;
    QString Name;
    void openFolder(const QString &, const QString &, const QString &) override {
        if (Failure == 2)
            throw 1;
        ArchiveInfo info;
        info.format = "zip";
        ArchiveEntry entry;
        entry.path = Name;
        entry.sizeValue = 7;
        emit infoReady(info);
        emit entriesReady({entry});
        emit finished(Failure == 0, {});
        // A later publication must not change the completed opening's snapshot.
        entry.path = "unrelated later listing";
        emit entriesReady({entry});
    }
};

class CExtractObserver final : public IFolderArchiveExtractCallback, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_1(IFolderArchiveExtractCallback)

  public:
    bool Cancel = false;
    unsigned Operations = 0;
    unsigned Errors = 0;
    QString Error;
    Z7_COM7F_IMF(SetTotal(UInt64)) override { return Cancel ? E_ABORT : S_OK; }
    Z7_COM7F_IMF(SetCompleted(const UInt64 *)) override { return Cancel ? E_ABORT : S_OK; }
    Z7_COM7F_IMF(AskOverwrite(const wchar_t *, const FILETIME *, const UInt64 *, const wchar_t *,
                              const FILETIME *, const UInt64 *, Int32 *answer))
    override {
        *answer = NOverwriteAnswer::kNo;
        return S_OK;
    }
    Z7_COM7F_IMF(PrepareOperation(const wchar_t *, Int32, Int32, const UInt64 *)) override {
        ++Operations;
        return Cancel ? E_ABORT : S_OK;
    }
    Z7_COM7F_IMF(MessageError(const wchar_t *message)) override {
        ++Errors;
        Error = QString::fromWCharArray(message);
        return S_OK;
    }
    Z7_COM7F_IMF(SetOperationResult(Int32 result, Int32)) override {
        if (result)
            ++Errors;
        if (result)
            Error = QStringLiteral("operation result %1").arg(result);
        return S_OK;
    }
};
} // namespace

class AgentOpeningTests final : public QObject, protected ArchiveBackendTestSupport {
    Q_OBJECT
    QString engine_;

    void createArchive(const QString &path, const QString &input) {
        QProcess process;
        process.start(engine_, {"a", "-tzip", "--", path, input});
        QVERIFY(process.waitForFinished(10000));
        QCOMPARE(process.exitCode(), 0);
    }
    void loadNative(ArchiveBackend &backend) {
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog());
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY2(ready[0][0].toBool(), "Matching official codec module is required");
    }
    HRESULT run(ArchiveBackend &backend, const std::function<HRESULT()> &operation) {
        std::atomic<bool> finished{false};
        HRESULT result = E_FAIL;
        auto *worker = QThread::create([&] {
            result = operation();
            finished.store(true);
        });
        const auto join = qScopeGuard([&] {
            backend.cancelAndWait();
            worker->wait();
            delete worker;
        });
        worker->start();
        QElapsedTimer timer;
        timer.start();
        while (!finished.load() && timer.elapsed() < 30000) {
            QCoreApplication::processEvents();
            QTest::qWait(1);
        }
        if (!finished.load()) {
            QTest::qFail("Source opening worker did not finish", __FILE__, __LINE__);
            return E_ABORT;
        }
        return result;
    }
    HRESULT open(ArchiveBackend &backend, IFolderManager *manager, const QString &path,
                 const wchar_t *format, IFolderFolder **folder, IProgress *progress = nullptr,
                 IInStream *stream = nullptr) {
        return run(backend, [&] {
            const std::wstring name = path.toStdWString();
            return manager->OpenFolderFile(stream, name.c_str(), format, folder, progress);
        });
    }

  private slots:
    void metadataOpeningFailureAndReopen_data() {
        QTest::addColumn<int>("failure");
        QTest::newRow("failed listing") << 1;
        QTest::newRow("throwing start") << 2;
    }
    void metadataOpeningFailureAndReopen() {
        QFETCH(int, failure);
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        CListingBackend backend;
        // Exercise the typed metadata service without a retained native reader.
        QCOMPARE(backend.loadCodecCatalog(temporary.filePath("absent module"), 1), E_FAIL);
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        CMyComPtr<IFolderFolder> folder;
        const QString path = temporary.filePath(QStringLiteral("архив 空.zip"));
        writeFile(path, "typed metadata fixture input");
        backend.Failure = failure;
        QCOMPARE(open(backend, manager, path, L"zip", &folder), E_FAIL);
        QVERIFY(!folder);
        backend.Failure = 0;
        backend.Name = QStringLiteral("первый 空.txt");
        QCOMPARE(open(backend, manager, path, L"zip", &folder), S_OK);
        QCOMPARE(folder->LoadItems(), S_OK);
        UInt32 count = 0;
        QCOMPARE(folder->GetNumberOfItems(&count), S_OK);
        QCOMPARE(count, UInt32(1));
        NWindows::NCOM::CPropVariant name;
        QCOMPARE(folder->GetProperty(0, kpidName, &name), S_OK);
        QCOMPARE(QString::fromWCharArray(name.bstrVal), backend.Name);
        backend.Name = QStringLiteral("обновлённый файл.txt");
        auto *agent = dynamic_cast<CAgentFolder *>(folder.Interface());
        QVERIFY(agent);
        CMyComPtr<IFolderFolder> reopened;
        QCOMPARE(run(backend, [&] { return agent->ReOpen(nullptr, &reopened); }), S_OK);
        QVERIFY(reopened);
        QCOMPARE(reopened->LoadItems(), S_OK);
        QCOMPARE(reopened->GetNumberOfItems(&count), S_OK);
        QCOMPARE(count, UInt32(1));
        QCOMPARE(reopened->GetProperty(0, kpidName, &name), S_OK);
        QCOMPARE(QString::fromWCharArray(name.bstrVal), backend.Name);
    }

    void readCancellationJoinsInitialCodecLoading_data() {
        QTest::addColumn<bool>("destroy");
        QTest::newRow("cancel and retry") << false;
        QTest::newRow("destroy") << true;
    }
    void readCancellationJoinsInitialCodecLoading() {
        QFETCH(bool, destroy);
        QTemporaryDir deployment;
        QTemporaryDir files;
        const auto catalog = NativeArchiveFixture::Catalog();
        QCOMPARE(catalog->result(), S_OK);
        const QString original =
            QString::fromWCharArray(fs2us(catalog->nativeCodecs().Libs[0].Path));
        const QString module = deployment.filePath(QFileInfo(original).fileName());
        QVERIFY(QFile::copy(QString::fromUtf8(SLOW_CODEC_FIXTURE), module));
        const QString entered = files.filePath(QStringLiteral("начало 空"));
        const QString finished = files.filePath(QStringLiteral("конец 空"));
        const QByteArray oldDelay = qgetenv("SEVENZIP_SLOW_CODEC_DELAY");
        const QByteArray oldEntered = qgetenv("SEVENZIP_SLOW_CODEC_ENTERED");
        const QByteArray oldFinished = qgetenv("SEVENZIP_SLOW_CODEC_FINISHED");
        const auto restore = qScopeGuard([&] {
            for (const auto &pair : {qMakePair("SEVENZIP_SLOW_CODEC_DELAY", oldDelay),
                                     qMakePair("SEVENZIP_SLOW_CODEC_ENTERED", oldEntered),
                                     qMakePair("SEVENZIP_SLOW_CODEC_FINISHED", oldFinished)}) {
                if (pair.second.isNull())
                    qunsetenv(pair.first);
                else
                    qputenv(pair.first, pair.second);
            }
        });
        qputenv("SEVENZIP_SLOW_CODEC_DELAY", "600");
        qputenv("SEVENZIP_SLOW_CODEC_ENTERED", entered.toUtf8().toBase64());
        qputenv("SEVENZIP_SLOW_CODEC_FINISHED", finished.toUtf8().toBase64());
        const QString archive = files.filePath("input.zip");
        QVERIFY(NativeArchiveFixture::Write(archive, {{QStringLiteral("имя 空.txt")}}, "zip"));
        auto backend =
            std::make_unique<ArchiveBackend>(nullptr, deployment.filePath("absent console"));
        QSignalSpy done(backend.get(), &ArchiveBackend::finished);
        QSignalSpy entries(backend.get(), &ArchiveBackend::entriesReady);
        backend->list(archive);
        QTRY_VERIFY(QFileInfo::exists(entered));
        QVERIFY(!QFileInfo::exists(finished));
        QVERIFY(backend->busy());
        if (destroy) {
            backend.reset();
            QVERIFY(done.isEmpty());
        } else {
            backend->cancelAndWait();
            QCOMPARE(done.size(), 1);
            QCOMPARE(backend->lastResult(), E_ABORT);
            QVERIFY(entries.isEmpty());
            QVERIFY(!backend->codecCatalog());
        }
        QVERIFY(QFileInfo::exists(finished));
        if (!destroy) {
            QVERIFY(QFile::remove(module));
            QVERIFY(QFile::copy(original, module));
            done.clear();
            backend->list(archive);
            QTRY_COMPARE(done.size(), 1);
            QVERIFY2(done[0][0].toBool(), qPrintable(done[0][1].toString()));
            QCOMPARE(entries.size(), 1);
            QCOMPARE(qvariant_cast<QList<ArchiveEntry>>(entries[0][0]).first().path,
                     QStringLiteral("имя 空.txt"));
        }
    }
    void initTestCase() {
        engine_ = QStandardPaths::findExecutable("7z");
        QVERIFY2(!engine_.isEmpty(), "Official fixture engine is required");
    }
    void openingWaitsForPendingCodecDiscovery_data() {
        QTest::addColumn<bool>("cancel");
        QTest::newRow("open") << false;
        QTest::newRow("cancel") << true;
    }
    void openingWaitsForPendingCodecDiscovery() {
        QFETCH(bool, cancel);
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString input = temporary.filePath(QStringLiteral("файл 空.bin"));
        const QString path = temporary.filePath(QStringLiteral("архив 空.pkg"));
        writeFile(input, "startup payload");
        createArchive(path, input);
        const QByteArray oldDelay = qgetenv("SEVENZIP_SLOW_CODEC_DELAY");
        qputenv("SEVENZIP_SLOW_CODEC_DELAY", "500");
        const auto restore = qScopeGuard([&] {
            if (oldDelay.isNull())
                qunsetenv("SEVENZIP_SLOW_CODEC_DELAY");
            else
                qputenv("SEVENZIP_SLOW_CODEC_DELAY", oldDelay);
        });
        COpeningBackend backend(engine_);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        const auto catalog = NativeArchiveFixture::Catalog();
        QCOMPARE(catalog->result(), S_OK);
        const QString original =
            QString::fromWCharArray(fs2us(catalog->nativeCodecs().Libs[0].Path));
        const QString module = temporary.filePath(QFileInfo(original).fileName());
        QVERIFY(QFile::copy(original, module));
        QVERIFY(QDir(temporary.path()).mkdir("Codecs"));
        const QString slow = temporary.filePath(
            "Codecs/" + QFileInfo(QString::fromUtf8(SLOW_CODEC_FIXTURE)).fileName());
        QVERIFY(QFile::copy(QString::fromUtf8(SLOW_CODEC_FIXTURE), slow));
        QVERIFY(backend.discoverCodecCatalog(module));
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        CMyComPtr<IFolderFolder> folder;
        auto *observer = new CBindingProgress;
        CMyComPtr<IProgress> progress = observer;
        observer->CancelWaiting = cancel;
        QCOMPARE(open(backend, manager, path, nullptr, &folder, progress), cancel ? E_ABORT : S_OK);
        QTRY_COMPARE(ready.size(), 1);
        if (cancel) {
            QVERIFY(!folder);
            QCOMPARE(backend.Listings, 0u);
            observer->CancelWaiting = false;
            QCOMPARE(open(backend, manager, path, nullptr, &folder, progress), S_OK);
        }
        QCOMPARE(ready.size(), 1);
        QVERIFY(folder);
        QCOMPARE(folder->LoadItems(), S_OK);
        UInt32 count = 0;
        QCOMPARE(folder->GetNumberOfItems(&count), S_OK);
        QCOMPARE(count, UInt32(1));
        QCOMPARE(backend.Listings, 0u);
    }
    void proxyProgressAndCancellation_data() {
        QTest::addColumn<bool>("cancel");
        QTest::newRow("success") << false;
        QTest::newRow("cancel-binding") << true;
    }
    void proxyProgressAndCancellation() {
        QFETCH(bool, cancel);
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString input = temporary.filePath(QStringLiteral("файл 空 with spaces.txt"));
        const QString path = temporary.filePath(QStringLiteral("архив 空.zip"));
        writeFile(input, QByteArray("protected\0payload", 17));
        createArchive(path, input);
        COpeningBackend backend(engine_);
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        auto *progressSpec = new CBindingProgress;
        CMyComPtr<IProgress> progress = progressSpec;
        progressSpec->CancelBinding = cancel;
        CMyComPtr<IFolderFolder> folder;
        QCOMPARE(open(backend, manager, path, nullptr, &folder, progress), cancel ? E_ABORT : S_OK);
        QCOMPARE(backend.Listings, 0u);
        QVERIFY(progressSpec->BindingTotals.load() > 0);
        QCOMPARE(bool(folder), !cancel);
        if (!cancel) {
            QVERIFY(progressSpec->BindingCompleted.load() > 0);
            QCOMPARE(folder->LoadItems(), S_OK);
            UInt32 count = 0;
            QCOMPARE(folder->GetNumberOfItems(&count), S_OK);
            QCOMPARE(count, UInt32(1));
            auto *source = dynamic_cast<CAgentFolder *>(folder.Interface());
            QVERIFY(source);
            CMyComPtr<IFolderFolder> reopened;
            QCOMPARE(run(backend, [&] { return source->ReOpen(nullptr, &reopened); }), S_OK);
            QVERIFY(reopened);
            QCOMPARE(reopened->LoadItems(), S_OK);
            QCOMPARE(reopened->GetNumberOfItems(&count), S_OK);
            QCOMPARE(count, UInt32(1));
        }
        QFile original(input);
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), QByteArray("protected\0payload", 17));
    }
    void invalidTypeStopsBeforeListing() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString input = temporary.filePath("file.txt");
        const QString path = temporary.filePath("archive.zip");
        writeFile(input, "known");
        createArchive(path, input);
        COpeningBackend backend(engine_);
        loadNative(backend);
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        CMyComPtr<IFolderFolder> folder;
        QCOMPARE(open(backend, manager, path, L"unknown-source-format", &folder), S_FALSE);
        QVERIFY(!folder);
        QCOMPARE(backend.Listings, 0u);
    }
    void nonOpenWarningRetainsDiagnosticFolder() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString path = temporary.filePath(QStringLiteral("сломанный 空.zip"));
        // Recognized ZIP local header with an incomplete fixed header.
        writeFile(path, QByteArray::fromHex("504b030414000000"));
        COpeningBackend backend(engine_);
        loadNative(backend);
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        CMyComPtr<IFolderFolder> folder;
        QCOMPARE(open(backend, manager, path, L"zip", &folder), S_FALSE);
        QVERIFY(folder);
        QCOMPARE(backend.Listings, 0u);
        CMyComPtr<IFolderArcProps> properties;
        CMyComPtr<IGetFolderArcProps> getter;
        QCOMPARE(folder.QueryInterface(IID_IGetFolderArcProps, &getter), S_OK);
        QCOMPARE(getter->GetFolderArcProps(&properties), S_OK);
        UInt32 levels = 99;
        QCOMPARE(properties->GetArcNumLevels(&levels), S_OK);
        QCOMPARE(levels, UInt32(0));
        NWindows::NCOM::CPropVariant value;
        QCOMPARE(properties->GetArcProp(0, kpidErrorFlags, &value), S_OK);
        QCOMPARE(value.vt, VARTYPE(VT_UI4));
        QVERIFY(value.ulVal != 0);
    }
    void streamExtractsOriginalBytesAfterManagerAndCallerRelease() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString input = temporary.filePath(QStringLiteral("файл 空.txt"));
        const QString path = temporary.filePath("archive.zip");
        writeFile(input, "known");
        createArchive(path, input);
        QFile archive(path);
        QVERIFY(archive.open(QIODevice::ReadOnly));
        const QByteArray bytes = archive.readAll();
        auto *buffer = new CBufferInStream;
        CMyComPtr<IInStream> stream = buffer;
        buffer->Buf.CopyFrom(reinterpret_cast<const Byte *>(bytes.constData()), bytes.size());
        buffer->Init();
        COpeningBackend backend(engine_);
        loadNative(backend);
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        CMyComPtr<IFolderFolder> folder;
        QCOMPARE(open(backend, manager, temporary.filePath("virtual missing.zip"), L"zip", &folder,
                      nullptr, stream),
                 S_OK);
        manager.Release();
        stream.Release();
        QCOMPARE(folder->LoadItems(), S_OK);
        UInt32 count = 0;
        QCOMPARE(folder->GetNumberOfItems(&count), S_OK);
        QCOMPARE(count, UInt32(1));
        CMyComPtr<IGetFolderArcProps> getter;
        QCOMPARE(folder.QueryInterface(IID_IGetFolderArcProps, &getter), S_OK);
        CMyComPtr<IFolderArcProps> properties;
        QCOMPARE(getter->GetFolderArcProps(&properties), S_OK);
        CMyComPtr<IInFolderArchive> retained;
        QCOMPARE(properties.QueryInterface(IID_IInFolderArchive, &retained), S_OK);
        auto *observer = new CExtractObserver;
        CMyComPtr<IFolderArchiveExtractCallback> callback = observer;
        const QString destination = temporary.filePath("output");
        QVERIFY(QDir().mkpath(destination));
        const auto prefix = (destination + '/').toStdWString();
        QCOMPARE(run(backend,
                     [&] {
                         return retained->Extract(NExtract::NPathMode::kFullPaths,
                                                  NExtract::NOverwriteMode::kOverwrite,
                                                  prefix.c_str(), false, callback);
                     }),
                 S_OK);
        QVERIFY2(observer->Errors == 0, qPrintable(observer->Error));
        QFile extracted(QDir(destination).filePath(QFileInfo(input).fileName()));
        QVERIFY(extracted.open(QIODevice::ReadOnly));
        QCOMPARE(extracted.readAll(), QByteArray("known"));
        folder.Release();
    }
    void agentInterfaceExtract_data() {
        QTest::addColumn<bool>("testMode");
        QTest::addColumn<bool>("cancel");
        QTest::addColumn<bool>("replaceCatalog");
        QTest::addColumn<bool>("missingDestination");
        QTest::newRow("extract") << false << false << false << false;
        QTest::newRow("test") << true << false << false << false;
        QTest::newRow("cancel") << false << true << false << false;
        QTest::newRow("retained-catalog") << false << false << true << false;
        QTest::newRow("missing-destination") << false << false << false << true;
    }
    void agentInterfaceExtract() {
        QFETCH(bool, testMode);
        QFETCH(bool, cancel);
        QFETCH(bool, replaceCatalog);
        QFETCH(bool, missingDestination);
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString name = QStringLiteral("файл 空 with spaces.bin");
        const QString input = temporary.filePath(name);
        const QString path = temporary.filePath("archive.zip");
        const QByteArray payload("source\0binary", 13);
        writeFile(input, payload);
        createArchive(path, input);
        COpeningBackend backend(engine_);
        loadNative(backend);
        const quint32 version = backend.codecCatalog()->version();
        std::weak_ptr<const ArchiveCodecCatalog> originalCatalog = backend.codecCatalog();
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        CMyComPtr<IFolderFolder> folder;
        QCOMPARE(open(backend, manager, path, L"zip", &folder), S_OK);
        if (replaceCatalog) {
            QCOMPARE(backend.loadCodecCatalog(temporary.filePath("absent-codec-module"), version),
                     E_FAIL);
            QVERIFY(backend.codecCatalog()->result() != S_OK);
            QVERIFY(!originalCatalog.expired());
        }
        CMyComPtr<IGetFolderArcProps> getter;
        QCOMPARE(folder.QueryInterface(IID_IGetFolderArcProps, &getter), S_OK);
        CMyComPtr<IFolderArcProps> properties;
        QCOMPARE(getter->GetFolderArcProps(&properties), S_OK);
        CMyComPtr<IInFolderArchive> archive;
        QCOMPARE(properties.QueryInterface(IID_IInFolderArchive, &archive), S_OK);
        UInt32 count = 0;
        QCOMPARE(archive->GetNumberOfProperties(&count), S_OK);
        QVERIFY(count > 0);
        for (UInt32 i = 0; i < count; ++i) {
            CMyComBSTR propertyName;
            PROPID id;
            VARTYPE type;
            QCOMPARE(archive->GetPropertyInfo(i, &propertyName, &id, &type), S_OK);
            QVERIFY(id != kpidPath);
        }
        auto *observer = new CExtractObserver;
        CMyComPtr<IFolderArchiveExtractCallback> callback = observer;
        observer->Cancel = cancel;
        const QString destination = temporary.filePath(QStringLiteral("выход 空"));
        // IInFolderArchive::Extract uses the caller's existing output root;
        // directory creation belongs to the command owner above this interface.
        if (!missingDestination)
            QVERIFY(QDir().mkpath(destination));
        const std::wstring prefix = (destination + '/').toStdWString();
        QCOMPARE(run(backend,
                     [&] {
                         return archive->Extract(NExtract::NPathMode::kFullPaths,
                                                 NExtract::NOverwriteMode::kOverwrite,
                                                 prefix.c_str(), testMode, callback);
                     }),
                 cancel ? E_ABORT : S_OK);
        if (missingDestination) {
            QCOMPARE(observer->Errors, 1u);
            QVERIFY(!observer->Error.isEmpty());
        } else
            QVERIFY2(observer->Errors == 0, qPrintable(observer->Error));
        if (missingDestination)
            QCOMPARE(observer->Operations, 0u);
        else if (!cancel)
            QVERIFY(observer->Operations > 0);
        QFile extracted(QDir(destination).filePath(name));
        if (!testMode && !cancel && !missingDestination) {
            QVERIFY(extracted.open(QIODevice::ReadOnly));
            QCOMPARE(extracted.readAll(), payload);
        } else
            QVERIFY(!extracted.exists());
        QFile original(input);
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), payload);
    }
};
QTEST_GUILESS_MAIN(AgentOpeningTests)
#include "agent_opening_tests.moc"
