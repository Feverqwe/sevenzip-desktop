#include "7zip/IPassword.h"
#include "Windows/PropVariant.h"
#include "archive/agent/Agent.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/resources/archivereadstream.h"
#include "archive/resources/qtarchivefileresource.h"
#include "archive/resources/qtarchivereopen.h"
#include "archive/support/archive_copy_callback.h"
#include "archive/support/archive_reader_fixture.h"
#include "ui/progress/ProgressDialog2.h"
#include "ui/progress/archiveprogresstransport.h"
#include <QScopeGuard>

namespace {
class CReadRequest final : public IProgress, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_1(IProgress)
  public:
    bool Cancel = false;
    Z7_COM7F_IMF(SetTotal(UInt64)) override { return Cancel ? E_ABORT : S_OK; }
    Z7_COM7F_IMF(SetCompleted(const UInt64 *)) override { return Cancel ? E_ABORT : S_OK; }
};
} // namespace

class ArchiveLayerLifetimeTests final : public ArchiveReaderFixture {
    Q_OBJECT
    QString module_;
    std::unique_ptr<ArchiveBackend> Backend() {
        auto backend = std::make_unique<ArchiveBackend>(nullptr, engine_);
        if (backend->loadCodecCatalog(module_, backend_->codecCatalog()->version()) != S_OK)
            return {};
        return backend;
    }
    HRESULT Stream(IFolderFolder *folder, UInt32 index, ISequentialInStream **stream,
                   IProgress *request = nullptr) {
        return Worker([&]() -> HRESULT {
            CMyComPtr<IArchiveGetStreamWithRequest> source;
            RINOK(folder->QueryInterface(IID_IArchiveGetStreamWithRequest, (void **)&source))
            return source->GetStreamWithRequest(index, stream, request);
        });
    }

  private slots:
    void initTestCase() {
        InitializeRuntime();
#ifdef SEVENZIP_CODEC_MODULE
        module_ = QStringLiteral(SEVENZIP_CODEC_MODULE);
#endif
    }
    void descendantsOutliveRefreshAndWindow_data() {
        QTest::addColumn<QString>("format");
        QTest::newRow("native-seekable-leaf") << "tar";
        QTest::newRow("native-extract-without-seek") << "zip";
    }
    void descendantsOutliveRefreshAndWindow() {
        QFETCH(QString, format);
        QTemporaryDir fixture;
        const QString name = QStringLiteral("выбранный 空 файл.bin");
        QByteArray payload(180000, '\0');
        for (qsizetype i = 0; i < payload.size(); ++i)
            payload[i] = char(i * 71 + i / 137);
        QVERIFY(Put(fixture.filePath(name), payload));
        QVERIFY(Make(fixture.path(), {"a", "-t" + format, "inner." + format, name}));
        QVERIFY(Make(fixture.path(), {"a", "-ttar", "outer.tar", "inner." + format}));
        const QString path = fixture.filePath("outer.tar");
        const auto original = Bytes(path);
        auto parentBackend = Backend();
        QVERIFY(parentBackend);
        auto parentCatalog =
            std::weak_ptr<const ArchiveCodecCatalog>(parentBackend->codecCatalog());
        CMyComPtr<IFolderManager> parentManager = new QtArchiveFolderManager(*parentBackend);
        CMyComPtr<IFolderFolder> parent;
        const auto nativePath = path.toStdWString();
        QCOMPARE(Worker([&] {
                     return parentManager->OpenFolderFile(nullptr, nativePath.c_str(), L"tar",
                                                          &parent, nullptr);
                 }),
                 S_OK);
        CMyComPtr<ISequentialInStream> item;
        QCOMPARE(Stream(parent, 0, &item), S_OK);
        QVERIFY(item);
        CMyComPtr<IInStream> input;
        QCOMPARE(item.QueryInterface(IID_IInStream, &input), S_OK);
        auto *parentAgent = dynamic_cast<CAgentFolder *>(parent.Interface());
        QVERIFY(parentAgent);
        CMyComPtr<IFolderFolder> refreshed;
        QCOMPARE(ReOpenArchiveFolder(*parentAgent, {}, nullptr, &refreshed, false, UString()),
                 S_OK);
        parent.Release();
        parentManager.Release();
        parentBackend.reset();
        QVERIFY(!parentCatalog.expired());
        auto *retainedParent = dynamic_cast<CAgentFolder *>(refreshed.Interface());
        QVERIFY(retainedParent);
        CMyComPtr<IFolderFolder> afterWindow;
        QCOMPARE(ReOpenArchiveFolder(*retainedParent, {}, nullptr, &afterWindow, false, UString()),
                 S_OK);
        afterWindow.Release();
        refreshed.Release();
        auto childBackend = Backend();
        QVERIFY(childBackend);
        auto childCatalog = std::weak_ptr<const ArchiveCodecCatalog>(childBackend->codecCatalog());
        CMyComPtr<IFolderManager> childManager = new QtArchiveFolderManager(*childBackend);
        CMyComPtr<IFolderFolder> child;
        const auto type = format.toStdWString();
        QCOMPARE(Worker([&] {
                     return childManager->OpenFolderFile(input, L"virtual child", type.c_str(),
                                                         &child, nullptr);
                 }),
                 S_OK);
        QCOMPARE(child->LoadItems(), S_OK);
        CMyComPtr<ISequentialInStream> first, second;
        QCOMPARE(Stream(child, 0, &first), S_OK);
        if (format == "tar") {
            QVERIFY(first);
            QCOMPARE(Stream(child, 0, &second), S_OK);
            QVERIFY(second);
        } else
            QVERIFY(!first); // ZIP offers Extract, not an invented seekable byte API.
        childManager.Release();
        childBackend.reset();
        item.Release();
        input.Release();
        // Native extraction remains available after both window services disappear.
        CMyComPtr<IFolderOperations> operations;
        QCOMPARE(child.QueryInterface(IID_IFolderOperations, &operations), S_OK);
        auto *observer = new CArchiveCopyCallback;
        CMyComPtr<IFolderOperationsExtractCallback> callback = observer;
        const QString output = fixture.filePath("output");
        QVERIFY(QDir().mkpath(output));
        const auto prefix = (output + '/').toStdWString();
        const UInt32 index = 0;
        QCOMPARE(Worker([&] {
                     return operations->CopyTo(0, &index, 1, 0, 0, prefix.c_str(), callback);
                 }),
                 S_OK);
        QCOMPARE(observer->Errors, 0u);
        QCOMPARE(Bytes(QDir(output).filePath(name)), payload);
        operations.Release();
        child.Release();
        if (first) {
            auto *control = new CReadRequest;
            CMyComPtr<IProgress> request = control;
            CMyComPtr<IArchiveReadStream> leaf, peer;
            QCOMPARE(first.QueryInterface(IID_IArchiveReadStream, &leaf), S_OK);
            QCOMPARE(second.QueryInterface(IID_IArchiveReadStream, &peer), S_OK);
            for (UInt64 offset : {UInt64(130000), UInt64(0), UInt64(70000)}) {
                QByteArray bytes(4096, '\0');
                UInt32 actual = 0;
                QCOMPARE(Worker([&]() -> HRESULT {
                             RINOK(leaf->SeekWithRequest(offset, STREAM_SEEK_SET, nullptr, request))
                             // The peer must not change this cursor's position.
                             RINOK(peer->SeekWithRequest(17, STREAM_SEEK_SET, nullptr, request))
                             char probe;
                             RINOK(peer->ReadWithRequest(&probe, 1, &actual, request))
                             return leaf->ReadWithRequest(bytes.data(), bytes.size(), &actual,
                                                          request);
                         }),
                         S_OK);
                QCOMPARE(actual, UInt32(bytes.size()));
                QCOMPARE(bytes, payload.mid(qsizetype(offset), bytes.size()));
            }
            control->Cancel = true;
            UInt32 actual = 99;
            char byte;
            QCOMPARE(Worker([&] { return leaf->ReadWithRequest(&byte, 1, &actual, request); }),
                     E_ABORT);
            QCOMPARE(actual, UInt32(0));
            control->Cancel = false;
            QCOMPARE(Worker([&] { return leaf->ReadWithRequest(&byte, 1, &actual, request); }),
                     S_OK);
            QCOMPARE(actual, UInt32(1));
            QCOMPARE(byte, payload[74096]);
        }
        first.Release();
        second.Release();
        QVERIFY(parentCatalog.expired());
        QVERIFY(childCatalog.expired());
        QCOMPARE(Bytes(path), original);
    }
    void progressAfterWindowRelease_data() {
        QTest::addColumn<bool>("native");
        QTest::addColumn<bool>("resourceTransport");
        QTest::newRow("native callbacks") << true << false;
        QTest::newRow("unavailable process") << false << false;
        QTest::newRow("resource native callbacks") << true << true;
        QTest::newRow("resource unavailable process") << false << true;
    }
    void progressAfterWindowRelease() {
        QFETCH(bool, native);
        QFETCH(bool, resourceTransport);
        auto backend = Backend();
        QVERIFY(backend);
        CProgressDialog dialog;
        std::unique_ptr<IQtProgressTransport> transport;
        if (resourceTransport) {
            CMyComPtr2<IUnknown, QtArchiveFileResource> resource;
            resource.SetFromCls(new QtArchiveFileResource(QString()));
            resource->AttachProgressService(*backend);
            transport = resource->CreateProgressTransport(dialog, native);
        } else
            transport = std::make_unique<ArchiveProgressTransport>(dialog, *backend, native);
        backend.reset();
        QCOMPARE(transport->SetPaused(true), native ? S_OK : E_FAIL);
        QCOMPARE(transport->SetPaused(false), native ? S_OK : E_FAIL);
        transport->Cancel();
    }
    void resourceTransportCancelsPausedOperation() {
        QTemporaryDir fixture;
        const QString path = fixture.filePath(QStringLiteral("данные 空 with spaces.bin"));
        const QByteArray bytes("resource progress bytes");
        QVERIFY(Put(path, bytes));
        auto backend = Backend();
        QVERIFY(backend);
        const auto cleanup = qScopeGuard([&] { backend->cancelAndWait(); });
        CProgressDialog dialog;
        dialog.Sync.Set_Paused(true);
        CMyComPtr2<IUnknown, QtArchiveFileResource> resource;
        resource.SetFromCls(new QtArchiveFileResource(QString()));
        resource->AttachProgressService(*backend);
        auto transport = resource->CreateProgressTransport(dialog);
        QVERIFY(transport);
        QSignalSpy finished(backend.get(), &ArchiveBackend::finished);
        backend->hashMethods({path}, {"CRC32"});
        QVERIFY(backend->busy());
        transport->Cancel();
        QTRY_COMPARE(finished.size(), 1);
        QVERIFY(!finished.front().front().toBool());
        QCOMPARE(backend->lastResult(), HRESULT(E_ABORT));
        QVERIFY(!backend->busy());
        QCOMPARE(Bytes(path), bytes);
    }
};
QTEST_MAIN(ArchiveLayerLifetimeTests)
#include "archive_layer_lifetime_tests.moc"
