#include "7zip/Common/StreamObjects.h"
#include "7zip/UI/FileManager/ProgressDialog2Res.h"
#include "Common/StringConvert.h"
#include "archive/backend/archivebackend.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/resources/archivenativeopen.h"
#include "archive/resources/qtarchivebatchopen.h"
#include "archive/resources/qtarchiveproperties.h"
#include "archive/support/native_archive_fixture.h"
#include "archive/support/source_extract_fixture.h"
#include "dialogs/password/PasswordDialog.h"
#include "filemanager/ExtractCallback.h"
#include "formats/rar/support/rar_memory_fixture.h"
#include "operations/CompressCall.h"
#include "platform/shell/qtshellarchiveservices.h"
#include "support/progress_dialog.h"
#include "ui/progress/ProgressDialog2.h"
#include <QApplication>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <future>

namespace {
class NativeOnlyBackend final : public ArchiveBackend {
  public:
    int listingCalls = 0;
    void openFolder(const QString &, const QString &, const QString & = {}) override {
        ++listingCalls;
        emit finished(false, "CLI listing unavailable");
    }
};
class FinalMessageDismissal {
    QTimer timer;

  public:
    FinalMessageDismissal() {
        QObject::connect(&timer, &QTimer::timeout, [] {
            for (auto *widget : QApplication::topLevelWidgets()) {
                if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible())
                    box->accept();
                if (widget->objectName() == "progressDialog" && widget->isVisible()) {
                    auto *close = widget->findChild<QPushButton *>("IDCANCEL");
                    if (close && close->property("languageID").toUInt() == IDS_CLOSE)
                        close->click();
                }
            }
        });
        timer.start(1);
    }
};
class CancelOpen final : public IOpenCallbackUI {
  public:
    HRESULT Open_CheckBreak() override { return E_ABORT; }
    HRESULT Open_SetTotal(const UInt64 *, const UInt64 *) override { return E_ABORT; }
    HRESULT Open_SetCompleted(const UInt64 *, const UInt64 *) override { return E_ABORT; }
    HRESULT Open_Finished() override { return E_ABORT; }
    HRESULT Open_CryptoGetTextPassword(BSTR *) override { return E_ABORT; }
};
} // namespace

class ArchiveOpenOptionsTests final : public QObject {
    Q_OBJECT
    QString module;
    UInt32 version = 0;
    void load(ArchiveBackend &backend) {
        QCOMPARE(backend.loadCodecCatalog(module, version), S_OK);
    }
    bool extract(ArchiveBackend &backend, const QString &archive, const QString &output,
                 const UString &type, bool excludeZip) {
        QtShellArchiveServices service(backend, nullptr);
        CObjectVector<COpenType> types;
        if (!ParseOpenTypes(service.Codecs(), type, types))
            return false;
        CIntVector excluded;
        if (excludeZip)
            excluded.Add(backend.codecCatalog()->formatIndex(QStringLiteral("zip")));
        UStringVector paths, fullPaths;
        paths.Add(archive.toStdWString().c_str());
        fullPaths = paths;
        NWildcard::CCensorNode censor;
        censor.Add_Wildcard();
        CExtractOptions options;
        options.OutputDir = us2fs(UString(QDir::toNativeSeparators(output).toStdWString().c_str()));
        options.OutDirMode = NExtractOutDirMode::k_Direct;
        options.YesToAll = true;
        CMyComPtr<IFolderArchiveExtractCallback> callback = new CExtractCallbackImp;
        auto *ui = static_cast<CExtractCallbackImp *>(callback.Interface());
        ui->YesToAll = true;
        ui->Init();
        bool displayed = false;
        FinalMessageDismissal dismiss;
        const HRESULT result =
            service.ExtractGUI(service.Codecs(), types, excluded, paths, fullPaths, censor, options,
                               nullptr, false, displayed, *ui, type);
        return result == S_OK && ui->IsOK();
    }
  private slots:
    void memoryInputUsesRequestedOpenOptions() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QByteArray payload("input bytes");
        const QString archive = SourceExtractFixture::makeArchive(fixture.path(), payload);
        QVERIFY(!archive.isEmpty());
        const QByteArray bytes = SourceExtractFixture::readFile(archive);
        auto *buffer = new CBufferInStream;
        CMyComPtr<IInStream> input = buffer;
        buffer->Buf.CopyFrom(reinterpret_cast<const Byte *>(bytes.constData()),
                             size_t(bytes.size()));
        buffer->Init();
        ArchiveBackend backend;
        load(backend);
        CObjectVector<COpenType> types;
        QVERIFY(backend.codecCatalog()->parseOpenTypes("zip", types));
        CIntVector excluded;
        COpenOptions options;
        options.types = &types;
        options.excludedFormats = &excluded;
        options.props = nullptr;
        options.stream = input;
        options.filePath = L"virtual 空 input.zip";
        QtArchiveBatchOpen opener(backend);
        opener.SetOpenType(UString(L"zip"));
        CArchiveLink opened;
        auto worker =
            std::async(std::launch::async, [&] { return opener.Open(opened, options, nullptr); });
        const auto cancel = qScopeGuard([&] {
            if (worker.valid() &&
                worker.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
                backend.cancelAndWait();
        });
        QTRY_VERIFY_WITH_TIMEOUT(
            worker.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready, 10000);
        QCOMPARE(worker.get(), S_OK);
        QVERIFY(opened.IsOpen);
        NWindows::NCOM::CPropVariant path, size;
        QCOMPARE(opened.GetArc()->Archive->GetProperty(0, kpidPath, &path), S_OK);
        QCOMPARE(QDir::fromNativeSeparators(QString::fromWCharArray(path.bstrVal)),
                 SourceExtractFixture::kName);
        QCOMPARE(opened.GetArc()->Archive->GetProperty(0, kpidSize, &size), S_OK);
        QCOMPARE(size.vt, VARTYPE(VT_UI8));
        QCOMPARE(size.uhVal.QuadPart, UInt64(payload.size()));
        QVERIFY(opened.GetArc()->IsReadOnly);
    }
    void initTestCase() {
#ifndef SEVENZIP_CODEC_MODULE
        QSKIP("Official codec module is unavailable");
#else
        QVERIFY(!QStandardPaths::findExecutable("7z").isEmpty());
        module = QString::fromUtf8(SEVENZIP_CODEC_MODULE);
        ArchiveCodecCatalog probe(module, 0);
        QCOMPARE(probe.result(), S_OK);
        version = probe.version();
        QVERIFY(version);
#endif
    }
    void cancellingStandaloneArchiveTestReportsNoChanges() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString archive = fixture.filePath("encrypted.zip");
        QVERIFY(NativeArchiveFixture::Write(archive, {{"payload.txt", "payload"}}, "zip",
                                            QStringLiteral("test password")));
        const QByteArray before = SourceExtractFixture::readFile(archive);
        ArchiveBackend backend;
        load(backend);
        QtShellArchiveServices service(backend, nullptr);
        UStringVector paths;
        paths.Add(archive.toStdWString().c_str());

        int prompts = 0;
        QStringList summaries;
        QTimer responder;
        connect(&responder, &QTimer::timeout, this, [&] {
            auto *modal = QApplication::activeModalWidget();
            if (auto *password = dynamic_cast<CPasswordDialog *>(modal)) {
                ++prompts;
                password->reject();
            } else if (auto *report = TestProgressDialog::ResultWindow(modal)) {
                summaries.append(report->findChild<QLabel *>("IDT_PROGRESS_STATUS")->text());
                TestProgressDialog::CloseResult(*report);
            } else if (auto *message = qobject_cast<QMessageBox *>(modal)) {
                summaries.append(message->text());
                message->accept();
            }
        });
        responder.start(5);
        QCOMPARE(TestArchives(paths, false, service), E_ABORT);
        QCOMPARE(prompts, 1);
        QCOMPARE(summaries, QStringList{"The operation was cancelled."});
        QCOMPARE(SourceExtractFixture::readFile(archive), before);
        QCOMPARE(QDir(fixture.path()).entryList(QDir::Files), QStringList{"encrypted.zip"});
    }
    void excludedZipIsNotOpened_data() {
        QTest::addColumn<bool>("excluded");
        QTest::newRow("allowed") << false;
        QTest::newRow("excluded") << true;
    }
    void excludedZipIsNotOpened() {
        QFETCH(bool, excluded);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QByteArray bytes("archive payload\0", 16);
        const QString archive = SourceExtractFixture::makeArchive(fixture.path(), bytes);
        QVERIFY(!archive.isEmpty());
        const QByteArray before = SourceExtractFixture::readFile(archive);
        ArchiveBackend backend;
        load(backend);
        const QString output = fixture.filePath(QStringLiteral("выход 空"));
        QCOMPARE(extract(backend, archive, output, UString(), excluded), !excluded);
        if (excluded) {
            QVERIFY(!QFileInfo::exists(QDir(output).filePath(SourceExtractFixture::kName)));
        } else
            QCOMPARE(
                SourceExtractFixture::readFile(QDir(output).filePath(SourceExtractFixture::kName)),
                bytes);
        QCOMPARE(SourceExtractFixture::readFile(archive), before);
    }
    void originalTypeChain() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QByteArray bytes("nested source types");
        QVERIFY(
            SourceExtractFixture::writeFile(fixture.filePath(SourceExtractFixture::kName), bytes));
        QProcess engine;
        engine.setWorkingDirectory(fixture.path());
        const QString name = QStringLiteral("цепочка 空 with spaces.zip");
        engine.start(QStandardPaths::findExecutable("7z"),
                     {"a", "-tzip", "-v128b", name, SourceExtractFixture::kName});
        QVERIFY(engine.waitForFinished(10000));
        QCOMPARE(engine.exitCode(), 0);
        const QString archive = fixture.filePath(name + ".001");
        engine.start(QStandardPaths::findExecutable("7z"), {"l", "-tzip.split", archive});
        QVERIFY(engine.waitForFinished(10000));
        QVERIFY2(engine.exitCode() == 0, engine.readAll().constData());
        QMap<QString, QByteArray> before;
        const auto volumes = QDir(fixture.path()).entryList({name + ".*"}, QDir::Files);
        QVERIFY(volumes.size() > 1);
        for (const auto &volume : volumes)
            before.insert(volume, SourceExtractFixture::readFile(fixture.filePath(volume)));
        ArchiveBackend backend;
        load(backend);
        const QString output = fixture.filePath("typed output 空");
        QVERIFY(extract(backend, archive, output, UString("zip.split"), false));
        QCOMPARE(SourceExtractFixture::readFile(QDir(output).filePath(SourceExtractFixture::kName)),
                 bytes);
        for (auto i = before.cbegin(); i != before.cend(); ++i)
            QCOMPARE(SourceExtractFixture::readFile(fixture.filePath(i.key())), i.value());
    }
    void originalOpenPropertiesAndCancellation() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString archive = fixture.filePath(QStringLiteral("архив 空.rar"));
        const QByteArray bytes = RarMemoryFixture::archive(0);
        QVERIFY(SourceExtractFixture::writeFile(archive, bytes));
        ArchiveBackend backend;
        load(backend);
        CObjectVector<COpenType> types;
        CIntVector excluded;
        CObjectVector<CProperty> properties;
        COpenOptions options;
        options.types = &types;
        options.excludedFormats = &excluded;
        options.props = &properties;
        options.filePath = archive.toStdWString().c_str();
        std::shared_ptr<ArchiveNativeOpen> reader;
        QCOMPARE(backend.openNativeArchive(archive, {}, nullptr, reader, &options), S_OK);
        QVERIFY(reader);
        reader.reset();
        CProperty &property = properties.AddNew();
        property.Name = L"memx";
        property.Value = L"invalid";
        QCOMPARE(backend.openNativeArchive(archive, {}, nullptr, reader, &options), E_INVALIDARG);
        QVERIFY(!reader);
        properties.Clear();
        CancelOpen cancel;
        QCOMPARE(backend.openNativeArchive(archive, {}, &cancel, reader, &options), E_ABORT);
        QVERIFY(!reader);
        QCOMPARE(SourceExtractFixture::readFile(archive), bytes);
    }
    void batchRetainsSourceVolumeAccounting_data() {
        QTest::addColumn<int>("outcome");
        QTest::newRow("native-volume-accounting") << 0;
        QTest::newRow("missing-first-volume") << 1;
        QTest::newRow("cancel-native-open") << 2;
    }
    void batchRetainsSourceVolumeAccounting() {
        QFETCH(int, outcome);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QByteArray bytes(2048, 'x');
        QVERIFY(
            SourceExtractFixture::writeFile(fixture.filePath(SourceExtractFixture::kName), bytes));
        const QString name = QStringLiteral("тома 空 with spaces.zip");
        QProcess engine;
        engine.setWorkingDirectory(fixture.path());
        engine.start(QStandardPaths::findExecutable("7z"),
                     {"a", "-tzip", "-mx0", "-v128b", name, SourceExtractFixture::kName});
        QVERIFY(engine.waitForFinished(10000));
        QCOMPARE(engine.exitCode(), 0);
        const QString archive = fixture.filePath(name + ".001");
        NativeOnlyBackend backend;
        load(backend);
        const auto catalog = backend.codecCatalog();
        CArchiveLink original;
        QCOMPARE(catalog->openArchive(original, archive, "zip.split", nullptr), S_OK);
        QVERIFY(!original.VolumePaths.IsEmpty());
        const UStringVector expectedPaths = original.VolumePaths;
        const UInt64 expectedSize = original.VolumesSize;
        QCOMPARE(original.Close(), S_OK);
        original.Release();
        if (outcome == 1)
            QVERIFY(QFile::remove(archive));
        QMap<QString, QByteArray> before;
        for (const QString &part : QDir(fixture.path()).entryList({name + ".*"}, QDir::Files))
            before.insert(part, SourceExtractFixture::readFile(fixture.filePath(part)));
        CObjectVector<COpenType> types;
        QVERIFY(catalog->parseOpenTypes("zip.split", types));
        COpenOptions options;
        options.types = &types;
        options.props = nullptr;
        options.filePath = archive.toStdWString().c_str();
        QtArchiveBatchOpen opener(backend);
        opener.SetOpenType(UString("zip.split"));
        CArchiveLink adapted;
        CancelOpen cancelled;
        auto worker = std::async(std::launch::async, [&] {
            return opener.Open(adapted, options, outcome == 2 ? &cancelled : nullptr);
        });
        const auto cancel = qScopeGuard([&] {
            if (worker.valid() &&
                worker.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
                backend.cancelAndWait();
        });
        QTRY_VERIFY_WITH_TIMEOUT(
            worker.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready, 10000);
        const HRESULT result = worker.get();
        if (outcome == 0) {
            QCOMPARE(result, S_OK);
            QCOMPARE(adapted.VolumePaths.Size(), expectedPaths.Size());
            FOR_VECTOR(i, expectedPaths)
            QCOMPARE(QString::fromWCharArray(adapted.VolumePaths[i].Ptr()),
                     QString::fromWCharArray(expectedPaths[i].Ptr()));
            QCOMPARE(adapted.VolumesSize, expectedSize);
            QCOMPARE(backend.listingCalls, 0);
        } else {
            if (outcome == 2)
                QCOMPARE(result, E_ABORT);
            else
                QVERIFY(result != S_OK);
            QVERIFY(!adapted.IsOpen);
            QCOMPARE(backend.listingCalls, 0);
        }
        for (auto i = before.cbegin(); i != before.cend(); ++i)
            QCOMPARE(SourceExtractFixture::readFile(fixture.filePath(i.key())), i.value());
        QCOMPARE(SourceExtractFixture::readFile(fixture.filePath(SourceExtractFixture::kName)),
                 bytes);
        QVERIFY(!backend.busy());
    }
    void nativeSingleArchiveRetainsWriterWithoutCliListing() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QByteArray bytes(2048, 'x');
        const QString archive = SourceExtractFixture::makeArchive(fixture.path(), bytes);
        QVERIFY(!archive.isEmpty());
        const QByteArray before = SourceExtractFixture::readFile(archive);
        NativeOnlyBackend backend;
        load(backend);
        CObjectVector<COpenType> types;
        QVERIFY(backend.codecCatalog()->parseOpenTypes("zip", types));
        COpenOptions options;
        options.types = &types;
        options.props = nullptr;
        options.filePath = archive.toStdWString().c_str();
        QtArchiveBatchOpen opener(backend);
        opener.SetOpenType(UString("zip"));
        CArchiveLink adapted;
        auto worker =
            std::async(std::launch::async, [&] { return opener.Open(adapted, options, nullptr); });
        const auto cancel = qScopeGuard([&] {
            if (worker.valid() &&
                worker.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
                backend.cancelAndWait();
        });
        QTRY_VERIFY_WITH_TIMEOUT(
            worker.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready, 10000);
        QCOMPARE(worker.get(), S_OK);
        QVERIFY(adapted.VolumePaths.IsEmpty());
        QCOMPARE(adapted.VolumesSize, UInt64(0));
        QVERIFY(ArchiveUpdateEnabled(adapted.GetArc()->Archive));
        QVERIFY(!adapted.GetArc()->IsReadOnly);
        QCOMPARE(SourceExtractFixture::readFile(archive), before);
    }
};
QTEST_MAIN(ArchiveOpenOptionsTests)
#include "archive_open_options_tests.moc"
