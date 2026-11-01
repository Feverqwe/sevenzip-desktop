#include "7zip/PropID.h"
#include "Windows/PropVariant.h"
#include "archive/agent/Agent.h"
#include "archive/resources/qtarchivefoldermanager.h"
#include "archive/support/archive_backend_test_support.h"
#include "archive/support/native_archive_fixture.h"
#include <QScopeGuard>
#include <QStandardPaths>
#include <atomic>
#include <functional>

namespace {
class CBindingCancellation final : public IProgress,
                                   public IArchiveOpenCallback,
                                   public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_2(IProgress, IArchiveOpenCallback)
  public:
    std::atomic<bool> Cancel{false};
    std::atomic<unsigned> Totals{0};
    bool ArmCancellation = true;
    bool BindingObserved = false;
    Z7_COM7F_IMF(SetTotal(UInt64 total)) override {
        ++Totals;
        // This fixture has one item. Native opening reports byte totals;
        // the source tree binding reports its item count through IProgress.
        if (total == 1) {
            BindingObserved = true;
            Cancel.store(ArmCancellation);
        }
        return Cancel.load() ? E_ABORT : S_OK;
    }
    Z7_COM7F_IMF(SetCompleted(const UInt64 *)) override { return Cancel.load() ? E_ABORT : S_OK; }
    Z7_COM7F_IMF(SetTotal(const UInt64 *files, const UInt64 *bytes)) override {
        ++Totals;
        if (files && *files == 1 && !bytes) {
            BindingObserved = true;
            Cancel.store(ArmCancellation);
        }
        return Cancel.load() ? E_ABORT : S_OK;
    }
    Z7_COM7F_IMF(SetCompleted(const UInt64 *, const UInt64 *)) override {
        return Cancel.load() ? E_ABORT : S_OK;
    }
};
} // namespace

class AgentLifecycleTests final : public QObject, protected ArchiveBackendTestSupport {
    Q_OBJECT
    QString engine_;

    static void verifyRetiredFolder(IFolderFolder &folder, IArchiveFolder &extract,
                                    IFolderOperations &operations, bool compareItems) {
        UInt32 count = 99;
        QVERIFY(FAILED(folder.GetNumberOfItems(&count)));
        QCOMPARE(count, 0u);
        CMyComPtr<IFolderGetItemName> names;
        QCOMPARE(folder.QueryInterface(IID_IFolderGetItemName, (void **)&names), S_OK);
        const wchar_t *name = L"old name";
        unsigned length = 8;
        QVERIFY(FAILED(names->GetItemName(0, &name, &length)));
        QVERIFY(!name);
        QCOMPARE(length, 0u);
        QVERIFY(FAILED(names->GetItemPrefix(0, &name, &length)));
        QVERIFY(!name);
        QCOMPARE(names->GetItemSize(0), UInt64(0));
        if (compareItems) {
            CMyComPtr<IFolderCompare> compare;
            QCOMPARE(folder.QueryInterface(IID_IFolderCompare, (void **)&compare), S_OK);
            QCOMPARE(compare->CompareItems(0, 1, kpidName, 0), Int32(0));
        }
        CMyComPtr<IFolderAltStreams> streams;
        QCOMPARE(folder.QueryInterface(IID_IFolderAltStreams, (void **)&streams), S_OK);
        Int32 supported = 1;
        QVERIFY(FAILED(streams->AreAltStreamsSupported(UInt32(-1), &supported)));
        QCOMPARE(supported, Int32(0));
        CMyComPtr<IFolderFolder> alternate;
        QVERIFY(FAILED(streams->BindToAltStreams(UInt32(-1), &alternate)));
        QVERIFY(!alternate);
        QVERIFY(FAILED(streams->BindToAltStreams(L"", &alternate)));
        QVERIFY(!alternate);
        CMyComPtr<IInArchiveGetStream> itemStreams;
        QCOMPARE(folder.QueryInterface(IID_IInArchiveGetStream, (void **)&itemStreams), S_OK);
        CMyComPtr<ISequentialInStream> stream;
        QVERIFY(FAILED(itemStreams->GetStream(0, &stream)));
        QVERIFY(!stream);
        const UInt32 index = 0;
        QVERIFY(FAILED(extract.Extract(&index, 1, 0, 0, NExtract::NPathMode::kNoPaths,
                                       NExtract::NOverwriteMode::kAsk, nullptr, 1, nullptr)));
        QVERIFY(FAILED(operations.CreateFolder(L"must not be created", nullptr)));
        NWindows::NCOM::CPropVariant comment(L"must not be written");
        QVERIFY(FAILED(operations.SetProperty(0, kpidComment, &comment, nullptr)));
        CMyComPtr<IArchiveGetRawProps> raw;
        QCOMPARE(folder.QueryInterface(IID_IArchiveGetRawProps, (void **)&raw), S_OK);
        const void *data = nullptr;
        UInt32 size = 0, type = 0;
        QVERIFY(FAILED(raw->GetRawProp(0, kpidCRC, &data, &size, &type)));
        QVERIFY(!data);
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
            QTest::qFail("Source lifecycle worker did not finish", __FILE__, __LINE__);
            return E_ABORT;
        }
        return result;
    }

  private slots:
    void initTestCase() {
        engine_ = QStandardPaths::findExecutable("7z");
        QVERIFY2(!engine_.isEmpty(), "Official fixture engine is required");
    }
    void reopenRestoresClosestSurvivingFolder_data() {
        QTest::addColumn<QString>("format");
        QTest::addColumn<int>("removedParents");
        for (const QString &format : {QStringLiteral("zip"), QStringLiteral("wim")})
            for (int removed = 0; removed <= 2; ++removed)
                QTest::newRow(qPrintable(format + QString::number(removed))) << format << removed;
    }
    void reopenRestoresClosestSurvivingFolder() {
        QFETCH(QString, format);
        QFETCH(int, removedParents);
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString parent = QStringLiteral("родитель 空");
        const QString child = QStringLiteral("внутри с пробелом");
        const QString prefix = parent + '/' + child + '/';
        const QString leaf = QStringLiteral("файл.bin");
        QList<NativeArchiveFixture::Item> original{
            {.Path = "root.bin", .Bytes = "root"},
            {.Path = parent, .Directory = true},
            {.Path = parent + "/keep.bin", .Bytes = "parent"},
            {.Path = parent + '/' + child, .Directory = true},
            {.Path = prefix + leaf, .Bytes = "original"}};
        QList<NativeArchiveFixture::Item> replacement{{.Path = "root.bin", .Bytes = "root"}};
        if (removedParents < 2) {
            replacement.append(original[1]);
            replacement.append(original[2]);
        }
        if (removedParents == 0) {
            replacement.append(original[3]);
            replacement.append({.Path = prefix + leaf, .Bytes = "updated content"});
        }
        const QString path = temporary.filePath(QStringLiteral("архив 空.") + format);
        const QString nextPath = temporary.filePath("replacement." + format);
        QVERIFY(NativeArchiveFixture::Write(path, original, format));
        QVERIFY(NativeArchiveFixture::Write(nextPath, replacement, format));
        ArchiveBackend backend(nullptr, engine_);
        QCOMPARE(NativeArchiveFixture::Attach(backend), S_OK);
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        CMyComPtr<IFolderFolder> root, parentFolder, selected;
        const auto nativePath = path.toStdWString();
        const auto nativeFormat = format.toStdWString();
        QCOMPARE(run(backend,
                     [&] {
                         return manager->OpenFolderFile(nullptr, nativePath.c_str(),
                                                        nativeFormat.c_str(), &root, nullptr);
                     }),
                 S_OK);
        QCOMPARE(root->BindToFolder(parent.toStdWString().c_str(), &parentFolder), S_OK);
        QVERIFY(parentFolder);
        QCOMPARE(parentFolder->BindToFolder(child.toStdWString().c_str(), &selected), S_OK);
        QVERIFY(selected);
        auto *agent = dynamic_cast<CAgentFolder *>(selected.Interface());
        QVERIFY(agent);
        QCOMPARE(agent->CloseArchive(), S_OK);
        QVERIFY(QFile::remove(path));
        QVERIFY(QFile::rename(nextPath, path));
        CMyComPtr<IFolderFolder> restored;
        QCOMPARE(run(backend, [&] { return agent->ReOpen(nullptr, &restored); }), S_OK);
        QVERIFY(restored);
        QCOMPARE(restored->LoadItems(), S_OK);
        NWindows::NCOM::CPropVariant value;
        QCOMPARE(restored->GetFolderProperty(kpidPath, &value), S_OK);
        QCOMPARE(value.vt, VARTYPE(VT_BSTR));
        const QString expectedPrefix = removedParents == 0   ? prefix
                                       : removedParents == 1 ? parent + '/'
                                                             : QString();
        QCOMPARE(QDir::fromNativeSeparators(QString::fromWCharArray(value.bstrVal)),
                 expectedPrefix);
        UInt32 count = 0;
        QCOMPARE(restored->GetNumberOfItems(&count), S_OK);
        QCOMPARE(count, UInt32(1));
        QCOMPARE(restored->GetProperty(0, kpidName, &value), S_OK);
        QCOMPARE(QString::fromWCharArray(value.bstrVal), removedParents == 0   ? leaf
                                                         : removedParents == 1 ? "keep.bin"
                                                                               : "root.bin");
    }
    void closedAndFailedReopenCannotLoadOldFolders_data() {
        QTest::addColumn<QString>("format");
        QTest::addColumn<bool>("flat");
        for (const QString &format : {QStringLiteral("zip"), QStringLiteral("wim")}) {
            QTest::newRow(qPrintable(format + "-normal")) << format << false;
            QTest::newRow(qPrintable(format + "-flat")) << format << true;
        }
    }
    void closedAndFailedReopenCannotLoadOldFolders() {
        QFETCH(QString, format);
        QFETCH(bool, flat);
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString directory = QStringLiteral("папка 空 with spaces");
        const QString name = QStringLiteral("файл 空.bin");
        QVERIFY(QDir(temporary.path()).mkpath(directory));
        const QString input = temporary.filePath(directory + '/' + name);
        const QByteArray payload("unchanged\0binary", 16);
        writeFile(input, payload);
        writeFile(temporary.filePath(directory + "/other.txt"), QByteArray("control"));
        const QString path = temporary.filePath(QStringLiteral("архив 空.") + format);
        QProcess process;
        process.setWorkingDirectory(temporary.path());
        process.start(engine_, {"a", "-t" + format, "--", path, directory});
        QVERIFY(process.waitForFinished(10000));
        QCOMPARE(process.exitCode(), 0);
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray archiveBytes = file.readAll();
        file.close();

        ArchiveBackend backend(nullptr, engine_);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog());
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY2(ready[0][0].toBool(), "Matching official codec module is required");
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        CMyComPtr<IFolderFolder> root;
        const std::wstring nativePath = path.toStdWString();
        const std::wstring nativeFormat = format.toStdWString();
        QCOMPARE(run(backend,
                     [&] {
                         return manager->OpenFolderFile(nullptr, nativePath.c_str(),
                                                        nativeFormat.c_str(), &root, nullptr);
                     }),
                 S_OK);
        CMyComPtr<IFolderSetFlatMode> mode;
        QCOMPARE(root.QueryInterface(IID_IFolderSetFlatMode, &mode), S_OK);
        QCOMPARE(mode->SetFlatMode(flat), S_OK);
        QCOMPARE(root->LoadItems(), S_OK);
        CMyComPtr<IFolderFolder> child;
        const std::wstring nativeDirectory = directory.toStdWString();
        QCOMPARE(root->BindToFolder(nativeDirectory.c_str(), &child), S_OK);
        QVERIFY(child);
        QCOMPARE(child->LoadItems(), S_OK);
        CMyComPtr<IArchiveFolder> rootExtract, childExtract;
        QCOMPARE(root.QueryInterface(IID_IArchiveFolder, &rootExtract), S_OK);
        QCOMPARE(child.QueryInterface(IID_IArchiveFolder, &childExtract), S_OK);
        CMyComPtr<IFolderOperations> rootOperations, childOperations;
        QCOMPARE(root.QueryInterface(IID_IFolderOperations, &rootOperations), S_OK);
        QCOMPARE(child.QueryInterface(IID_IFolderOperations, &childOperations), S_OK);
        CMyComPtr<IFolderCompare> compare;
        QCOMPARE(child.QueryInterface(IID_IFolderCompare, &compare), S_OK);
        QVERIFY(compare->CompareItems(0, 1, kpidName, 0) != 0);
        compare.Release();
        CMyComPtr<IGetFolderArcProps> getter;
        CMyComPtr<IFolderArcProps> properties;
        CMyComPtr<IInFolderArchive> archive;
        QCOMPARE(root.QueryInterface(IID_IGetFolderArcProps, &getter), S_OK);
        QCOMPARE(getter->GetFolderArcProps(&properties), S_OK);
        QCOMPARE(properties.QueryInterface(IID_IInFolderArchive, &archive), S_OK);
        NWindows::NCOM::CPropVariant originalType;
        QCOMPARE(root->GetFolderProperty(kpidType, &originalType), S_OK);
        const QString type = QString::fromWCharArray(originalType.bstrVal);
        manager.Release();
        QCOMPARE(archive->Close(), S_OK);
        // Both published folders still retain their source owner/proxy, but
        // the original LoadItems refuses a closed link before reading either.
        QCOMPARE(root->LoadItems(), E_FAIL);
        QCOMPARE(child->LoadItems(), E_FAIL);
        verifyRetiredFolder(*root, *rootExtract, *rootOperations, false);
        if (QTest::currentTestFailed())
            return;
        verifyRetiredFolder(*child, *childExtract, *childOperations, true);
        if (QTest::currentTestFailed())
            return;
        CMyComPtr<IFolderFolder> closedRoot;
        QVERIFY(FAILED(archive->BindToRootFolder(&closedRoot)));
        QVERIFY(!closedRoot);
        QVERIFY(QFile::remove(path));
        const HRESULT failure = run(backend, [&] { return archive->ReOpen(nullptr); });
        QVERIFY(failure != S_OK);
        // ReOpen has now deleted the old proxies. The source link-open guard
        // must short-circuit before consulting those invalidated pointers.
        QCOMPARE(root->LoadItems(), E_FAIL);
        QCOMPARE(child->LoadItems(), E_FAIL);
        verifyRetiredFolder(*root, *rootExtract, *rootOperations, false);
        if (QTest::currentTestFailed())
            return;
        verifyRetiredFolder(*child, *childExtract, *childOperations, true);
        if (QTest::currentTestFailed())
            return;
        NWindows::NCOM::CPropVariant stalePath;
        QCOMPARE(root->GetFolderProperty(kpidPath, &stalePath), E_FAIL);
        QCOMPARE(child->GetFolderProperty(kpidPath, &stalePath), E_FAIL);
        NWindows::NCOM::CPropVariant retainedType;
        QCOMPARE(root->GetFolderProperty(kpidType, &retainedType), S_OK);
        QCOMPARE(QString::fromWCharArray(retainedType.bstrVal), type);
        writeFile(path, archiveBytes);
        QCOMPARE(run(backend, [&] { return archive->ReOpen(nullptr); }), S_OK);
        CMyComPtr<IFolderFolder> repaired;
        QCOMPARE(archive->BindToRootFolder(&repaired), S_OK);
        QVERIFY(repaired);
        QCOMPARE(root->GetFolderProperty(kpidPath, &stalePath), E_FAIL);
        QCOMPARE(child->GetFolderProperty(kpidPath, &stalePath), E_FAIL);
        verifyRetiredFolder(*root, *rootExtract, *rootOperations, false);
        if (QTest::currentTestFailed())
            return;
        verifyRetiredFolder(*child, *childExtract, *childOperations, true);
        if (QTest::currentTestFailed())
            return;
        root.Release();
        child.Release();
        rootExtract.Release();
        childExtract.Release();
        rootOperations.Release();
        childOperations.Release();
        getter.Release();
        mode.Release();
        properties.Release();
        archive.Release();
        QCOMPARE(repaired->LoadItems(), S_OK);
        QCOMPARE(repaired->BindToFolder(nativeDirectory.c_str(), &child), S_OK);
        QCOMPARE(child->LoadItems(), S_OK);
        UInt32 count = 0;
        QCOMPARE(child->GetNumberOfItems(&count), S_OK);
        QCOMPARE(count, UInt32(2));
        UInt32 payloadIndex = 0;
        NWindows::NCOM::CPropVariant firstName;
        QCOMPARE(child->GetProperty(0, kpidName, &firstName), S_OK);
        if (QString::fromWCharArray(firstName.bstrVal) != name)
            payloadIndex = 1;
        NWindows::NCOM::CPropVariant size;
        QCOMPARE(child->GetProperty(payloadIndex, kpidSize, &size), S_OK);
        QCOMPARE(size.vt, VARTYPE(VT_UI8));
        QCOMPARE(size.uhVal.QuadPart, UInt64(payload.size()));
        QFile original(input);
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), payload);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), archiveBytes);
    }
    void canceledBindingCanRetryWithSameManager_data() {
        QTest::addColumn<QString>("format");
        QTest::newRow("path-proxy") << QStringLiteral("zip");
        QTest::newRow("tree-proxy") << QStringLiteral("wim");
    }
    void canceledBindingCanRetryWithSameManager() {
        QFETCH(QString, format);
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString input = temporary.filePath(QStringLiteral("файл 空 with spaces.txt"));
        const QString path = temporary.filePath(QStringLiteral("архив 空.") + format);
        const QByteArray payload("protected\0bytes", 15);
        writeFile(input, payload);
        QProcess process;
        process.start(engine_, {"a", "-t" + format, "--", path, input});
        QVERIFY(process.waitForFinished(10000));
        QCOMPARE(process.exitCode(), 0);
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray archiveBytes = file.readAll();
        file.close();
        auto *progressSpec = new CBindingCancellation;
        CMyComPtr<IProgress> progress = progressSpec;
        ArchiveBackend backend(nullptr, engine_);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog());
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY(ready[0][0].toBool());
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        const std::wstring nativePath = path.toStdWString();
        const std::wstring nativeFormat = format.toStdWString();
        CMyComPtr<IFolderFolder> folder;
        auto open = [&] {
            return manager->OpenFolderFile(nullptr, nativePath.c_str(), nativeFormat.c_str(),
                                           &folder, progress);
        };
        QCOMPARE(run(backend, open), E_ABORT);
        QVERIFY(!folder);
        QVERIFY(progressSpec->BindingObserved);
        QVERIFY(progressSpec->Totals.load() > 0);
        progressSpec->ArmCancellation = false;
        progressSpec->Cancel.store(false);
        QCOMPARE(run(backend, open), S_OK);
        QVERIFY(folder);

        manager.Release();
        progress.Release();
        CMyComPtr<IFolderSetFlatMode> flat;
        QCOMPARE(folder.QueryInterface(IID_IFolderSetFlatMode, &flat), S_OK);
        QCOMPARE(flat->SetFlatMode(true), S_OK);
        QCOMPARE(folder->LoadItems(), S_OK);
        UInt32 count = 0;
        QCOMPARE(folder->GetNumberOfItems(&count), S_OK);
        QCOMPARE(count, UInt32(1));
        QFile original(input);
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), payload);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), archiveBytes);
    }
};
QTEST_GUILESS_MAIN(AgentLifecycleTests)
#include "agent_lifecycle_tests.moc"
