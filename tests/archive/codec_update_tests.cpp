#include "7zip/Common/FileStreams.h"
#include "archive/agent/Agent.h"
#include "archive/agent/UpdateCallbackAgent.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/resources/archivenativeopen.h"
#include "archive/resources/qtarchivefileresource.h"
#include "archive/resources/qtarchivefoldermanager.h"
#include "archive/resources/qtarchiveproperties.h"
#include "archive/resources/qtarchivewritelease.h"
#include "archive/support/archive_backend_test_support.h"
#include "filemanager/UpdateCallback100.h"
#include "filemanager/providers/FileFolderPluginOpen.h"
#include "filemanager/providers/qtlocationfolder.h"
#include "fileops/native/FileDir.h"
#include <QApplication>
#include <QStandardPaths>

namespace {
QByteArray readFile(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
class CNativeUpdateFault final : public IFolderArchiveUpdateCallback,
                                 public ICompressProgressInfo,
                                 public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_2(IFolderArchiveUpdateCallback, ICompressProgressInfo)
    CMyComPtr<IFolderArchiveUpdateCallback> _callback;
    HRESULT _failure;
    bool &_observed;
    HRESULT CheckProgress(const UInt64 *completed) {
        if (!completed || !*completed)
            return S_OK;
        _observed = true;
        return _failure;
    }

  public:
    CNativeUpdateFault(IFolderArchiveUpdateCallback *callback, HRESULT failure, bool &observed)
        : _callback(callback), _failure(failure), _observed(observed) {}
    Z7_COM7F_IMF(SetTotal(UInt64 size)) override { return _callback->SetTotal(size); }
    Z7_COM7F_IMF(SetCompleted(const UInt64 *completed)) override {
        RINOK(CheckProgress(completed))
        return _callback->SetCompleted(completed);
    }
    Z7_COM7F_IMF(SetRatioInfo(const UInt64 *inSize, const UInt64 *)) override {
        return CheckProgress(inSize);
    }
    Z7_COM7F_IMF(CompressOperation(const wchar_t *name)) override {
        return _callback->CompressOperation(name);
    }
    Z7_COM7F_IMF(DeleteOperation(const wchar_t *name)) override {
        return _callback->DeleteOperation(name);
    }
    Z7_COM7F_IMF(OperationResult(Int32 result)) override {
        return _callback->OperationResult(result);
    }
    Z7_COM7F_IMF(UpdateErrorMessage(const wchar_t *message)) override {
        return _callback->UpdateErrorMessage(message);
    }
    Z7_COM7F_IMF(SetNumFiles(UInt64 count)) override { return _callback->SetNumFiles(count); }
};
class CCodecUpdateWorker final : public CProgressThreadVirt {
    CMyComPtr<IFolderOperations> _operations;
    QString _source;
    HRESULT ProcessVirt() override {
        CMyComPtr2_Create<IFolderArchiveUpdateCallback, CUpdateCallback100Imp> ui;
        ui->ProgressDialog = this;
        ui->Init();
        CMyComPtr<IFolderArchiveUpdateCallback> callback = ui.Interface();
        if (NativeFailure != S_OK)
            callback =
                new CNativeUpdateFault(ui.Interface(), NativeFailure, ObservedNativeProgress);
        if (!NewName.isEmpty()) {
            const auto name = NewName.toStdWString();
            return CreateFolder ? _operations->CreateFolder(name.c_str(), callback)
                                : _operations->Rename(0, name.c_str(), callback);
        }
        if (Delete) {
            const UInt32 selected = 0;
            return _operations->Delete(&selected, 1, callback);
        }
        if (UseSourcePlan) {
            const std::wstring parent = QFileInfo(_source).absolutePath().toStdWString();
            const std::wstring name = QFileInfo(_source).fileName().toStdWString();
            const wchar_t *names[] = {name.c_str()};
            return _operations->CopyFrom(0, parent.c_str(), names, 1, callback);
        }
        return _operations->CopyFromFile(0, _source.toStdWString().c_str(), callback);
    }

  public:
    bool UseSourcePlan = false;
    bool Delete = false;
    bool CreateFolder = false;
    QString NewName;
    HRESULT NativeFailure = S_OK;
    bool ObservedNativeProgress = false;
    CCodecUpdateWorker(IFolderOperations *operations, const QString &source)
        : _operations(operations), _source(source) {}
};
} // namespace

class CodecUpdateTests final : public QObject, protected ArchiveBackendTestSupport {
    Q_OBJECT
    QString engine_, module_;
    quint32 version_ = 0;
  private slots:
    void initTestCase() {
#ifndef SEVENZIP_CODEC_MODULE
        QSKIP("Official module unavailable; native writer transactions unverified");
#else
        engine_ = QStandardPaths::findExecutable("7z");
        if (engine_.isEmpty())
            QSKIP("Official engine unavailable; native writer transactions unverified");
        module_ = QString::fromUtf8(SEVENZIP_CODEC_MODULE);
        ArchiveBackend backend(nullptr, engine_);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(module_));
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY2(ready[0][0].toBool(), qPrintable(ready[0][1].toString()));
        version_ = backend.codecCatalog()->version();
#endif
    }
    void sourceCopyFromFilePublishesNewBytes_data() {
        QTest::addColumn<QString>("format");
        QTest::addColumn<QByteArray>("mode");
        QTest::addColumn<bool>("sourcePlan");
        QTest::addColumn<bool>("deleteOnly");
        QTest::addColumn<bool>("prefixed");
        for (const QString &format : {"7z", "zip", "gzip", "bzip2", "xz", "tar", "wim"})
            for (const QByteArray &mode :
                 {QByteArray("success"), QByteArray("fail"), QByteArray("cancel")})
                for (bool sourcePlan : {false, true})
                    QTest::newRow(qPrintable(format + QString::fromLatin1(mode) +
                                             (sourcePlan ? " plan" : " file")))
                        << format << mode << sourcePlan << false << false;
        for (const QString &format : {"bzip2", "xz"})
            QTest::newRow(qPrintable(format + " cancel paused delete"))
                << format << QByteArray("cancel") << false << true << false;
        for (const QByteArray &mode :
             {QByteArray("success"), QByteArray("fail"), QByteArray("cancel")})
            for (bool sourcePlan : {false, true})
                QTest::newRow(qPrintable("7z SFX " + QString::fromLatin1(mode) +
                                         (sourcePlan ? " plan" : " file")))
                    << QString("7z") << mode << sourcePlan << false << true;
    }
    void sourceCopyFromFilePublishesNewBytes() {
        QFETCH(QString, format);
        QFETCH(QByteArray, mode);
        QFETCH(bool, sourcePlan);
        QFETCH(bool, deleteOnly);
        QFETCH(bool, prefixed);
        QTemporaryDir fixture(QDir::tempPath() + "/codec update 空-XXXXXX");
        QVERIFY(fixture.isValid());
        QString name = QStringLiteral("исходник 空 with spaces.bin");
#ifdef Q_OS_WIN
        // Source GzHandler stores its filename using CP_ACP, not Unicode.
        if (format == "gzip")
            name = QStringLiteral("source with spaces.bin");
#endif
        const QString source = fixture.filePath(name);
        writeFile(source, "original");
        // Even handlers without a stored filename derive this name from the
        // unknown archive suffix through the source DefaultName implementation.
        const QString archive = fixture.filePath(name + ".unknown");
        QProcess creator;
        creator.setWorkingDirectory(fixture.path());
        creator.start(engine_, {"a", "-t" + format, "--", archive, name});
        QVERIFY(creator.waitForFinished(10000));
        QCOMPARE(creator.exitCode(), 0);
        ArchiveBackend backend(nullptr, engine_);
        QCOMPARE(backend.loadCodecCatalog(module_, version_), S_OK);
        QByteArray prefix;
        if (prefixed) {
            const auto sfx = backend.sfxModule();
            QVERIFY2(sfx.available(), qPrintable(sfx.error()));
            prefix = readFile(sfx.path());
            QVERIFY(!prefix.isEmpty());
            writeFile(archive, prefix + readFile(archive));
        }
        const QByteArray original = readFile(archive);
        QVERIFY(!original.isEmpty());
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        CFfpOpen opening(manager);
        QCOMPARE(opening.OpenFileFolderPlugin(nullptr, us2fs(archive.toStdWString().c_str()),
                                              UString(format.toStdWString().c_str()), nullptr),
                 S_OK);
        auto *folder = dynamic_cast<CAgentFolder *>(opening.Folder.Interface());
        QVERIFY(folder);
        auto *resource = dynamic_cast<QtArchiveFileResource *>(folder->GetEngineResource());
        QVERIFY(resource && resource->HasUpdateService());
        if (prefixed) {
            std::shared_ptr<ArchiveNativeOpen> reader;
            QCOMPARE(backend.openNativeArchive(archive, format, nullptr, reader), S_OK);
            QVERIFY(reader);
            QCOMPARE(reader->arc().ArcStreamOffset, UInt64(prefix.size()));
            QVERIFY(reader->arc().InStream);
            CArc presented;
            presented.Archive = folder->GetArchiveProperties();
            QCOMPARE(resource->ConfigureArc(presented), S_OK);
            QCOMPARE(presented.ArcStreamOffset, UInt64(prefix.size()));
            QVERIFY(presented.InStream);
            QVERIFY(!presented.ErrorInfo.ThereIsTail);
        }
        QVERIFY(resource->NativeItemProperties());
        QCOMPARE(folder->LoadItems(), S_OK);
        UInt32 count = 0;
        QCOMPARE(folder->GetNumberOfItems(&count), S_OK);
        QCOMPARE(count, UInt32(1));
        CMyComPtr<IFolderOperations> operations;
        QCOMPARE(opening.Folder.QueryInterface(IID_IFolderOperations, &operations), S_OK);
        const QByteArray replacement("changed\0binary bytes", 20);
        writeFile(source, replacement);
        if (sourcePlan) {
            CompressionOptions options;
            options.format = format;
            options.Level = format == "tar" || format == "wim" ? 0 : 5;
            resource->SetCompressionOptions(options);
        }
        CCodecUpdateWorker worker(operations, source);
        worker.UseSourcePlan = sourcePlan;
        worker.Delete = deleteOnly;
        if (mode != "success" && !deleteOnly)
            worker.NativeFailure = mode == "cancel" ? E_ABORT : E_FAIL;
        worker.DisableUserQuestions = true;
        auto transport = resource->CreateProgressTransport(worker);
        if (deleteOnly) {
            worker.Sync.Set_Paused(true);
            QTimer::singleShot(100, &worker, [&] { worker.Sync.Set_Stopped(true); });
        }
        QCOMPARE(worker.Create(L"Native codec update", nullptr), S_OK);
        // Publication refreshes the retained native metadata without a process.
        if (mode != "success") {
            QCOMPARE(worker.Result, mode == "cancel" ? E_ABORT : E_FAIL);
            QVERIFY(deleteOnly ? worker.Sync.Get_Stopped() : worker.ObservedNativeProgress);
            QCOMPARE(readFile(archive), original);
            QCOMPARE(readFile(source), replacement);
            QVERIFY(QDir(fixture.path())
                        .entryList({"*.tmp", ".sevenzip-*"}, QDir::Files | QDir::Hidden)
                        .isEmpty());
            return;
        }
        QCOMPARE(worker.Result, S_OK);
        QVERIFY(readFile(archive) != original);
        if (prefixed)
            QCOMPARE(readFile(archive).left(prefix.size()), prefix);
        QProcess extractor;
        extractor.start(engine_,
                        {"x", "-y", "-t" + format, "-o" + fixture.filePath("out"), "--", archive});
        QVERIFY(extractor.waitForFinished(10000));
        QCOMPARE(extractor.exitCode(), 0);
        QCOMPARE(readFile(QDir(fixture.filePath("out")).filePath(name)), replacement);
        QCOMPARE(readFile(source), replacement);
    }
    void anotherWriterBlocksNativeFolderUpdateUntilRelease() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath("payload.txt");
        const QString archive = fixture.filePath("target.zip");
        writeFile(source, "original bytes");
        QProcess creator;
        creator.start(engine_, {"a", "-tzip", "--", archive, source});
        QVERIFY(creator.waitForFinished(10000));
        QCOMPARE(creator.exitCode(), 0);
        const QByteArray before = readFile(archive);
        ArchiveBackend backend(nullptr, engine_);
        QCOMPARE(backend.loadCodecCatalog(module_, version_), S_OK);
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        CFfpOpen opening(manager);
        QCOMPARE(opening.OpenFileFolderPlugin(nullptr, us2fs(archive.toStdWString().c_str()),
                                              UString("zip"), nullptr),
                 S_OK);
        QCOMPARE(opening.Folder->LoadItems(), S_OK);
        auto *folder = dynamic_cast<CAgentFolder *>(opening.Folder.Interface());
        QVERIFY(folder);
        auto *resource = dynamic_cast<QtArchiveFileResource *>(folder->GetEngineResource());
        QVERIFY(resource);
        CMyComPtr<IFolderOperations> operations;
        QCOMPARE(opening.Folder.QueryInterface(IID_IFolderOperations, &operations), S_OK);
        writeFile(source, "replacement bytes");
        {
            const auto owner = QtArchiveWriteLease::Acquire({archive});
            QVERIFY2(owner.has_value(), owner ? "" : qPrintable(owner.error()));
            CCodecUpdateWorker worker(operations, source);
            worker.DisableUserQuestions = true;
            auto transport = resource->CreateProgressTransport(worker);
            QCOMPARE(worker.Create(L"Conflicting writer", nullptr), S_OK);
            QCOMPARE(worker.Result, E_FAIL);
            QCOMPARE(readFile(archive), before);
            QCOMPARE(readFile(source), QByteArray("replacement bytes"));
        }
        CCodecUpdateWorker retry(operations, source);
        retry.DisableUserQuestions = true;
        auto transport = resource->CreateProgressTransport(retry);
        QCOMPARE(retry.Create(L"Retry after writer release", nullptr), S_OK);
        QCOMPARE(retry.Result, S_OK);
        QProcess extractor;
        const QString output = fixture.filePath("verified");
        extractor.start(engine_, {"x", "-y", "-o" + output, "--", archive});
        QVERIFY(extractor.waitForFinished(10000));
        QCOMPARE(extractor.exitCode(), 0);
        QCOMPARE(readFile(QDir(output).filePath("payload.txt")), QByteArray("replacement bytes"));
    }
    void staleNativeFolderCannotOverwriteAnotherPublishedGeneration() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath("payload.txt");
        const QString other = fixture.filePath("other.txt");
        const QString archive = fixture.filePath("target.zip");
        const QString replacement = fixture.filePath("replacement.zip");
        writeFile(source, "original payload");
        writeFile(other, "other writer's member");
        QProcess creator;
        creator.start(engine_, {"a", "-tzip", "--", archive, source});
        QVERIFY(creator.waitForFinished(10000));
        QCOMPARE(creator.exitCode(), 0);
        ArchiveBackend backend(nullptr, engine_);
        QCOMPARE(backend.loadCodecCatalog(module_, version_), S_OK);
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        CFfpOpen opening(manager);
        QCOMPARE(opening.OpenFileFolderPlugin(nullptr, us2fs(archive.toStdWString().c_str()),
                                              UString("zip"), nullptr),
                 S_OK);
        QCOMPARE(opening.Folder->LoadItems(), S_OK);
        CMyComPtr<IFolderOperations> operations;
        QCOMPARE(opening.Folder.QueryInterface(IID_IFolderOperations, &operations), S_OK);
        auto *folder = dynamic_cast<CAgentFolder *>(opening.Folder.Interface());
        QVERIFY(folder);
        auto *resource = dynamic_cast<QtArchiveFileResource *>(folder->GetEngineResource());
        QVERIFY(resource);
        creator.start(engine_, {"a", "-tzip", "--", replacement, source, other});
        QVERIFY(creator.waitForFinished(10000));
        QCOMPARE(creator.exitCode(), 0);
        const QByteArray published = readFile(replacement);
        QVERIFY2(NWindows::NFile::NDir::MyPublishFile_with_Progress(
                     us2fs(replacement.toStdWString().c_str()),
                     us2fs(archive.toStdWString().c_str()), nullptr, true),
                 qPrintable(QString::number(::GetLastError())));
        writeFile(source, "changed by this view");
        CCodecUpdateWorker stale(operations, source);
        stale.DisableUserQuestions = true;
        {
            auto transport = resource->CreateProgressTransport(stale);
            QCOMPARE(stale.Create(L"Stale source generation", nullptr), S_OK);
        }
        QCOMPARE(stale.Result, E_FAIL);
        QCOMPARE(readFile(archive), published);
        // Refusal leaves the retained view usable; a fresh opening can update.
        QCOMPARE(opening.Folder->LoadItems(), S_OK);
        CFfpOpen refreshed(manager);
        QCOMPARE(refreshed.OpenFileFolderPlugin(nullptr, us2fs(archive.toStdWString().c_str()),
                                                UString("zip"), nullptr),
                 S_OK);
        QCOMPARE(refreshed.Folder->LoadItems(), S_OK);
        CMyComPtr<IFolderOperations> current;
        QCOMPARE(refreshed.Folder.QueryInterface(IID_IFolderOperations, &current), S_OK);
        auto *freshFolder = dynamic_cast<CAgentFolder *>(refreshed.Folder.Interface());
        QVERIFY(freshFolder);
        auto *freshResource =
            dynamic_cast<QtArchiveFileResource *>(freshFolder->GetEngineResource());
        QVERIFY(freshResource);
        CCodecUpdateWorker retry(current, source);
        retry.UseSourcePlan = true;
        retry.DisableUserQuestions = true;
        auto transport = freshResource->CreateProgressTransport(retry);
        QCOMPARE(retry.Create(L"Fresh source generation", nullptr), S_OK);
        QCOMPARE(retry.Result, S_OK);
        const QString output = fixture.filePath("verified");
        QProcess extractor;
        extractor.start(engine_, {"x", "-y", "-o" + output, "--", archive});
        QVERIFY(extractor.waitForFinished(10000));
        QCOMPARE(extractor.exitCode(), 0);
        QCOMPARE(readFile(QDir(output).filePath("payload.txt")),
                 QByteArray("changed by this view"));
        QCOMPARE(readFile(QDir(output).filePath("other.txt")), QByteArray("other writer's member"));
    }
    void unsafeNativeMutationDoesNotPublish_data() {
        QTest::addColumn<QString>("format");
        QTest::addColumn<QString>("path");
        QTest::addColumn<bool>("createFolder");
        for (const QString &format : {"7z", "zip", "tar"})
            for (const QString &path : {"../outside", "safe/../outside", "/absolute"})
                for (bool createFolder : {false, true})
                    QTest::newRow(
                        qPrintable(format + path + (createFolder ? " create" : " rename")))
                        << format << path << createFolder;
    }
    void unsafeNativeMutationDoesNotPublish() {
        QFETCH(QString, format);
        QFETCH(QString, path);
        QFETCH(bool, createFolder);
        QTemporaryDir fixture(QDir::tempPath() + "/native update 空-XXXXXX");
        QVERIFY(fixture.isValid());
        const QString name = QStringLiteral("исходник 空 with spaces.bin");
        writeFile(fixture.filePath(name), "unchanged file");
        const QString archive = fixture.filePath("archive." + format);
        QProcess creator;
        creator.setWorkingDirectory(fixture.path());
        creator.start(engine_, {"a", "-t" + format, "--", archive, name});
        QVERIFY(creator.waitForFinished(10000));
        QCOMPARE(creator.exitCode(), 0);
        const QByteArray original = readFile(archive);
        QVERIFY(!original.isEmpty());
        ArchiveBackend backend(nullptr, engine_);
        QCOMPARE(backend.loadCodecCatalog(module_, version_), S_OK);
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        CFfpOpen opening(manager);
        QCOMPARE(opening.OpenFileFolderPlugin(nullptr, us2fs(archive.toStdWString().c_str()),
                                              UString(format.toStdWString().c_str()), nullptr),
                 S_OK);
        auto *folder = dynamic_cast<CAgentFolder *>(opening.Folder.Interface());
        QVERIFY(folder);
        auto *resource = dynamic_cast<QtArchiveFileResource *>(folder->GetEngineResource());
        QVERIFY(resource && resource->HasNativeUpdateService());
        CMyComPtr<IFolderOperations> operations;
        QCOMPARE(opening.Folder.QueryInterface(IID_IFolderOperations, &operations), S_OK);
        CCodecUpdateWorker worker(operations, {});
        worker.NewName = path;
        worker.CreateFolder = createFolder;
        worker.DisableUserQuestions = true;
        auto transport = resource->CreateProgressTransport(worker);
        QCOMPARE(worker.Create(L"Native path validation", nullptr), S_OK);
        QCOMPARE(worker.Result, E_INVALIDARG);
        QCOMPARE(readFile(archive), original);
        QCOMPARE(readFile(fixture.filePath(name)), QByteArray("unchanged file"));
        QVERIFY(QDir(fixture.path())
                    .entryList({"*.tmp", ".sevenzip-*"}, QDir::Files | QDir::Hidden)
                    .isEmpty());
    }
    void metadataViewsKeepIndependentNativeIndexBindings() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString name = QStringLiteral("é 空.bin");
        writeFile(fixture.filePath(name), "original");
        const QString archive = fixture.filePath("archive.7z");
        QProcess creator;
        creator.setWorkingDirectory(fixture.path());
        creator.start(engine_, {"a", "-t7z", "--", archive, name});
        QVERIFY(creator.waitForFinished(10000));
        QCOMPARE(creator.exitCode(), 0);
        ArchiveBackend backend(nullptr, engine_);
        QCOMPARE(backend.loadCodecCatalog(module_, version_), S_OK);
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        CFfpOpen opening(manager);
        QCOMPARE(opening.OpenFileFolderPlugin(nullptr, us2fs(archive.toStdWString().c_str()),
                                              UString(L"7z"), nullptr),
                 S_OK);
        auto *folder = dynamic_cast<CAgentFolder *>(opening.Folder.Interface());
        QVERIFY(folder);
        auto *resource = dynamic_cast<QtArchiveFileResource *>(folder->GetEngineResource());
        QVERIFY(resource && resource->HasNativeUpdateService());
        NWindows::NCOM::CPropVariant stored;
        QCOMPARE(resource->NativeItemProperties()->GetProperty(0, kpidPath, &stored), S_OK);
        QCOMPARE(stored.vt, VARTYPE(VT_BSTR));
        const QString original = QString::fromWCharArray(stored.bstrVal);
        CMyComPtr<IInArchive> nativeView;
        QCOMPARE(CreateArchivePropertyTransport(nullptr, &nativeView, resource), S_OK);
        CMyComPtr<IFolderArcProps> archiveProperties;
        QCOMPARE(folder->GetFolderArcProps(&archiveProperties), S_OK);
        NWindows::NCOM::CPropVariant archivePath;
        QCOMPARE(archiveProperties->GetArcProp(0, kpidPath, &archivePath), S_OK);
        QCOMPARE(archivePath.vt, VARTYPE(VT_BSTR));
        const QString originalArchivePath = QString::fromWCharArray(archivePath.bstrVal);
        NWindows::NCOM::CPropVariant physicalSize;
        QCOMPARE(nativeView->GetArchiveProperty(kpidPhySize, &physicalSize), S_OK);
        QCOMPARE(physicalSize.vt, VARTYPE(VT_UI8));
        const UInt64 originalPhysicalSize = physicalSize.uhVal.QuadPart;
        ArchiveEntry entry;
        entry.path =
            original.normalized(original == original.normalized(QString::NormalizationForm_C)
                                    ? QString::NormalizationForm_D
                                    : QString::NormalizationForm_C);
        QVERIFY(entry.path != original);
        CMyComPtr<IFolderFolder> metadata;
        QCOMPARE(CreateArchiveMetadata({entry}, &metadata), S_OK);
        CMyComPtr<IInArchive> unrelatedView;
        QCOMPARE(CreateArchivePropertyTransport(metadata, &unrelatedView, resource), S_OK);
        NWindows::NCOM::CPropVariant value;
        QCOMPARE(nativeView->GetProperty(0, kpidPath, &value), S_OK);
        QCOMPARE(QString::fromWCharArray(value.bstrVal), original);
        QCOMPARE(unrelatedView->GetProperty(0, kpidPath, &value), S_OK);
        QCOMPARE(QString::fromWCharArray(value.bstrVal), entry.path);
        QVERIFY(ArchiveUpdateEnabled(nativeView));
        QVERIFY(!ArchiveUpdateEnabled(unrelatedView));
        QCOMPARE(unrelatedView->Close(), S_OK);
        const QString unrelated = entry.path;
        metadata.Release();
        entry.path = original;
        QCOMPARE(CreateArchiveMetadata({entry}, &metadata), S_OK);
        CMyComPtr<IInArchive> matchingView;
        QCOMPARE(CreateArchivePropertyTransport(metadata, &matchingView, resource), S_OK);
        QCOMPARE(matchingView->GetProperty(0, kpidPath, &value), S_OK);
        QCOMPARE(QString::fromWCharArray(value.bstrVal), original);
        QVERIFY(ArchiveUpdateEnabled(matchingView));
        QCOMPARE(unrelatedView->GetProperty(0, kpidPath, &value), S_OK);
        QCOMPARE(QString::fromWCharArray(value.bstrVal), unrelated);
        QVERIFY(!ArchiveUpdateEnabled(unrelatedView));
        QCOMPARE(nativeView->GetProperty(0, kpidPath, &value), S_OK);
        QCOMPARE(QString::fromWCharArray(value.bstrVal), original);

        const QString replacementName = QStringLiteral("replacement 空.bin");
        writeFile(fixture.filePath(replacementName), "replacement");
        const QString replacementArchive = fixture.filePath("replacement.7z");
        creator.start(engine_, {"a", "-t7z", "--", replacementArchive, replacementName});
        QVERIFY(creator.waitForFinished(10000));
        QCOMPARE(creator.exitCode(), 0);
        std::shared_ptr<ArchiveNativeOpen> replacement;
        QCOMPARE(backend.openNativeArchive(replacementArchive, "7z", nullptr, replacement), S_OK);
        resource->EnginePath = replacementArchive;
        auto retainedReader = resource->NativeReaderLease();
        resource->AttachNativeOpen(std::move(replacement));
        resource->UseNativeMetadata();
        QCOMPARE(archiveProperties->GetArcProp(0, kpidPath, &archivePath), S_OK);
        QCOMPARE(QString::fromWCharArray(archivePath.bstrVal), originalArchivePath);
        QCOMPARE(nativeView->GetArchiveProperty(kpidPhySize, &physicalSize), S_OK);
        QCOMPARE(physicalSize.vt, VARTYPE(VT_UI8));
        QCOMPARE(physicalSize.uhVal.QuadPart, originalPhysicalSize);
        retainedReader.reset();
        const HRESULT oldArchiveRead = archiveProperties->GetArcProp(0, kpidPath, &archivePath);
        QVERIFY(oldArchiveRead != S_OK ||
                (archivePath.vt == VT_BSTR &&
                 QString::fromWCharArray(archivePath.bstrVal) == originalArchivePath));
        const HRESULT oldPhysicalRead = nativeView->GetArchiveProperty(kpidPhySize, &physicalSize);
        QVERIFY(oldPhysicalRead != S_OK ||
                (physicalSize.vt == VT_UI8 && physicalSize.uhVal.QuadPart == originalPhysicalSize));
        const HRESULT oldRead = nativeView->GetProperty(0, kpidPath, &value);
        QVERIFY(oldRead != S_OK ||
                (value.vt == VT_BSTR && QString::fromWCharArray(value.bstrVal) == original));
        CMyComPtr<IInArchive> replacementView;
        QCOMPARE(CreateArchivePropertyTransport(nullptr, &replacementView, resource), S_OK);
        QCOMPARE(replacementView->GetProperty(0, kpidPath, &value), S_OK);
        QCOMPARE(QString::fromWCharArray(value.bstrVal), replacementName);
        QCOMPARE(nativeView->Close(), S_OK);
        QCOMPARE(replacementView->GetProperty(0, kpidPath, &value), S_OK);
        QCOMPARE(QString::fromWCharArray(value.bstrVal), replacementName);
    }
    void sourcePropertiesReachWriterAndResetAfterUpdate() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString name = QStringLiteral("параметр 空.bin");
        const QString source = fixture.filePath(name);
        const QString archive = fixture.filePath("properties.zip");
        writeFile(source, "initial");
        QProcess creator;
        creator.setWorkingDirectory(fixture.path());
        creator.start(engine_, {"a", "-tzip", "--", archive, name});
        QVERIFY(creator.waitForFinished(10000));
        QCOMPARE(creator.exitCode(), 0);
        ArchiveBackend backend(nullptr, engine_);
        QCOMPARE(backend.loadCodecCatalog(module_, version_), S_OK);
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        CFfpOpen opening(manager);
        QCOMPARE(opening.OpenFileFolderPlugin(nullptr, us2fs(archive.toStdWString().c_str()),
                                              UString(L"zip"), nullptr),
                 S_OK);
        auto *folder = dynamic_cast<CAgentFolder *>(opening.Folder.Interface());
        QVERIFY(folder);
        auto *resource = dynamic_cast<QtArchiveFileResource *>(folder->GetEngineResource());
        QVERIFY(resource && resource->HasNativePropertyService());
        // This second source Agent exercises its public SetProperties contract.
        // Supply the fixture's known metadata instead of relying on CLI signals
        // from the unrelated initial opening.
        ArchiveEntry entry;
        entry.path = name;
        const QList<ArchiveEntry> entries{entry};
        CMyComPtr<IFolderFolder> metadata;
        QCOMPARE(CreateArchiveMetadata(entries, &metadata), S_OK);
        CMyComPtr2_Create<IFolderArcProps, CAgent> agent;
        QCOMPARE(agent->Init(metadata, UString(L"zip"), resource), S_OK);
        CMyComPtr<IFolderFolder> root;
        QCOMPARE(agent->BindToRootFolder(&root), S_OK);
        QCOMPARE(root->LoadItems(), S_OK);
        CMyComPtr<IFolderOperations> operations;
        QCOMPARE(root.QueryInterface(IID_IFolderOperations, &operations), S_OK);
        CMyComPtr<ISetProperties> properties;
        QCOMPARE(agent.Interface()->QueryInterface(IID_ISetProperties, (void **)&properties), S_OK);
        const wchar_t *names[] = {L"x"};
        // The optional Qt projection must not override explicit source properties.
        CompressionOptions options;
        options.format = "zip";
        options.Level = 9;
        resource->SetCompressionOptions(options);
        for (int level : {0, 9, -1, 1000}) {
            const QByteArray replacement(1024 * 1024, char('a' + (level & 15)));
            writeFile(source, replacement);
            NWindows::NCOM::CPropVariant value{UInt32(level)};
            if (level == 1000)
                value = L"invalid";
            QCOMPARE(properties->SetProperties(level < 0 ? nullptr : names,
                                               level < 0 ? nullptr : &value, level < 0 ? 0 : 1),
                     S_OK);
            const QByteArray before = readFile(archive);
            CCodecUpdateWorker worker(operations, source);
            worker.UseSourcePlan = true;
            worker.DisableUserQuestions = true;
            auto transport = resource->CreateProgressTransport(worker);
            QCOMPARE(worker.Create(L"Source writer properties", nullptr), S_OK);
            if (level == 1000) {
                QCOMPARE(worker.Result, E_INVALIDARG);
                QCOMPARE(readFile(archive), before);
                continue;
            }
            QCOMPARE(worker.Result, S_OK);
            QVERIFY(resource->HasNativePropertyService());
            NWindows::NCOM::CPropVariant packed;
            QCOMPARE(resource->NativeItemProperties()->GetProperty(0, kpidPackSize, &packed), S_OK);
            QCOMPARE(packed.vt, VARTYPE(VT_UI8));
            if (level == 0)
                QCOMPARE(packed.uhVal.QuadPart, UInt64(replacement.size()));
            else
                QVERIFY(packed.uhVal.QuadPart < UInt64(replacement.size() / 10));
            QProcess extractor;
            extractor.start(engine_, {"x", "-y", "-o" + fixture.filePath("out"), "--", archive});
            QVERIFY(extractor.waitForFinished(10000));
            QCOMPARE(extractor.exitCode(), 0);
            QCOMPARE(readFile(QDir(fixture.filePath("out")).filePath(name)), replacement);
        }
        QVERIFY(QDir(fixture.path())
                    .entryList({"*.tmp", ".sevenzip-*"}, QDir::Files | QDir::Hidden)
                    .isEmpty());
    }
    void duplicateNamesRetainDistinctNativeIndexes() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        writeFile(fixture.filePath("one.bin"), "one");
        writeFile(fixture.filePath("two.bin"), "different two");
        const QString initial = fixture.filePath("initial.zip");
        QProcess creator;
        creator.setWorkingDirectory(fixture.path());
        creator.start(engine_, {"a", "-tzip", "--", initial, "one.bin", "two.bin"});
        QVERIFY(creator.waitForFinished(10000));
        QCOMPARE(creator.exitCode(), 0);
        ArchiveBackend backend(nullptr, engine_);
        QCOMPARE(backend.loadCodecCatalog(module_, version_), S_OK);
        std::shared_ptr<ArchiveNativeOpen> reader;
        QCOMPARE(backend.openNativeArchive(initial, "zip", nullptr, reader), S_OK);
        QVERIFY(reader);
        CUpdateCallbackAgent progress;
        CMyComPtr2_Create<IArchiveUpdateCallback, CArchiveUpdateCallback> callback;
        CRecordVector<CUpdatePair2> pairs;
        CObjectVector<UString> names;
        names.Add(L"duplicate.bin");
        for (unsigned i = 0; i < 2; ++i) {
            CUpdatePair2 pair;
            pair.SetAs_NoChangeArcItem(i);
            pair.NewProps = true;
            pair.NewNameIndex = 0;
            pairs.Add(pair);
        }
        callback->Archive = reader->arc().Archive;
        callback->Callback = &progress;
        callback->UpdatePairs = &pairs;
        callback->NewNames = &names;
        const QString archive = fixture.filePath("duplicate.zip");
        CMyComPtr2_Create<ISequentialOutStream, COutFileStream> output;
        QVERIFY(output->Create_NEW(us2fs(archive.toStdWString().c_str())));
        QCOMPARE(backend.updateNativeArchive(reader, output, 2, callback), S_OK);
        QCOMPARE(output->Close(), S_OK);
        const QByteArray original = readFile(archive);
        QVERIFY(!original.isEmpty());
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        CFfpOpen opening(manager);
        QCOMPARE(opening.OpenFileFolderPlugin(nullptr, us2fs(archive.toStdWString().c_str()),
                                              UString(L"zip"), nullptr),
                 S_OK);
        auto *folder = dynamic_cast<CAgentFolder *>(opening.Folder.Interface());
        QVERIFY(folder);
        auto *resource = dynamic_cast<QtArchiveFileResource *>(folder->GetEngineResource());
        QVERIFY(resource);
        QVERIFY(resource->HasUpdateService());
        CMyComPtr<IFolderOperations> operations;
        QCOMPARE(opening.Folder.QueryInterface(IID_IFolderOperations, &operations), S_OK);
        CCodecUpdateWorker worker(operations, {});
        worker.NewName = "renamed.bin";
        worker.DisableUserQuestions = true;
        auto transport = resource->CreateProgressTransport(worker);
        QVERIFY(transport);
        QCOMPARE(worker.Create(L"Rename one duplicate entry", nullptr), S_OK);
        QCOMPARE(worker.Result, S_OK);
        QVERIFY(readFile(archive) != original);
        const QString destination = fixture.filePath("output");
        QProcess extractor;
        extractor.start(engine_, {"x", "-y", "-o" + destination, "--", archive});
        QVERIFY(extractor.waitForFinished(10000));
        QCOMPARE(extractor.exitCode(), 0);
        QCOMPARE(readFile(QDir(destination).filePath("renamed.bin")), QByteArray("one"));
        QCOMPARE(readFile(QDir(destination).filePath("duplicate.bin")),
                 QByteArray("different two"));
    }
};
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    CodecUpdateTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "codec_update_tests.moc"
