#include "7zip/Common/StreamObjects.h"
#include "Windows/PropVariant.h"
#include "archive/callbacks/ArchiveExtractCallback.h"
#include "archive/hashing/HashCalc.h"
#include "archive/hashing/qtfilecrc.h"
#include "archive/resources/qtarchivehashoperation.h"
#include "dialogs/taskresult/qttaskresultdialog.h"
#include "filemanager/ExtractCallback.h"
#include "filemanager/panels/qtpanelfolder.h"
#include "gui/HashGUI.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QPlainTextEdit>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

namespace {
class CHashFixture final : public IArchiveUpdateCallback,
                           public IArchiveExtractCallback,
                           public IArchiveUpdateCallbackFile,
                           public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_3(IArchiveUpdateCallback, IArchiveExtractCallback,
                         IArchiveUpdateCallbackFile)
  public:
    QByteArray Data = "123456789";
    UString Name = L"имя с пробелом.txt";
    bool Canceled = false;
    bool Missing = false;
    QList<Int32> Results;
    Z7_COM7F_IMF(SetTotal(UInt64)) override { return Canceled ? E_ABORT : S_OK; }
    Z7_COM7F_IMF(SetCompleted(const UInt64 *)) override { return Canceled ? E_ABORT : S_OK; }
    Z7_COM7F_IMF(GetUpdateItemInfo(UInt32, Int32 *data, Int32 *props, UInt32 *index)) override {
        *data = *props = 1;
        *index = UInt32(-1);
        return S_OK;
    }
    Z7_COM7F_IMF(GetProperty(UInt32, PROPID id, PROPVARIANT *value)) override {
        NWindows::NCOM::CPropVariant property;
        if (id == kpidPath)
            property = Name;
        else if (id == kpidIsDir)
            property = false;
        else if (id == kpidSize)
            property = UInt64(Data.size());
        return property.Detach(value);
    }
    Z7_COM7F_IMF(GetStream(UInt32 index, ISequentialInStream **stream)) override {
        return GetStream2(index, stream, 0);
    }
    Z7_COM7F_IMF(GetStream2(UInt32, ISequentialInStream **stream, UInt32)) override {
        *stream = nullptr;
        if (Missing)
            return E_FAIL;
        if (Canceled)
            return E_ABORT;
        Create_BufInStream_WithNewBuffer(Data.constData(), size_t(Data.size()), stream);
        return S_OK;
    }
    Z7_COM7F_IMF(GetStream(UInt32, ISequentialOutStream **stream, Int32)) override {
        *stream = nullptr;
        return S_OK;
    }
    Z7_COM7F_IMF(PrepareOperation(Int32)) override { return S_OK; }
    Z7_COM7F_IMF(ReportOperation(UInt32, UInt32, UInt32)) override { return S_OK; }
    Z7_COM7F_IMF(SetOperationResult(Int32 result)) override {
        Results.append(result);
        return S_OK;
    }
};
} // namespace

class NativeHashWorkflowTests final : public QObject {
    Q_OBJECT
    static bool write(const QString &path, const QByteArray &data) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
    }
  private slots:
    void panelFileResultIsShownOnce() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString name = QStringLiteral("имя с пробелом.txt");
        const QByteArray payload = "native checksum fixture";
        QVERIFY(write(fixture.filePath(name), payload));
        UStringVector paths;
        paths.Add(name.toStdWString().c_str());
        CExternalCodecs providers;
        int resultWindows = 0;
        QString resultText;
        QTimer responder;
        connect(&responder, &QTimer::timeout, this, [&] {
            auto *dialog = qobject_cast<QtTaskResultDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            ++resultWindows;
            auto *rows = dialog->findChild<QPlainTextEdit *>("taskResultReport");
            resultText = rows->toPlainText();
            dialog->accept();
        });
        responder.start(5);
        const UString prefix((fixture.path() + '/').toStdWString().c_str());
        const HRESULT result =
            CalculateFileCrc(L"SHA256", &providers, prefix, paths, true, nullptr);
        responder.stop();
        QCOMPARE(result, S_OK);
        QCOMPARE(resultWindows, 1);
        QVERIFY(resultText.contains(QString::fromLatin1(
            QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex())));
    }
    void extractedDirectoryContributesToHash_data() {
        QTest::addColumn<bool>("toStream");
        QTest::newRow("filesystem") << false;
        QTest::newRow("stream-callback") << true;
    }
    void extractedDirectoryContributesToHash() {
        QFETCH(bool, toStream);
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString name = QStringLiteral("пустая папка 空");
        CPanelItem item;
        item.RelPath = name;
        item.IsFolder = true;
        CObjectVector<CPanelItem> items;
        items.Add(item);
        CMyComPtr<IFolderFolder> metadata =
            new QtPanelFolder({}, items, QtPanelFolder::PropertyCatalog::ArchiveTransport);
        CArc arc;
        QCOMPARE(metadata.QueryInterface(IID_IInArchive, &arc.Archive), S_OK);
        CHashBundle hash;
        QCOMPARE(hash.SetMethods(nullptr, UStringVector()), S_OK);
        CProgressDialog progress;
        CMyComPtr2_Create<IFolderArchiveExtractCallback, CExtractCallbackImp> ui;
        ui->ProgressDialog = &progress;
        ui->Init();
        ui->StreamMode = toStream;
        if (toStream)
            ui->SetHashMethods(&hash);
        CMyComPtr2_Create<IArchiveExtractCallback, CArchiveExtractCallback> callback;
        callback->InitForMulti(false, NExtract::NPathMode::kFullPaths,
                               NExtract::NOverwriteMode::kAsk, NExtract::NZoneIdMode::kNone, false);
        callback->InitBeforeNewArchive();
        callback->Init(CExtractNtOptions(), nullptr, &arc, ui, false, false,
                       us2fs(temporary.path().toStdWString().c_str()), UStringVector(), false, 0);
        if (!toStream)
            callback->SetHashMethods(&hash);
        CMyComPtr<ISequentialOutStream> stream;
        QCOMPARE(
            callback.Interface()->GetStream(0, &stream, NArchive::NExtract::NAskMode::kExtract),
            S_OK);
        QCOMPARE(callback.Interface()->PrepareOperation(NArchive::NExtract::NAskMode::kExtract),
                 S_OK);
        QCOMPARE(
            callback.Interface()->SetOperationResult(NArchive::NExtract::NOperationResult::kOK),
            S_OK);
        stream.Release();
        QCOMPARE(callback->CloseArc(), S_OK);
        QCOMPARE(hash.NumDirs, UInt64(1));
        QCOMPARE(hash.NumFiles, UInt64(0));
        QCOMPARE(hash.FilesSize, UInt64(0));
        char namesHash[160];
        hash.Hashers[0].WriteToString(k_HashCalc_Index_NamesSum, namesHash);
        QVERIFY(QByteArray(namesHash) != QByteArray("00000000"));
        if (!toStream)
            QVERIFY(QDir(temporary.filePath(name)).exists());
    }
    void filesystemMethodsAndScanTotals_data() {
        QTest::addColumn<QStringList>("methods");
        QTest::newRow("multiple") << QStringList{"CRC32", "SHA256", "MD5"};
        QTest::newRow("all") << QStringList{"*"};
    }
    void filesystemMethodsAndScanTotals() {
        QFETCH(QStringList, methods);
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString root = temporary.filePath(QStringLiteral("папка с пробелом"));
        QVERIFY(QDir().mkpath(root + "/empty"));
        QVERIFY(write(root + QStringLiteral("/имя.txt"), "123456789"));
        QVERIFY(write(root + "/other.txt", "abc"));
        CHashOptions options;
        for (const QString &method : methods)
            options.Methods.Add(method.toStdWString().c_str());
        QtArchiveHashOperation worker({root}, std::move(options), {},
                                      std::make_unique<CExternalCodecs>());
        QSignalSpy finished(&worker, &QThread::finished);
        worker.start();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 30000);
        QVERIFY(worker.wait());
        QCOMPARE(worker.Result, S_OK);
        QCOMPARE(worker.Bundle.NumFiles, UInt64(2));
        QCOMPARE(worker.Bundle.NumDirs, UInt64(2));
        QCOMPARE(worker.Bundle.FilesSize, UInt64(12));
        QCOMPARE(worker.Bundle.NumErrors, UInt64(0));
        QVERIFY(worker.Errors.isEmpty());
        const QString output = worker.Output.join('\n');
        QVERIFY(output.contains("CBF43926"));
        QVERIFY(output.contains(QString::fromLatin1(
            QCryptographicHash::hash("123456789", QCryptographicHash::Sha256).toHex())));
        QVERIFY(output.contains(
            QString::fromLatin1(QCryptographicHash::hash("abc", QCryptographicHash::Md5).toHex())));
        if (methods.first() != "*")
            QCOMPARE(worker.Bundle.Hashers.Size(), 3u);
    }
    void sourceWorkerFailureAndPausedCancellation_data() {
        QTest::addColumn<bool>("alreadyFinished");
        QTest::newRow("completion-after-wait") << false;
        QTest::newRow("completion-before-wait") << true;
    }
    void sourceWorkerFailureAndPausedCancellation() {
        QFETCH(bool, alreadyFinished);
        QTemporaryDir temporary;
        const QString path = temporary.filePath("input.txt");
        QVERIFY(write(path, "input"));
        for (bool cancel : {false, true}) {
            CHashOptions options;
            options.Methods.Add(cancel ? L"SHA256" : L"unknown");
            QtArchiveHashOperation worker({path}, std::move(options), {},
                                          std::make_unique<CExternalCodecs>());
            QSignalSpy finished(&worker, &QThread::finished);
            if (cancel)
                worker.setPaused(true);
            worker.start();
            if (cancel)
                worker.cancel();
            if (alreadyFinished)
                QVERIFY(worker.wait(30000));
            QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 30000);
            QVERIFY(worker.wait());
            QCOMPARE(worker.Result, cancel ? E_ABORT : E_NOTIMPL);
            QVERIFY(worker.Output.isEmpty());
        }
    }
    void originalHashFileWriteReadVerifyFailureCancel() {
        CMyComPtr<IInArchive> owner = new NHash::CHandler;
        CMyComPtr<IOutArchive> writer;
        QCOMPARE(owner.QueryInterface(IID_IOutArchive, &writer), S_OK);
        CMyComPtr<ISetProperties> properties;
        QCOMPARE(owner.QueryInterface(IID_ISetProperties, &properties), S_OK);
        const wchar_t *name = L"m";
        NWindows::NCOM::CPropVariant method(L"SHA256");
        QCOMPARE(properties->SetProperties(&name, &method, 1), S_OK);
        CMyComPtr2<ISequentialOutStream, CDynBufSeqOutStream> output;
        output.Create_if_Empty();
        output->Init();
        CMyComPtr2<IArchiveUpdateCallback, CHashFixture> fixture;
        fixture.Create_if_Empty();
        QCOMPARE(writer->UpdateItems(output.Interface(), 1, fixture.Interface()), S_OK);
        const QByteArray hashFile(reinterpret_cast<const char *>(output->GetBuffer()),
                                  int(output->GetSize()));
        QVERIFY(hashFile.contains(
            QCryptographicHash::hash("123456789", QCryptographicHash::Sha256).toHex()));
        QVERIFY(hashFile.contains(QStringLiteral("имя с пробелом.txt").toUtf8()));
        CMyComPtr2<IInStream, CBufInStream> input;
        input.Create_if_Empty();
        input->Init(reinterpret_cast<const Byte *>(hashFile.constData()), size_t(hashFile.size()));
        CMyComPtr<IInArchive> reader = new NHash::CHandler;
        QCOMPARE(reader->Open(input.Interface(), nullptr, nullptr), S_OK);
        UInt32 count = 0;
        QCOMPARE(reader->GetNumberOfItems(&count), S_OK);
        QCOMPARE(count, 1u);
        NWindows::NCOM::CPropVariant path;
        QCOMPARE(reader->GetProperty(0, kpidPath, &path), S_OK);
        QCOMPARE(QString::fromWCharArray(path.bstrVal), QStringLiteral("имя с пробелом.txt"));
        fixture->Results.clear();
        auto *callback = static_cast<IArchiveExtractCallback *>(fixture.operator->());
        QCOMPARE(reader->Extract(nullptr, UInt32(-1), 1, callback), S_OK);
        QCOMPARE(fixture->Results, QList<Int32>{NArchive::NExtract::NOperationResult::kOK});
        fixture->Data = "corrupt";
        fixture->Results.clear();
        QCOMPARE(reader->Extract(nullptr, UInt32(-1), 1, callback), S_OK);
        QCOMPARE(fixture->Results, QList<Int32>{NArchive::NExtract::NOperationResult::kCRCError});
        fixture->Missing = true;
        QCOMPARE(reader->Extract(nullptr, UInt32(-1), 1, callback), E_FAIL);
        fixture->Missing = false;
        fixture->Canceled = true;
        QCOMPARE(reader->Extract(nullptr, UInt32(-1), 1, callback), E_ABORT);
        output->Init();
        QCOMPARE(writer->UpdateItems(output.Interface(), 1, fixture.Interface()), E_ABORT);
        QCOMPARE(output->GetSize(), size_t(0));
    }
    void originalGuiWorkerShowsMultipleMethods() {
        QTemporaryDir temporary;
        const QString path = temporary.filePath(QStringLiteral("файл с пробелом.txt"));
        QVERIFY(write(path, "123456789"));
        NWildcard::CCensor censor;
        NWildcard::CCensorPathProps props;
        props.WildcardMatching = false;
        censor.AddItem(NWildcard::k_RelatPath, true, path.toStdWString().c_str(), props);
        CHashOptions options;
        options.Methods.Add(L"CRC32");
        options.Methods.Add(L"SHA256");
        bool shown = false;
        QTimer closeResults;
        connect(&closeResults, &QTimer::timeout, this, [&] {
            auto *dialog = qobject_cast<QtTaskResultDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            auto *rows = dialog->findChild<QPlainTextEdit *>("taskResultReport");
            shown = rows->toPlainText().contains("CBF43926");
            dialog->accept();
        });
        closeResults.start(1);
        bool messages = false;
        QCOMPARE(HashCalcGUI(nullptr, censor, options, messages), S_OK);
        closeResults.stop();
        QVERIFY(shown);
    }
};
QTEST_MAIN(NativeHashWorkflowTests)
#include "native_hash_workflow_tests.moc"
