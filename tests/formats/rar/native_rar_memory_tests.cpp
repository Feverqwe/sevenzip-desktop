#include "archive/agent/Agent.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/operations/ZipRegistry.h"
#include "archive/resources/qtarchivebatchopen.h"
#include "archive/resources/qtarchivefileresource.h"
#include "archive/resources/qtarchivefoldermanager.h"
#include "archive/resources/qtarchiveproperties.h"
#include "filemanager/ExtractCallback.h"
#include "filemanager/providers/FileFolderPluginOpen.h"
#include "filemanager/providers/qtlocationfolder.h"
#include "formats/rar/support/rar_memory_fixture.h"
#include "gui/ExtractGUI.h"
#include "ui/formatting/qtregionalformat.h"
#include <QApplication>
#include <QFile>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

namespace {
class CObservedExtractCallback final : public IFolderArchiveExtractCallback,
                                       public IArchiveRequestMemoryUseCallback,
                                       public CMyUnknownImp {
    Z7_COM7F_IMF(QueryInterface(REFIID iid, void **outObject)) override {
        if (iid == IID_IUnknown || iid == IID_IFolderArchiveExtractCallback)
            *outObject = static_cast<IFolderArchiveExtractCallback *>(this);
        else if (iid == IID_IArchiveRequestMemoryUseCallback)
            *outObject = static_cast<IArchiveRequestMemoryUseCallback *>(this);
        else
            return owner_->QueryInterface(iid, outObject);
        AddRef();
        return S_OK;
    }
    Z7_COM_ADDREF_RELEASE
    CMyComPtr<IFolderArchiveExtractCallback> owner_;
    CMyComPtr<IArchiveRequestMemoryUseCallback> memory_;

  public:
    CExtractCallbackImp *Source;
    CObservedExtractCallback() {
        Source = new CExtractCallbackImp;
        owner_ = Source;
        owner_.QueryInterface(IID_IArchiveRequestMemoryUseCallback, &memory_);
    }
    Z7_COM7F_IMF(SetTotal(UInt64 size)) override { return owner_->SetTotal(size); }
    Z7_COM7F_IMF(SetCompleted(const UInt64 *size)) override { return owner_->SetCompleted(size); }
    Z7_COM7F_IMF(AskOverwrite(const wchar_t *existName, const FILETIME *existTime,
                              const UInt64 *existSize, const wchar_t *newName,
                              const FILETIME *newTime, const UInt64 *newSize, Int32 *answer))
    override {
        return owner_->AskOverwrite(existName, existTime, existSize, newName, newTime, newSize,
                                    answer);
    }
    Z7_COM7F_IMF(PrepareOperation(const wchar_t *name, Int32 isFolder, Int32 mode,
                                  const UInt64 *position))
    override { return owner_->PrepareOperation(name, isFolder, mode, position); }
    Z7_COM7F_IMF(MessageError(const wchar_t *message)) override {
        return owner_->MessageError(message);
    }
    Z7_COM7F_IMF(SetOperationResult(Int32 result, Int32 encrypted)) override {
        return owner_->SetOperationResult(result, encrypted);
    }
    UInt64 Required = 0, Allowed = 0;
    UInt32 Flags = 0, Answer = 0;
    unsigned Requests = 0;
    Z7_COM7F_IMF(RequestMemoryUse(UInt32 flags, UInt32 indexType, UInt32 index, const wchar_t *path,
                                  UInt64 required, UInt64 *allowed, UInt32 *answer))
    override {
        ++Requests;
        Required = required;
        Allowed = *allowed;
        Flags = flags;
        const HRESULT result =
            memory_->RequestMemoryUse(flags, indexType, index, path, required, allowed, answer);
        Answer = *answer;
        return result;
    }
};
class CRarExtractWorker final : public CProgressThreadVirt {
    CMyComPtr<IArchiveFolder> folder_;
    QString output_, archive_;
    bool selected_, test_;
    CMyComPtr<IFolderArchiveExtractCallback> owner_;
    HRESULT ProcessVirt() override {
        RINOK(static_cast<IExtractCallbackUI *>(Callback->Source)
                  ->BeforeOpen(archive_.toStdWString().c_str(), test_))
        const UInt32 indices[] = {0, 1};
        return folder_->Extract(
            indices, selected_ ? 1 : 2, false, false, NExtract::NPathMode::kFullPaths,
            NExtract::NOverwriteMode::kOverwrite, output_.toStdWString().c_str(), test_, owner_);
    }

  public:
    CObservedExtractCallback *Callback;
    CRarExtractWorker(IArchiveFolder *folder, const QString &archive, const QString &output,
                      bool selected, bool test)
        : folder_(folder), output_(output), archive_(archive), selected_(selected), test_(test) {
        Callback = new CObservedExtractCallback;
        owner_ = Callback;
        Callback->Source->ProgressDialog = this;
        Callback->Source->Init();
        DisableUserQuestions = true;
    }
};
QByteArray read(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
} // namespace

class NativeRarMemoryTests final : public QObject {
    Q_OBJECT
    QString engine_, module_;
    quint32 version_ = 0;
  private slots:
    void initTestCase() {
#ifndef SEVENZIP_CODEC_MODULE
        QSKIP("Official module unavailable; actual RAR memory GUI consumer unverified");
#else
        engine_ = QStandardPaths::findExecutable("7z");
        QVERIFY(!engine_.isEmpty());
        module_ = QString::fromUtf8(SEVENZIP_CODEC_MODULE);
        ArchiveBackend backend(nullptr, engine_);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(module_));
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY(ready[0][0].toBool());
        version_ = backend.codecCatalog()->version();
#endif
    }
    void actualHandlerCallsSourceGui_data() {
        QTest::addColumn<unsigned>("algorithm");
        QTest::addColumn<bool>("selected");
        QTest::addColumn<bool>("test");
        QTest::addColumn<bool>("raiseLimit");
        QTest::addColumn<bool>("corrupt");
        for (unsigned algorithm : {0u, 1u})
            for (bool selected : {false, true})
                for (bool test : {false, true})
                    for (bool raiseLimit : {false, true})
                        QTest::newRow(qPrintable(QString("%1-%2-%3-%4")
                                                     .arg(algorithm)
                                                     .arg(selected)
                                                     .arg(test)
                                                     .arg(raiseLimit)))
                            << algorithm << selected << test << raiseLimit << false;
        for (unsigned algorithm : {0u, 1u})
            for (bool test : {false, true})
                QTest::newRow(qPrintable(QString("crc-error-%1-%2").arg(algorithm).arg(test)))
                    << algorithm << false << test << true << true;
    }
    void sourceCompressedDecoderMatchesIndependentEngine() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString archive = fixture.filePath("compressed.rar");
        QVERIFY(QFile::copy(QString::fromUtf8(RAR_COMPRESSED_FIXTURE), archive));
        const QByteArray original = read(archive);
        ArchiveBackend backend(nullptr, engine_);
        QCOMPARE(backend.loadCodecCatalog(module_, version_), S_OK);
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        CFfpOpen opening(manager);
        QCOMPARE(opening.OpenFileFolderPlugin(nullptr, us2fs(archive.toStdWString().c_str()),
                                              UString(L"Rar5"), nullptr),
                 S_OK);
        auto *folder = dynamic_cast<CAgentFolder *>(opening.Folder.Interface());
        QVERIFY(folder);
        QCOMPARE(folder->LoadItems(), S_OK);
        auto *resource = dynamic_cast<QtArchiveFileResource *>(folder->GetEngineResource());
        QVERIFY(resource && resource->HasNativeExtractService());
        CMyComPtr<IArchiveFolder> archiveFolder;
        QCOMPARE(opening.Folder.QueryInterface(IID_IArchiveFolder, &archiveFolder), S_OK);
        const QString output = fixture.filePath("source-output");
        CRarExtractWorker worker(archiveFolder, archive, output, true, false);
        auto transport = resource->CreateProgressTransport(worker, true);
        QCOMPARE(worker.Create(L"Source RAR compressed decoder", nullptr), S_OK);
        QCOMPARE(worker.Result, S_OK);
        QVERIFY(worker.Callback->Source->IsOK());
        QCOMPARE(worker.Callback->Requests, 0u);
        const QByteArray bytes = read(output + "/test.bin");
        QCOMPARE(bytes.size(), 1200);
        QCOMPARE(CrcCalc(bytes.constData(), size_t(bytes.size())), UInt32(0x7CCA70CD));
        QProcess independent;
        const QString reference = fixture.filePath("independent-output");
        const QString bsdtar = QStandardPaths::findExecutable("bsdtar");
        if (!bsdtar.isEmpty()) {
            QVERIFY(QDir().mkpath(reference));
            independent.start(bsdtar, {"-xf", archive, "-C", reference});
            QVERIFY(independent.waitForFinished(10000));
            QCOMPARE(independent.exitCode(), 0);
            QCOMPARE(bytes, read(reference + "/test.bin"));
        } else
            qInfo("Independent libarchive extraction unavailable; source size/CRC checked");
        QCOMPARE(read(archive), original);
        // A lost index contract must refuse the actual source consumer, not
        // silently run the console decoder with different memory decisions.
        UInt32 itemCount = 0;
        QCOMPARE(opening.Folder->GetNumberOfItems(&itemCount), S_OK);
        QList<ArchiveEntry> otherEntries;
        for (UInt32 index = 0; index < itemCount; ++index) {
            ArchiveEntry entry;
            entry.path = QString("another archive's file-%1.bin").arg(index);
            otherEntries.append(entry);
        }
        CMyComPtr<IFolderFolder> unrelated;
        QCOMPARE(CreateArchiveMetadata(otherEntries, &unrelated), S_OK);
        CMyComPtr2_Create<IFolderArcProps, CAgent> mismatchedAgent;
        QCOMPARE(mismatchedAgent->Init(unrelated, UString(L"rar"), resource), S_OK);
        CMyComPtr<IFolderFolder> mismatchedFolder;
        QCOMPARE(mismatchedAgent->BindToRootFolder(&mismatchedFolder), S_OK);
        auto *mismatched = dynamic_cast<CAgentFolder *>(mismatchedFolder.Interface());
        QVERIFY(mismatched);
        QVERIFY(resource->HasNativeRarReader());
        CMyComPtr<IArchiveFolder> mismatchedArchive;
        QCOMPARE(mismatchedFolder.QueryInterface(IID_IArchiveFolder, &mismatchedArchive),
                 E_NOINTERFACE);
        CMyComPtr<IArchiveFolder> retainedOriginal;
        QCOMPARE(opening.Folder.QueryInterface(IID_IArchiveFolder, &retainedOriginal), S_OK);
        // A caller that already has the interface still cannot bypass the
        // extraction boundary's native-index check.
        mismatchedArchive = static_cast<IArchiveFolder *>(mismatched);
        const QString refusedOutput = fixture.filePath("refused-output");
        CRarExtractWorker refused(mismatchedArchive, archive, refusedOutput, true, false);
        auto refusedTransport = resource->CreateProgressTransport(refused, true);
        QCOMPARE(refused.Create(L"Source RAR index refusal", nullptr), S_OK);
        QVERIFY(refused.Result != S_OK);
        QVERIFY(!QFileInfo::exists(refusedOutput + "/test.bin"));
        QCOMPARE(read(archive), original);
    }
    void batchCanRetryAfterChangingSettings() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        ArchiveBackend backend(nullptr, engine_);
        QCOMPARE(backend.loadCodecCatalog(module_, version_), S_OK);
        UStringVector paths, fullPaths;
        for (unsigned algorithm : {0u, 1u}) {
            const QString archive = fixture.filePath(QString("batch-%1.rar").arg(algorithm));
            QFile file(archive);
            QVERIFY(file.open(QIODevice::WriteOnly));
            const auto bytes = RarMemoryFixture::archive(algorithm);
            QCOMPARE(file.write(bytes), qint64(bytes.size()));
            paths.Add(UString(archive.toStdWString().c_str()));
            fullPaths.Add(paths.Back());
        }
        const QString settings = fixture.filePath("settings.ini");
        QTimer responder;
        connect(&responder, &QTimer::timeout, this, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                box->accept();
            else if (auto *progress =
                         dynamic_cast<CProgressDialog *>(QApplication::activeModalWidget())) {
                auto *pause = progress->findChild<QPushButton *>("IDB_PAUSE");
                auto *close = progress->findChild<QPushButton *>("IDCANCEL");
                if (pause && close && !pause->isVisible())
                    close->click();
            }
        });
        responder.start(2);
        auto run = [&](const QString &output) {
            CCodecs codecs;
            CObjectVector<COpenType> types;
            CIntVector excluded;
            NWildcard::CCensorNode censor;
            censor.Add_Wildcard();
            CExtractOptions options;
            options.OutputDir = us2fs(output.toStdWString().c_str());
            options.OutDirMode = NExtractOutDirMode::k_Direct;
            options.OverwriteMode = NExtract::NOverwriteMode::kOverwrite;
            CProperty property;
            property.Name = L"memx";
            property.Value = L"1m";
            options.Properties.Add(property);
            CMyComPtr<IFolderArchiveExtractCallback> owner = new CExtractCallbackImp;
            auto *callback = static_cast<CExtractCallbackImp *>(owner.Interface());
            callback->MultiArcMode = true;
            callback->SettingsPath = settings;
            QtArchiveBatchOpen open(backend);
            bool displayed = false;
            return ExtractGUI(&codecs, types, excluded, paths, fullPaths, censor, options, nullptr,
                              false, displayed, callback, &open, &open);
        };
        const QString output = fixture.filePath("output");
        QVERIFY(run(output) != E_ABORT);
        QVERIFY(!QFileInfo::exists(output + "/" + RarMemoryFixture::name()));
        QCOMPARE(NExtract::Read_LimitGB(settings), UInt32(-1));
        NExtract::Save_LimitGB(settings, 1);
        QCOMPARE(run(fixture.filePath("retried-output")), S_OK);
        QCOMPARE(read(fixture.filePath("retried-output/") + RarMemoryFixture::name()),
                 RarMemoryFixture::payload());
        for (unsigned algorithm : {0u, 1u})
            QCOMPARE(read(fixture.filePath(QString("batch-%1.rar").arg(algorithm))),
                     RarMemoryFixture::archive(algorithm));
    }
    void actualHandlerCallsSourceGui() {
        QFETCH(unsigned, algorithm);
        QFETCH(bool, selected);
        QFETCH(bool, test);
        QFETCH(bool, raiseLimit);
        QFETCH(bool, corrupt);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString archive = fixture.filePath("fixture.rar");
        QFile file(archive);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QByteArray original = RarMemoryFixture::archive(algorithm);
        if (corrupt) {
            const auto offset = original.indexOf(RarMemoryFixture::payload());
            QVERIFY(offset >= 0);
            original[offset] = char(original[offset] ^ 1);
        }
        QCOMPARE(file.write(original), qint64(original.size()));
        file.close();
        QProcess independent;
        independent.start(engine_, {"t", "--", archive});
        QVERIFY(independent.waitForFinished(10000));
        QCOMPARE(independent.exitCode(), corrupt ? 2 : 0);
        ArchiveBackend backend(nullptr, engine_);
        QCOMPARE(backend.loadCodecCatalog(module_, version_), S_OK);
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        CFfpOpen opening(manager);
        QCOMPARE(opening.OpenFileFolderPlugin(nullptr, us2fs(archive.toStdWString().c_str()),
                                              UString(L"Rar5"), nullptr),
                 S_OK);
        auto *folder = dynamic_cast<CAgentFolder *>(opening.Folder.Interface());
        QVERIFY(folder);
        QCOMPARE(folder->LoadItems(), S_OK);
        auto *resource = dynamic_cast<QtArchiveFileResource *>(folder->GetEngineResource());
        QVERIFY(resource && resource->HasNativeExtractService());
        // Public native handler option, not a synthetic RequestMemoryUse call.
        CMyComPtr<ISetProperties> properties;
        QCOMPARE(resource->NativeItemProperties()->QueryInterface(IID_ISetProperties,
                                                                  (void **)&properties),
                 S_OK);
        const wchar_t *property = L"memx";
        NWindows::NCOM::CPropVariant limit(UInt64(1) << 20);
        QCOMPARE(properties->SetProperties(&property, &limit, 1), S_OK);
        CMyComPtr<IArchiveFolder> archiveFolder;
        QCOMPARE(opening.Folder.QueryInterface(IID_IArchiveFolder, &archiveFolder), S_OK);
        const QString output = fixture.filePath("output");
        CRarExtractWorker worker(archiveFolder, archive, output, selected, test);
        worker.Callback->Source->SettingsPath = fixture.filePath("settings.ini");
        auto transport = resource->CreateProgressTransport(worker, true);
        if (raiseLimit)
            NExtract::Save_LimitGB(worker.Callback->Source->SettingsPath, 1);
        QCOMPARE(worker.Create(L"Source RAR memory", nullptr), S_OK);
        QCOMPARE(worker.Callback->Requests, 1u);
        QCOMPARE(worker.Callback->Required, UInt64(2) << 20);
        QCOMPARE(worker.Callback->Allowed, UInt64(1) << 20);
        QVERIFY(worker.Callback->Flags & NRequestMemoryUseFlags::k_MLimit_Exceeded);
        QVERIFY(worker.Callback->Flags & NRequestMemoryUseFlags::k_AllowedSize_WasForced);
        QCOMPARE(worker.Result, raiseLimit ? S_OK : E_OUTOFMEMORY);
        QCOMPARE(read(archive), original);
        QCOMPARE(worker.Callback->Source->IsOK(), raiseLimit && !corrupt);
        if (!raiseLimit) {
            QStringList details;
            FOR_VECTOR(i, worker.Sync.Messages)
            details.append(QString::fromWCharArray(worker.Sync.Messages[i]));
            const QString report = details.join('\n');
            QVERIFY(report.contains(QtRegionalFormat::exactBytes(UInt64(2) << 20)));
            QVERIFY(report.contains(QtRegionalFormat::exactBytes(UInt64(1) << 20)));
        }
        if (raiseLimit && !test) {
            if (!corrupt)
                QCOMPARE(read(output + "/" + RarMemoryFixture::name()),
                         RarMemoryFixture::payload());
            QCOMPARE(QFileInfo::exists(output + "/other.txt"), !selected);
        } else
            QVERIFY(QDir(output).entryList(QDir::Files).isEmpty());
    }
};
QTEST_MAIN(NativeRarMemoryTests)
#include "native_rar_memory_tests.moc"
