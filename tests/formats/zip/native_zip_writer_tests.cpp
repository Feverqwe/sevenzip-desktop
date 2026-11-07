#include "7zip/Archive/Zip/ZipHandler.h"
#include "7zip/Common/FileStreams.h"
#include "Windows/PropVariant.h"
#include "archive/agent/UpdateCallbackAgent.h"
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

namespace {
class CUpdateProgress final : public IFolderArchiveUpdateCallback, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_1(IFolderArchiveUpdateCallback)

  public:
    bool Cancel = false;
    Z7_COM7F_IMF(SetTotal(UInt64)) override { return Cancel ? E_ABORT : S_OK; }
    Z7_COM7F_IMF(SetCompleted(const UInt64 *)) override { return Cancel ? E_ABORT : S_OK; }
    Z7_COM7F_IMF(CompressOperation(const wchar_t *)) override { return S_OK; }
    Z7_COM7F_IMF(DeleteOperation(const wchar_t *)) override { return E_FAIL; }
    Z7_COM7F_IMF(OperationResult(Int32)) override { return S_OK; }
    Z7_COM7F_IMF(UpdateErrorMessage(const wchar_t *)) override { return E_FAIL; }
    Z7_COM7F_IMF(SetNumFiles(UInt64)) override { return S_OK; }
};
class CFailedOutput final : public ISequentialOutStream, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_1(ISequentialOutStream)
    Z7_COM7F_IMF(Write(const void *, UInt32, UInt32 *processed)) override {
        if (processed)
            *processed = 0;
        return E_FAIL;
    }
};
QByteArray bytes(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
} // namespace

class NativeZipWriterTests final : public QObject {
    Q_OBJECT
    QString engine_{QString::fromUtf8(SEVENZIP_ZIP_VERIFY_ENGINE)};
    bool run(const QStringList &args, const QString &directory = {}) {
        QProcess process;
        if (!directory.isEmpty())
            process.setWorkingDirectory(directory);
        process.start(engine_, args);
        return process.waitForFinished(10000) && process.exitCode() == 0;
    }

  private slots:
    void renamePreservesProperties_data() {
        QTest::addColumn<bool>("encrypted");
        QTest::addColumn<QString>("outcome");
        for (bool encrypted : {false, true})
            for (const QString &outcome :
                 {QStringLiteral("success"), QStringLiteral("failure"), QStringLiteral("cancel")})
                QTest::newRow(qPrintable(QString("%1-%2").arg(encrypted).arg(outcome)))
                    << encrypted << outcome;
    }
    void renamePreservesProperties() {
        QFETCH(bool, encrypted);
        QFETCH(QString, outcome);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString name = QStringLiteral("имя 空 with spaces.bin");
        const QByteArray payload("binary\0payload\xff", 15);
        QFile input(fixture.filePath(name));
        QVERIFY(input.open(QIODevice::WriteOnly));
        QCOMPARE(input.write(payload), qint64(payload.size()));
        input.close();
        QFile second(fixture.filePath("unchanged.txt"));
        QVERIFY(second.open(QIODevice::WriteOnly));
        QCOMPARE(second.write("other data"), qint64(10));
        second.close();
        const QString path = fixture.filePath("archive.zip");
        QStringList create{"a", "-tzip"};
        if (encrypted)
            create << "-plocal fixture password" << "-mem=AES256";
        create << "--" << path << name << "unchanged.txt";
        if (encrypted) {
            QVERIFY(run(create, fixture.path()));
        } else {
            // ZIP with a pre-existing item comment, produced by Python zipfile.
            const QByteArray fixtureBytes = QByteArray::fromBase64(
                "UEsDBBQAAAgAAAAAIQD9I60DDwAAAA8AAAAaAAAA0LjQvNGPIOepuiB3aXRoIHNwYWNlcy5iaW5i"
                "aW5hcnkAcGF5bG9hZP9QSwMEFAAAAAAAAAAhAK/SNb8KAAAACgAAAA0AAAB1bmNoYW5nZWQudHh0"
                "b3RoZXIgZGF0YVBLAQIUAxQAAAgAAAAAIQD9I60DDwAAAA8AAAAaAAAAFQAAAAAAAACAAQAAAADQ"
                "uNC80Y8g56m6IHdpdGggc3BhY2VzLmJpbmV4aXN0aW5nIGl0ZW0gY29tbWVudFBLAQIUAxQAAAAA"
                "AAAAIQCv0jW/CgAAAAoAAAANAAAAAAAAAAAAAACAAUcAAAB1bmNoYW5nZWQudHh0UEsFBgAAAAAC"
                "AAIAmAAAAHwAAAAAAA==");
            QFile archive(path);
            QVERIFY(archive.open(QIODevice::WriteOnly));
            QCOMPARE(archive.write(fixtureBytes), qint64(fixtureBytes.size()));
        }
        const QByteArray original = bytes(path);
        QVERIFY(!original.isEmpty());

        CMyComPtr2_Create<IInStream, CInFileStream> stream;
        QVERIFY(stream->Open(us2fs(path.toStdWString().c_str())));
        CMyComPtr<IInArchive> reader = new NArchive::NZip::CHandler;
        QCOMPARE(reader->Open(stream, nullptr, nullptr), S_OK);
        UInt32 count = 0;
        QCOMPARE(reader->GetNumberOfItems(&count), S_OK);
        QCOMPARE(count, UInt32(2));
        CRecordVector<CUpdatePair2> pairs;
        UInt32 selected = UInt32(-1);
        for (UInt32 i = 0; i < count; ++i) {
            CUpdatePair2 pair;
            pair.SetAs_NoChangeArcItem(i);
            NWindows::NCOM::CPropVariant itemPath;
            QCOMPARE(reader->GetProperty(i, kpidPath, &itemPath), S_OK);
            QCOMPARE(itemPath.vt, VARTYPE(VT_BSTR));
            if (QString::fromWCharArray(itemPath.bstrVal) == name) {
                selected = i;
                pair.NewProps = true;
                pair.NewNameIndex = 0;
                pair.IsMainRenameItem = true;
            }
            pairs.Add(pair);
        }
        QVERIFY(selected != UInt32(-1));
        const QString renamed = QStringLiteral("новое имя 空.bin");
        UStringVector names;
        names.Add(UString(renamed.toStdWString().c_str()));
        if (!encrypted) {
            NWindows::NCOM::CPropVariant comment;
            QCOMPARE(reader->GetProperty(selected, kpidComment, &comment), S_OK);
            QCOMPARE(comment.vt, VARTYPE(VT_BSTR));
            QCOMPARE(QString::fromWCharArray(comment.bstrVal), QString("existing item comment"));
        }
        CArc arc;
        arc.Archive = reader;
        auto *progress = new CUpdateProgress;
        CMyComPtr<IFolderArchiveUpdateCallback> progressOwner = progress;
        progress->Cancel = outcome == "cancel";
        CUpdateCallbackAgent ui;
        ui.SetCallback(progressOwner);
        CMyComPtr2_Create<IArchiveUpdateCallback, CArchiveUpdateCallback> callback;
        callback->Arc = &arc;
        callback->Archive = reader;
        callback->UpdatePairs = &pairs;
        callback->NewNames = &names;
        callback->Callback = &ui;
        CMyComPtr<IOutArchive> writer;
        QCOMPARE(reader.QueryInterface(IID_IOutArchive, &writer), S_OK);
        const QString outputPath = fixture.filePath("updated.zip");
        CMyComPtr<ISequentialOutStream> output;
        CMyComPtr2_Create<IOutStream, COutFileStream> file;
        if (outcome == "failure")
            output = new CFailedOutput;
        else {
            QVERIFY(file->Create_NEW(us2fs(outputPath.toStdWString().c_str())));
            output = file.Interface();
        }
        const HRESULT result = writer->UpdateItems(output, count, callback);
        QCOMPARE(bytes(path), original);
        QCOMPARE(bytes(input.fileName()), payload);
        if (outcome != "success") {
            QCOMPARE(result, outcome == "cancel" ? E_ABORT : E_FAIL);
            return;
        }
        QCOMPARE(result, S_OK);
        output.Release();
        QCOMPARE(file->Close(), S_OK);
        CMyComPtr2_Create<IInStream, CInFileStream> updatedStream;
        QVERIFY(updatedStream->Open(us2fs(outputPath.toStdWString().c_str())));
        CMyComPtr<IInArchive> updated = new NArchive::NZip::CHandler;
        QCOMPARE(updated->Open(updatedStream, nullptr, nullptr), S_OK);
        UInt32 updatedCount = 0;
        QCOMPARE(updated->GetNumberOfItems(&updatedCount), S_OK);
        QCOMPARE(updatedCount, count);
        for (UInt32 i = 0; i < count; ++i) {
            NWindows::NCOM::CPropVariant pathValue;
            QCOMPARE(updated->GetProperty(i, kpidPath, &pathValue), S_OK);
            QCOMPARE(pathValue.vt, VARTYPE(VT_BSTR));
            QCOMPARE(QString::fromWCharArray(pathValue.bstrVal),
                     i == selected ? renamed : QString("unchanged.txt"));
            for (PROPID id : {kpidComment, kpidSize, kpidCRC, kpidEncrypted}) {
                NWindows::NCOM::CPropVariant before, after;
                QCOMPARE(reader->GetProperty(i, id, &before), S_OK);
                QCOMPARE(updated->GetProperty(i, id, &after), S_OK);
                QCOMPARE(before.Compare(after), 0);
            }
        }
        const QString extracted = fixture.filePath("extracted");
        QVERIFY(run({"x", "-plocal fixture password", "-o" + extracted, "--", outputPath}));
        QCOMPARE(bytes(extracted + "/" + renamed), payload);
        QCOMPARE(bytes(extracted + "/unchanged.txt"), QByteArray("other data"));
    }
};

QTEST_GUILESS_MAIN(NativeZipWriterTests)
#include "native_zip_writer_tests.moc"
