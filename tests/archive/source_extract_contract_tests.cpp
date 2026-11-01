#include "7zip/Common/StreamObjects.h"
#include "archive/backend/archivebackend.h"
#include "archive/resources/qtarchivefileresource.h"
#include "archive/support/source_extract_fixture.h"
#include "filemanager/panels/qtpanelfolder.h"
#include "ui/progress/ProgressDialog2.h"
#include <QTemporaryDir>
#include <QtTest>
using namespace SourceExtractFixture;
namespace {
class CContractCallback final : public IArchiveExtractCallback, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_1(IArchiveExtractCallback)
    CMyComPtr2<ISequentialOutStream, CDynBufSeqOutStream> _stream;

  public:
    UInt64 Total = 0;
    QVector<UInt64> Completed;
    QVector<UInt32> Indices;
    QList<QByteArray> Bytes;
    QVector<Int32> Results;
    HRESULT errorResult = E_FAIL;
    Z7_COM7F_IMF(SetTotal(UInt64 total)) override {
        Total = total;
        return S_OK;
    }
    Z7_COM7F_IMF(SetCompleted(const UInt64 *completed)) override {
        if (completed)
            Completed.append(*completed);
        return S_OK;
    }
    Z7_COM7F_IMF(GetStream(UInt32 index, ISequentialOutStream **stream, Int32)) override {
        Indices.append(index);
        _stream.SetFromCls(new CDynBufSeqOutStream);
        CMyComPtr<ISequentialOutStream> owner = _stream.Interface();
        *stream = owner.Detach();
        return S_OK;
    }
    Z7_COM7F_IMF(PrepareOperation(Int32)) override { return S_OK; }
    Z7_COM7F_IMF(SetOperationResult(Int32 result)) override {
        Results.append(result);
        Bytes.append(
            QByteArray(reinterpret_cast<const char *>(_stream->GetBuffer()), _stream->GetSize()));
        return result == NArchive::NExtract::NOperationResult::kOK ? S_OK : errorResult;
    }
};

class CExtractContractWorker final : public CProgressThreadVirt {
    CMyComPtr<IUnknown> _resource;
    QtArchiveFileResource *_resourceSpec;
    CMyComPtr<IFolderFolder> _metadata;
    std::unique_ptr<IQtProgressTransport> _transport;
    HRESULT ProcessVirt() override {
        CMyComPtr<IInArchive> archive;
        RINOK(_metadata.QueryInterface(IID_IInArchive, &archive))
        const UInt32 ignored = UInt32(-1);
        return _resourceSpec->Extract(archive, false, &ignored, UInt32(-1), false,
                                      Callback.Interface());
    }

  public:
    CMyComPtr2_Create<IArchiveExtractCallback, CContractCallback> Callback;
    CExtractContractWorker(ArchiveBackend &backend, const QString &path, IFolderFolder *metadata,
                           const QString &format = "zip")
        : _resourceSpec(new QtArchiveFileResource(path)), _metadata(metadata) {
        _resource = _resourceSpec;
        _resourceSpec->AttachExtractService(backend, format);
        _transport = _resourceSpec->CreateProgressTransport(*this);
    }
};
} // namespace
class SourceExtractContractTests final : public QObject {
    Q_OBJECT
  private slots:
    void unverifiedBytesRetainCrcErrorAndHealthySiblings_data() {
        QTest::addColumn<QString>("format");
        QTest::newRow("zip") << QString("zip");
        QTest::newRow("7z") << QString("7z");
    }
    void unverifiedBytesRetainCrcErrorAndHealthySiblings() {
        QFETCH(QString, format);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QByteArray broken(256, 'B'), healthy("healthy bytes");
        QVERIFY(writeFile(directory.filePath("broken.txt"), broken));
        QVERIFY(writeFile(directory.filePath("healthy.txt"), healthy));
        QProcess process;
        process.setWorkingDirectory(directory.path());
        const QString archiveName = "input." + format;
        process.start(QStandardPaths::findExecutable("7z"),
                      {"a", "-t" + format, "-mx=0", archiveName, "broken.txt", "healthy.txt"});
        QVERIFY(process.waitForFinished(10000));
        QCOMPARE(process.exitCode(), 0);
        const QString path = directory.filePath(archiveName);
        QByteArray image = readFile(path);
        const auto payload = image.indexOf(broken);
        QVERIFY(payload > 0);
        image[payload] = '!';
        QVERIFY(writeFile(path, image));
        CObjectVector<CPanelItem> items;
        for (const auto &entry : {qMakePair(QString("broken.txt"), broken),
                                  qMakePair(QString("healthy.txt"), healthy)}) {
            CPanelItem item;
            item.RelPath = entry.first;
            item.SizeDefined = true;
            item.Size = entry.second.size();
            items.Add(item);
        }
        CMyComPtr<IFolderFolder> metadata =
            new QtPanelFolder({}, items, QtPanelFolder::PropertyCatalog::ArchiveTransport);
        ArchiveBackend backend;
        QStringList diagnostics;
        connect(&backend, &ArchiveBackend::outputReceived, &backend,
                [&](const QString &line) { diagnostics.append(line); });
        CExtractContractWorker worker(backend, path, metadata, format);
        worker.Callback->errorResult = S_OK;
        QCOMPARE(worker.Create(L"Unverified data"), S_OK);
        QCOMPARE(worker.Result, S_OK);
        QVERIFY2(worker.Callback->Results.value(0) ==
                     NArchive::NExtract::NOperationResult::kCRCError,
                 qPrintable(diagnostics.join('\n')));
        QCOMPARE(worker.Callback->Results,
                 QVector<Int32>({NArchive::NExtract::NOperationResult::kCRCError,
                                 NArchive::NExtract::NOperationResult::kOK}));
        QCOMPARE(worker.Callback->Bytes,
                 QList<QByteArray>({QByteArray("!") + broken.mid(1), healthy}));
        CExtractContractWorker aborting(backend, path, metadata, format);
        aborting.Callback->errorResult = E_ABORT;
        QCOMPARE(aborting.Create(L"Stop after checksum failure"), S_OK);
        QCOMPARE(aborting.Result, E_ABORT);
        QCOMPARE(aborting.Callback->Indices, QVector<UInt32>({0}));
        QCOMPARE(readFile(path), image);
    }
    void cancelledSourceItemDoesNotReportDataError() {
        QTemporaryDir directory;
        const auto archive = makeArchive(directory.path(), QByteArray(4 << 20, 'x'));
        QVERIFY(!archive.isEmpty());
        const QByteArray original = readFile(archive);
        CPanelItem item;
        item.RelPath = kName;
        item.SizeDefined = true;
        item.Size = 4 << 20;
        CObjectVector<CPanelItem> items;
        items.Add(item);
        CMyComPtr<IFolderFolder> metadata =
            new QtPanelFolder({}, items, QtPanelFolder::PropertyCatalog::ArchiveTransport);
        ArchiveBackend backend;
        connect(&backend, &ArchiveBackend::phaseStarted, &backend,
                [&backend] { backend.cancel(); });
        CExtractContractWorker worker(backend, archive, metadata);
        QCOMPARE(worker.Create(L"Cancelled source item"), S_OK);
        QCOMPARE(worker.Result, E_ABORT);
        QVERIFY(worker.Callback->Results.isEmpty());
        QVERIFY(worker.Callback->Bytes.isEmpty());
        QVERIFY(!backend.busy());
        QCOMPARE(readFile(archive), original);
    }

    void allItemsIgnoreIndicesAndAccumulateProgress() {
        QTemporaryDir directory;
        const QByteArray first("first\0binary", 12), second(256 << 10, 'z');
        const auto archive = makeArchive(directory.path(), first);
        QVERIFY(!archive.isEmpty());
        const QString other = QStringLiteral("ещё один.bin");
        QVERIFY(writeFile(directory.filePath(other), second));
        QProcess process;
        process.setWorkingDirectory(directory.path());
        process.start(QStandardPaths::findExecutable("7z"), {"a", "input.zip", other});
        QVERIFY(process.waitForFinished(10000));
        QCOMPARE(process.exitCode(), 0);
        ArchiveBackend backend;
        QList<ArchiveEntry> listed;
        connect(&backend, &ArchiveBackend::entriesReady, &backend,
                [&listed](const QList<ArchiveEntry> &entries) { listed = entries; });
        backend.list(archive);
        QTRY_VERIFY(!backend.busy());
        CObjectVector<CPanelItem> items;
        for (const auto &pair : {qMakePair(kName, first), qMakePair(other, second)}) {
            CPanelItem item;
            QString actualName;
            for (const auto &entry : listed) {
                QString compared = entry.path;
                compared.replace('\\', '/');
                if (!entry.directory && compared.normalized(QString::NormalizationForm_C) ==
                                            pair.first.normalized(QString::NormalizationForm_C)) {
                    QVERIFY(actualName.isEmpty());
                    actualName = entry.path;
                }
            }
            QVERIFY(!actualName.isEmpty());
            item.RelPath = actualName;
            item.SizeDefined = true;
            item.Size = pair.second.size();
            items.Add(item);
        }
        CMyComPtr<IFolderFolder> metadata =
            new QtPanelFolder({}, items, QtPanelFolder::PropertyCatalog::ArchiveTransport);
        CExtractContractWorker worker(backend, archive, metadata);
        QCOMPARE(worker.Create(L"All entries"), S_OK);
        QCOMPARE(worker.Result, S_OK);
        QCOMPARE(worker.Callback->Indices, QVector<UInt32>({0, 1}));
        QCOMPARE(worker.Callback->Bytes, QList<QByteArray>({first, second}));
        QCOMPARE(worker.Callback->Total, UInt64(first.size() + second.size()));
        QVERIFY(!worker.Callback->Completed.isEmpty());
        UInt64 previous = 0;
        for (UInt64 value : worker.Callback->Completed) {
            QVERIFY(value >= previous);
            QVERIFY(value <= worker.Callback->Total);
            previous = value;
        }
        QCOMPARE(previous, worker.Callback->Total);
    }
};
QTEST_MAIN(SourceExtractContractTests)
#include "source_extract_contract_tests.moc"
