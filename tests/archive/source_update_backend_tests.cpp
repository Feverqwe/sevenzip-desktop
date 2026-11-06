#include "7zip/Common/FileStreams.h"
#include "Common/StringConvert.h"
#include "archive/agent/Agent.h"
#include "archive/agent/UpdateCallbackAgent.h"
#include "archive/backend/archivebackend.h"
#include "archive/operations/EnumDirItems.h"
#include "archive/resources/archivenativeopen.h"
#include "archive/resources/qtarchivebackendrequest.h"
#include "archive/resources/qtarchivefileresource.h"
#include "archive/resources/qtarchivefoldermanager.h"
#include "archive/support/native_archive_fixture.h"
#include "archive/support/technical_listing_fixture.h"
#include "filemanager/UpdateCallback100.h"
#include "filemanager/panels/qtpanelfolder.h"
#include "filemanager/providers/FileFolderPluginOpen.h"
#include "gui/UpdateCallbackGUI.h"
#include "gui/UpdateGUI.h"
#include "platform/temp/WorkDir.h"
#include "ui/progress/ProgressDialog2.h"
#include "ui/progress/archiveprogresstransport.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>
#include <vector>

namespace {
enum class Mutation { Delete, Rename, CreateFolder };
QString enginePath() { return QStandardPaths::findExecutable("7z"); }
bool writeFile(const QString &path, const QByteArray &data) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}
QByteArray readFile(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
bool runEngine(const QString &directory, const QStringList &args) {
    QProcess process;
    process.setWorkingDirectory(directory);
    process.start(enginePath(), args);
    return process.waitForFinished(10000) && process.exitStatus() == QProcess::NormalExit &&
           process.exitCode() == 0;
}
HRESULT OpenResource(QtArchiveFileResource &resource, const QString &format,
                     CProgressDialog &progress, const QString &password = {}) {
    const auto catalog = NativeArchiveFixture::Catalog();
    if (catalog->result() != S_OK)
        return catalog->result();
    CUpdateCallbackGUI callback;
    callback.ProgressDialog = &progress;
    callback.Init();
    callback.PasswordIsDefined = !password.isEmpty();
    callback.Password = ToUString(password);
    std::shared_ptr<ArchiveNativeOpen> reader;
    RINOK(ArchiveBackend::openNativeArchive(catalog, resource.EnginePath, format, &callback, reader,
                                            nullptr))
    if (!reader)
        return E_FAIL;
    resource.AttachCodecCatalog(catalog);
    resource.AttachNativeOpen(std::move(reader));
    resource.UseNativeMetadata();
    return S_OK;
}
HRESULT NativeProperties(QtArchiveFileResource &resource, IFolderFolder **result) {
    auto *native = resource.NativeItemProperties();
    if (!native)
        return E_NOINTERFACE;
    UInt32 count = 0;
    RINOK(native->GetNumberOfItems(&count))
    CObjectVector<CPanelItem> items;
    for (UInt32 i = 0; i < count; ++i) {
        NWindows::NCOM::CPropVariant path;
        RINOK(native->GetProperty(i, kpidPath, &path))
        if (path.vt != VT_BSTR)
            return E_FAIL;
        CPanelItem item;
        item.RelPath = QString::fromWCharArray(path.bstrVal);
        items.Add(item);
    }
    CMyComPtr<IFolderFolder> properties =
        new QtPanelFolder({}, items, QtPanelFolder::PropertyCatalog::ArchiveTransport);
    *result = properties.Detach();
    return S_OK;
}
class CNativeUpdateWorker final : public CProgressThreadVirt {
    QString _archive, _disk, _output;
    CMyComPtr<IUnknown> _resource;
    QtArchiveFileResource *_resourceSpec;
    HRESULT ProcessVirt() override {
        RINOK(OpenResource(*_resourceSpec, "zip", *this))
        if (CancelBeforeUpdate)
            Sync.Set_Stopped(true);
        CMyComPtr<IFolderFolder> props;
        RINOK(NativeProperties(*_resourceSpec, &props))
        CMyComPtr2_Create<IFolderArcProps, CAgent> agent;
        RINOK(agent->Init(props, UString("zip"), _resource))
        CMyComPtr<IFolderFolder> root, inside;
        RINOK(agent->BindToRootFolder(&root))
        RINOK(root->BindToFolder(L"inside", &inside))
        RINOK(inside->LoadItems())
        RINOK(agent->SetFolder(inside))
        CMyComPtr2_Create<IFolderArchiveUpdateCallback, CUpdateCallback100Imp> ui;
        ui->ProgressDialog = this;
        ui->Init();
        const UInt32 selected = 0;
        if (Publish) {
            CMyComPtr<IFolderOperations> operations;
            RINOK(inside.QueryInterface(IID_IFolderOperations, &operations))
            const HRESULT result =
                operations->CopyFromFile(selected, _disk.toStdWString().c_str(), ui);
            Published = ui->Published;
            RINOK(result)
            RINOK(inside->LoadItems())
            NWindows::NCOM::CPropVariant size;
            RINOK(inside->GetProperty(0, kpidSize, &size))
            if (size.vt != VT_UI8)
                return E_FAIL;
            ReopenedSize = size.uhVal.QuadPart;
            return S_OK;
        }
        auto *outputSpec = new COutFileStream;
        CMyComPtr<ISequentialOutStream> output = outputSpec;
        if (!outputSpec->Create_NEW(us2fs(_output.toStdWString().c_str())))
            return E_FAIL;
        RINOK(agent->UpdateOneFile(output, &selected, 1, _disk.toStdWString().c_str(), ui))
        return outputSpec->Close();
    }

  public:
    bool CancelBeforeUpdate = false;
    bool Publish = false;
    UInt64 ReopenedSize = 0;
    std::optional<bool> Published;
    CNativeUpdateWorker(ArchiveBackend &backend, const QString &archive, const QString &disk,
                        const QString &output)
        : _archive(archive), _disk(disk), _output(output),
          _resourceSpec(new QtArchiveFileResource(archive)) {
        DisableUserQuestions = true;
        _resource = _resourceSpec;
        _resourceSpec->AttachUpdateService(backend, "zip");
    }
    std::unique_ptr<IQtProgressTransport> CreateProgressTransport() {
        return _resourceSpec->CreateProgressTransport(*this);
    }
};
class CNativeMutationWorker final : public CProgressThreadVirt {
    CMyComPtr<IUnknown> _resource;
    QtArchiveFileResource *_resourceSpec;
    QString _format;
    HRESULT ProcessVirt() override {
        RINOK(OpenResource(*_resourceSpec, _format, *this, Password))
        if (CancelBeforeUpdate)
            Sync.Set_Stopped(true);
        CMyComPtr<IFolderFolder> props;
        RINOK(NativeProperties(*_resourceSpec, &props))
        CMyComPtr2_Create<IFolderArcProps, CAgent> agent;
        RINOK(agent->Init(props, ToUString(_format), _resource))
        CMyComPtr<IFolderFolder> root, inside;
        RINOK(agent->BindToRootFolder(&root))
        RINOK(root->BindToFolder(L"inside", &inside))
        CMyComPtr<IFolderFolder> folder = RootFolder ? root : inside;
        RINOK(folder->LoadItems())
        CMyComPtr<IFolderOperations> operations;
        RINOK(folder.QueryInterface(IID_IFolderOperations, &operations))
        CMyComPtr2_Create<IFolderArchiveUpdateCallback, CUpdateCallback100Imp> ui;
        ui->ProgressDialog = this;
        ui->Init();
        if (!Password.isEmpty()) {
            ui->PasswordIsDefined = true;
            ui->Password = ToUString(Password);
        }
        const UInt32 selected = 0;
        HRESULT result;
        switch (Operation) {
        case Mutation::Delete:
            result = operations->Delete(&selected, 1, ui);
            break;
        case Mutation::Rename:
            result = operations->Rename(selected, L"новое имя.bin", ui);
            break;
        case Mutation::CreateFolder:
            result = operations->CreateFolder(L"новая папка", ui);
            break;
        default:
            return E_FAIL;
        }
        RINOK(result)
        RINOK(folder->LoadItems())
        NWindows::NCOM::CPropVariant path;
        RINOK(folder->GetFolderProperty(kpidPath, &path))
        if (path.vt != VT_BSTR)
            return E_FAIL;
        RestoredPath = QDir::fromNativeSeparators(QString::fromWCharArray(path.bstrVal));
        return S_OK;
    }

  public:
    bool CancelBeforeUpdate = false;
    Mutation Operation = Mutation::Delete;
    bool RootFolder = false;
    QString Password;
    QString RestoredPath;
    CNativeMutationWorker(ArchiveBackend &backend, const QString &archive, const QString &format)
        : _resourceSpec(new QtArchiveFileResource(archive)), _format(format) {
        DisableUserQuestions = true;
        _resource = _resourceSpec;
        _resourceSpec->AttachUpdateService(backend, format);
    }
    std::unique_ptr<IQtProgressTransport> CreateProgressTransport() {
        return _resourceSpec->CreateProgressTransport(*this);
    }
};

class CDeletionTrace final : public IFolderArchiveUpdateCallback, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_1(IFolderArchiveUpdateCallback)
    CMyComPtr<IFolderArchiveUpdateCallback> callback_;
    QStringList &deleted_;

  public:
    CDeletionTrace(IFolderArchiveUpdateCallback *callback, QStringList &deleted)
        : callback_(callback), deleted_(deleted) {}
    Z7_COM7F_IMF(SetTotal(UInt64 value)) override { return callback_->SetTotal(value); }
    Z7_COM7F_IMF(SetCompleted(const UInt64 *value)) override {
        return callback_->SetCompleted(value);
    }
    Z7_COM7F_IMF(CompressOperation(const wchar_t *name)) override {
        return callback_->CompressOperation(name);
    }
    Z7_COM7F_IMF(DeleteOperation(const wchar_t *name)) override {
        deleted_.append(QString::fromWCharArray(name));
        return callback_->DeleteOperation(name);
    }
    Z7_COM7F_IMF(OperationResult(Int32 value)) override {
        return callback_->OperationResult(value);
    }
    Z7_COM7F_IMF(UpdateErrorMessage(const wchar_t *message)) override {
        return callback_->UpdateErrorMessage(message);
    }
    Z7_COM7F_IMF(SetNumFiles(UInt64 count)) override { return callback_->SetNumFiles(count); }
};

class CNativeAddWorker final : public CProgressThreadVirt {
    QString _source, _format;
    CMyComPtr<IUnknown> _resource;
    CMyComPtr<IFolderFolder> _openedFolder;
    QtArchiveFileResource *_resourceSpec;
    HRESULT ProcessVirt() override {
        if (!_openedFolder)
            RINOK(OpenResource(*_resourceSpec, _format, *this, Password))
        if (CancelBeforeUpdate)
            Sync.Set_Stopped(true);
        CMyComPtr<IFolderFolder> folder = _openedFolder;
        if (!folder) {
            CMyComPtr<IFolderFolder> props;
            RINOK(NativeProperties(*_resourceSpec, &props))
            CMyComPtr2_Create<IFolderArcProps, CAgent> agent;
            RINOK(agent->Init(props, ToUString(_format), _resource))
            CMyComPtr<IFolderFolder> root, inside;
            RINOK(agent->BindToRootFolder(&root))
            folder = root;
            if (!RootFolder) {
                RINOK(root->BindToFolder(L"inside", &inside))
                folder = inside;
            }
            RINOK(folder->LoadItems())
        }
        CMyComPtr<IFolderOperations> operations;
        RINOK(folder.QueryInterface(IID_IFolderOperations, &operations))
        CMyComPtr2_Create<IFolderArchiveUpdateCallback, CUpdateCallback100Imp> ui;
        ui->ProgressDialog = this;
        ui->Init();
        if (!Password.isEmpty()) {
            ui->PasswordIsDefined = true;
            ui->Password = ToUString(Password);
        }
        if (PathMode && *PathMode == NWildcard::k_AbsPath) {
            CompressionOptions options;
            options.PathMode = *PathMode;
            _resourceSpec->SetCompressionOptions(options);
        }
        const QStringList sources = Sources.isEmpty() ? QStringList{_source} : Sources;
        CMyComPtr<IFolderArchiveUpdateCallback> callback = ui.Interface();
        if (TraceDeletion)
            callback = new CDeletionTrace(ui.Interface(), DeletedNames);
        const std::wstring parent = QFileInfo(sources.first()).absolutePath().toStdWString();
        std::vector<std::wstring> names;
        names.reserve(size_t(sources.size()));
        for (const QString &source : sources)
            names.push_back((RelativeSourcePaths ? QDir::current().relativeFilePath(
                                                       QFileInfo(source).canonicalFilePath())
                             : PathMode          ? QFileInfo(source).absoluteFilePath()
                                                 : QFileInfo(source).fileName())
                                .toStdWString());
        std::vector<const wchar_t *> pointers;
        pointers.reserve(names.size());
        for (const std::wstring &name : names)
            pointers.push_back(name.c_str());
        if (ActionSet) {
            auto *agentFolder = dynamic_cast<CAgentFolder *>((IFolderFolder *)folder);
            if (!agentFolder)
                return E_NOINTERFACE;
            if (PathMode)
                return agentFolder->UpdateFromPaths(pointers.data(), UInt32(pointers.size()),
                                                    *ActionSet, *PathMode, callback);
            return agentFolder->UpdateFrom(parent.c_str(), pointers.data(), UInt32(pointers.size()),
                                           *ActionSet, callback);
        }
        return operations->CopyFrom(MoveMode, parent.c_str(), pointers.data(),
                                    UInt32(pointers.size()), callback);
    }

  public:
    bool CancelBeforeUpdate = false;
    bool MoveMode = false;
    bool RootFolder = false;
    QStringList ArchivePaths;
    QStringList ArchiveDirectories;
    QString ArchiveMTimeText;
    const NUpdateArchive::CActionSet *ActionSet = nullptr;
    std::optional<NWildcard::ECensorPathMode> PathMode;
    QString Password;
    QStringList Sources;
    bool TraceDeletion = false;
    bool RelativeSourcePaths = false;
    QStringList DeletedNames;
    CNativeAddWorker(ArchiveBackend &backend, const QString &archive, const QString &source,
                     const QString &format)
        : _source(source), _format(format), _resourceSpec(new QtArchiveFileResource(archive)) {
        DisableUserQuestions = true;
        _resource = _resourceSpec;
        _resourceSpec->AttachUpdateService(backend, format);
    }
    std::unique_ptr<IQtProgressTransport> CreateProgressTransport() {
        return _resourceSpec->CreateProgressTransport(*this);
    }
    void SetOpenedFolder(IFolderFolder *folder) {
        _openedFolder = folder;
        auto *agent = dynamic_cast<CAgentFolder *>(folder);
        _resourceSpec =
            agent ? dynamic_cast<QtArchiveFileResource *>(agent->GetEngineResource()) : nullptr;
        Q_ASSERT(_resourceSpec);
        _resource = _resourceSpec;
    }
};
} // namespace

class SourceUpdateBackendTests final : public QObject {
    Q_OBJECT
  private slots:
    void nativeSyncRemovesDirectoryRecordsAndKeepsChildren_data() {
        QTest::addColumn<QString>("format");
        QTest::newRow("zip") << QString("zip");
        QTest::newRow("7z") << QString("7z");
    }
    void nativeSyncRemovesDirectoryRecordsAndKeepsChildren() {
        QFETCH(QString, format);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString oldRoot = fixture.filePath("old");
        const QString diskRoot = fixture.filePath("disk");
        const QString selectedFolder = QStringLiteral("папка с пробелом");
        const QString source = QDir(diskRoot).filePath(selectedFolder + "/nested/keep.txt");
        QVERIFY(writeFile(source, "older disk bytes"));
        const QString previousDirectory = QDir::currentPath();
        const auto restoreDirectory = qScopeGuard([&] { QDir::setCurrent(previousDirectory); });
        QVERIFY(QDir::setCurrent(diskRoot));
        NWildcard::CCensor censor;
        censor.AddPreItem_NoWildcard(ToUString(QDir(diskRoot).relativeFilePath(source)));
        censor.AddPathsToCensor(NWildcard::k_FullPath);
        CDirItems enumerated;
        QCOMPARE(EnumerateItems(censor, NWildcard::k_FullPath, UString(), enumerated), S_OK);
        QCOMPARE(enumerated.Items.Size(), 1u);
        const QString fullName =
            QDir::fromNativeSeparators(QString::fromWCharArray(enumerated.GetLogPath(0)));
        QVERIFY(fullName.endsWith("/nested/keep.txt"));
        const QString folder =
            QDir::fromNativeSeparators(fullName).chopped(QStringLiteral("/nested/keep.txt").size());
        const QString child = folder + "/nested/keep.txt";
        const QString removed = folder + "/nested/remove.txt";
        const QString archive = fixture.filePath("directories." + format);
        QVERIFY(writeFile(QDir(oldRoot).filePath(selectedFolder + "/nested/keep.txt"),
                          "retained archive bytes"));
        QVERIFY(
            writeFile(QDir(oldRoot).filePath(selectedFolder + "/nested/remove.txt"), "removed"));
        QCOMPARE(folder, selectedFolder);
        QVERIFY(NativeArchiveFixture::WriteFiles(archive, {QDir(oldRoot).filePath(selectedFolder)},
                                                 format));
        const QByteArray original = readFile(archive);
        QFile disk(source);
        QVERIFY(disk.open(QIODevice::ReadWrite));
        QVERIFY(disk.setFileTime(QDateTime::fromSecsSinceEpoch(1500000000, QTimeZone::UTC),
                                 QFileDevice::FileModificationTime));
        disk.close();
        ArchiveBackend backend(nullptr, enginePath());
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        CFfpOpen opening(manager);
        QCOMPARE(opening.OpenFileFolderPlugin(nullptr, us2fs(archive.toStdWString().c_str()),
                                              UString(), nullptr),
                 S_OK);
        auto *opened = dynamic_cast<CAgentFolder *>(opening.Folder.Interface());
        QVERIFY(opened);
        IInArchive *properties = opened->GetArchiveProperties();
        QVERIFY(properties);
        UInt32 count = 0;
        QCOMPARE(properties->GetNumberOfItems(&count), S_OK);
        QStringList actualPaths;
        for (UInt32 i = 0; i < count; ++i) {
            NWindows::NCOM::CPropVariant path;
            QCOMPARE(properties->GetProperty(i, kpidPath, &path), S_OK);
            QCOMPARE(path.vt, VARTYPE(VT_BSTR));
            actualPaths.append(QDir::fromNativeSeparators(QString::fromWCharArray(path.bstrVal)));
        }
        QVERIFY2(actualPaths.contains(child), qPrintable(actualPaths.join('\n')));
        CNativeAddWorker worker(backend, archive, source, format);
        worker.RelativeSourcePaths = true;
        worker.SetOpenedFolder(opening.Folder);
        worker.PathMode = NWildcard::k_FullPath;
        worker.ActionSet = &NUpdateArchive::k_ActionSet_Sync;
        worker.TraceDeletion = true;
        auto transport = worker.CreateProgressTransport();
        QCOMPARE(worker.Create(L"Synchronize selected child", nullptr), S_OK);
        QCOMPARE(worker.Result, S_OK);
        QStringList sourceDeleted;
        for (const QString &path : worker.DeletedNames)
            sourceDeleted.append(QDir::fromNativeSeparators(path));
        QVERIFY(sourceDeleted.contains(folder));
        QVERIFY(sourceDeleted.contains(removed));
        QVERIFY(!sourceDeleted.contains(child));
        QVERIFY(readFile(archive) != original);
        QProcess listing;
        listing.start(enginePath(), {"l", "-slt", "-sccUTF-8", "--", archive});
        QVERIFY(listing.waitForFinished(10000));
        QCOMPARE(listing.exitCode(), 0);
        const QByteArray contents = NormalizeTechnicalListing(listing.readAllStandardOutput());
        QVERIFY2(contents.contains("Path = " + child.toUtf8() + '\n'), contents.constData());
        QVERIFY(!contents.contains("Path = " + folder.toUtf8() + '\n'));
        QVERIFY(!contents.contains("Path = " + (folder + "/nested").toUtf8() + '\n'));
        QVERIFY(!contents.contains("Path = " + removed.toUtf8() + '\n'));
        const QString extracted = fixture.filePath("extracted");
        QVERIFY(QDir().mkpath(extracted));
        QVERIFY(runEngine(extracted, {"x", "-y", "--", archive}));
        QCOMPARE(readFile(QDir(extracted).filePath(child)), QByteArray("retained archive bytes"));
    }
    void sourcePathModeLogicalNames() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath("selected.txt");
        QVERIFY(writeFile(source, "path mode"));
        QStringList logicalNames;
        for (const auto mode :
             {NWildcard::k_RelatPath, NWildcard::k_FullPath, NWildcard::k_AbsPath}) {
            NWildcard::CCensor censor;
            censor.AddPreItem_NoWildcard(ToUString(source));
            censor.AddPathsToCensor(mode);
            CDirItems items;
            QCOMPARE(EnumerateItems(censor, mode, UString(), items), S_OK);
            QCOMPARE(items.Items.Size(), 1u);
            logicalNames.append(QString::fromWCharArray(items.GetLogPath(0).Ptr()));
        }
        QCOMPARE(logicalNames[0], QStringLiteral("selected.txt"));
#ifndef _WIN32
        QCOMPARE(logicalNames[1], source.mid(1));
#else
        QVERIFY(logicalNames[1].endsWith(QStringLiteral("selected.txt")));
#endif
        QCOMPARE(QDir::fromNativeSeparators(logicalNames[2]), source);
    }
    void nativeGuiPathModes_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("relative") << int(NWildcard::k_RelatPath);
        QTest::newRow("full") << int(NWildcard::k_FullPath);
        QTest::newRow("absolute") << int(NWildcard::k_AbsPath);
    }
    void nativeGuiPathModes() {
        QFETCH(int, mode);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString sourceRoot = fixture.filePath("old");
        const QString source = fixture.filePath("nested/selected.txt");
        if (mode == int(NWildcard::k_AbsPath))
            QVERIFY(ArchiveBackend::sourceUpdatePathIsSafe(source, true));
        const QString archive = fixture.filePath("paths.zip");
        QVERIFY(writeFile(QDir(sourceRoot).filePath("keep.txt"), "kept"));
        QVERIFY(runEngine(sourceRoot, {"a", "-tzip", "--", archive, "keep.txt"}));
        QVERIFY(writeFile(source, "path mode"));
        ArchiveBackend backend(nullptr, enginePath());
        CNativeAddWorker worker(backend, archive, source, "zip");
        worker.RootFolder = true;
        worker.ArchivePaths = {"keep.txt"};
        worker.ActionSet = &NUpdateArchive::k_ActionSet_Add;
        worker.PathMode = static_cast<NWildcard::ECensorPathMode>(mode);
        worker.DisableUserQuestions = true;
        auto transport = worker.CreateProgressTransport();
        QCOMPARE(worker.Create(L"Path mode", nullptr), S_OK);
        QCOMPARE(worker.Result, S_OK);
        if (mode == int(NWildcard::k_AbsPath)) {
            QProcess listing;
            listing.start(enginePath(), {"l", "-slt", "-sccUTF-8", "--", archive});
            QVERIFY(listing.waitForFinished(10000));
            QCOMPARE(listing.exitCode(), 0);
            QVERIFY(NormalizeTechnicalListing(listing.readAllStandardOutput())
                        .contains("Path = " + source.toUtf8()));
            return;
        }
        const QString extracted = fixture.filePath("extracted");
        QVERIFY(QDir().mkpath(extracted));
        QVERIFY(runEngine(extracted, {"x", "-y", "--", archive}));
        QString logical = QStringLiteral("selected.txt");
        if (mode == int(NWildcard::k_FullPath)) {
#ifdef _WIN32
            logical = QFileInfo(source).absoluteFilePath().mid(QDir::rootPath().length());
#else
            logical = QFileInfo(source).absoluteFilePath().mid(1);
#endif
        }
        QCOMPARE(readFile(QDir(extracted).filePath(logical)), QByteArray("path mode"));
        QCOMPARE(readFile(QDir(extracted).filePath("keep.txt")), QByteArray("kept"));
    }
    void nativeGuiDirectoryPathModes_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("relative") << int(NWildcard::k_RelatPath);
        QTest::newRow("full") << int(NWildcard::k_FullPath);
        QTest::newRow("absolute") << int(NWildcard::k_AbsPath);
    }
    void nativeGuiDirectoryPathModes() {
        QFETCH(int, mode);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString oldRoot = fixture.filePath("old");
        const QString folder = fixture.filePath(QStringLiteral("папка с пробелом"));
        const QString archive = fixture.filePath("directories.zip");
        QVERIFY(writeFile(QDir(oldRoot).filePath("keep.txt"), "keep"));
        QVERIFY(runEngine(oldRoot, {"a", "-tzip", "--", archive, "keep.txt"}));
        QVERIFY(writeFile(QDir(folder).filePath(QStringLiteral("внутри/файл.txt")), "nested"));
        QVERIFY(QDir().mkpath(QDir(folder).filePath("empty")));
        ArchiveBackend backend(nullptr, enginePath());
        CNativeAddWorker worker(backend, archive, folder, "zip");
        worker.RootFolder = true;
        worker.ArchivePaths = {"keep.txt"};
        worker.ActionSet = &NUpdateArchive::k_ActionSet_Add;
        worker.PathMode = static_cast<NWildcard::ECensorPathMode>(mode);
        worker.DisableUserQuestions = true;
        auto transport = worker.CreateProgressTransport();
        QCOMPARE(worker.Create(L"Directory path mode", nullptr), S_OK);
        QCOMPARE(worker.Result, S_OK);
        QString logical = QFileInfo(folder).fileName();
        if (mode == int(NWildcard::k_FullPath)) {
#ifdef _WIN32
            logical = folder.mid(QDir::rootPath().length());
#else
            logical = folder.mid(1);
#endif
        } else if (mode == int(NWildcard::k_AbsPath))
            logical = folder;
        QProcess listing;
        listing.start(enginePath(), {"l", "-slt", "-sccUTF-8", "--", archive});
        QVERIFY(listing.waitForFinished(10000));
        QCOMPARE(listing.exitCode(), 0);
        const QByteArray contents = NormalizeTechnicalListing(listing.readAllStandardOutput());
        const QString listingText =
            QString::fromUtf8(contents).normalized(QString::NormalizationForm_C);
        const QString expected = logical.normalized(QString::NormalizationForm_C);
        QVERIFY(listingText.contains("Path = " + expected + "/внутри/файл.txt"));
        QVERIFY(listingText.contains("Path = " + expected + "/empty"));
        if (mode != int(NWildcard::k_AbsPath)) {
            const QString extracted = fixture.filePath("extracted");
            QVERIFY(QDir().mkpath(extracted));
            QVERIFY(runEngine(extracted, {"x", "-y", "--", archive}));
            QCOMPARE(readFile(QDir(extracted).filePath(logical + "/внутри/файл.txt")),
                     QByteArray("nested"));
            QVERIFY(QFileInfo(QDir(extracted).filePath(logical + "/empty")).isDir());
        }
    }
    void initTestCase() {
        QVERIFY2(!enginePath().isEmpty(), "7z required for local source-update fixture");
    }
    void nativeUpdateModesFollowSourcePairActions_data() {
        QTest::addColumn<QString>("format");
        QTest::addColumn<int>("mode");
        QTest::addColumn<QByteArray>("olderBytes");
        QTest::addColumn<bool>("newPresent");
        QTest::addColumn<bool>("archiveOnlyPresent");
        QTest::newRow("zip-add") << QString("zip") << int(NCompressDialog::NUpdateMode::kAdd)
                                 << QByteArray("disk older") << true << true;
        QTest::newRow("zip-update") << QString("zip") << int(NCompressDialog::NUpdateMode::kUpdate)
                                    << QByteArray("archive older") << true << true;
        QTest::newRow("zip-freshen") << QString("zip") << int(NCompressDialog::NUpdateMode::kFresh)
                                     << QByteArray("archive older") << false << true;
        QTest::newRow("zip-sync") << QString("zip") << int(NCompressDialog::NUpdateMode::kSync)
                                  << QByteArray("archive older") << true << false;
        QTest::newRow("7z-add") << QString("7z") << int(NCompressDialog::NUpdateMode::kAdd)
                                << QByteArray("disk older") << true << true;
        QTest::newRow("7z-update") << QString("7z") << int(NCompressDialog::NUpdateMode::kUpdate)
                                   << QByteArray("archive older") << true << true;
        QTest::newRow("7z-freshen") << QString("7z") << int(NCompressDialog::NUpdateMode::kFresh)
                                    << QByteArray("archive older") << false << true;
        QTest::newRow("7z-sync") << QString("7z") << int(NCompressDialog::NUpdateMode::kSync)
                                 << QByteArray("archive older") << true << false;
    }
    void nativeUpdateModesFollowSourcePairActions() {
        QFETCH(QString, format);
        QFETCH(int, mode);
        QFETCH(QByteArray, olderBytes);
        QFETCH(bool, newPresent);
        QFETCH(bool, archiveOnlyPresent);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString oldRoot = fixture.filePath("original");
        const QString diskRoot = fixture.filePath("disk");
        const QString archive = fixture.filePath("modes." + format);
        const QStringList archiveNames = {"newer.txt", "older.txt", "archive-only.txt"};
        QVERIFY(writeFile(QDir(oldRoot).filePath("newer.txt"), "archive newer"));
        QVERIFY(writeFile(QDir(oldRoot).filePath("older.txt"), "archive older"));
        QVERIFY(writeFile(QDir(oldRoot).filePath("archive-only.txt"), "archive only"));
        const QDateTime baseline = QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC);
        for (const QString &name : archiveNames) {
            QFile file(QDir(oldRoot).filePath(name));
            QVERIFY(file.open(QIODevice::ReadWrite));
            QVERIFY(file.setFileTime(baseline, QFileDevice::FileModificationTime));
        }
        QVERIFY(runEngine(oldRoot, {"a", "-t" + format, "--", archive, "newer.txt", "older.txt",
                                    "archive-only.txt"}));
        const QByteArray original = readFile(archive);
        QVERIFY(writeFile(QDir(diskRoot).filePath("newer.txt"), "disk newer"));
        QVERIFY(writeFile(QDir(diskRoot).filePath("older.txt"), "disk older"));
        QVERIFY(writeFile(QDir(diskRoot).filePath("new-file.txt"), "disk new"));
        for (const auto &entry : {qMakePair(QStringLiteral("newer.txt"), 86400),
                                  qMakePair(QStringLiteral("older.txt"), -86400),
                                  qMakePair(QStringLiteral("new-file.txt"), 86400)}) {
            QFile file(QDir(diskRoot).filePath(entry.first));
            QVERIFY(file.open(QIODevice::ReadWrite));
            QVERIFY(file.setFileTime(baseline.addSecs(entry.second),
                                     QFileDevice::FileModificationTime));
        }
        ArchiveBackend backend(nullptr, enginePath());
        CNativeAddWorker worker(backend, archive, QDir(diskRoot).filePath("newer.txt"), format);
        worker.RootFolder = true;
        worker.ArchivePaths = archiveNames;
        worker.ArchiveMTimeText = baseline.toLocalTime().toString("yyyy-MM-dd HH:mm:ss");
        worker.Sources = {QDir(diskRoot).filePath("newer.txt"),
                          QDir(diskRoot).filePath("older.txt"),
                          QDir(diskRoot).filePath("new-file.txt")};
        worker.ActionSet = NCompressDialog::FindUpdateActionSet(
            static_cast<NCompressDialog::NUpdateMode::EEnum>(mode));
        QVERIFY(worker.ActionSet);
        auto transport = worker.CreateProgressTransport();
        QCOMPARE(worker.Create(L"Update", nullptr), S_OK);
        QCOMPARE(worker.Result, S_OK);
        QVERIFY(readFile(archive) != original);
        const QString extracted = fixture.filePath("extracted");
        QVERIFY(QDir().mkpath(extracted));
        QVERIFY(runEngine(extracted, {"x", "-y", "--", archive}));
        QCOMPARE(readFile(QDir(extracted).filePath("newer.txt")), QByteArray("disk newer"));
        QCOMPARE(readFile(QDir(extracted).filePath("older.txt")), olderBytes);
        QCOMPARE(QFileInfo::exists(QDir(extracted).filePath("new-file.txt")), newPresent);
        QCOMPARE(QFileInfo::exists(QDir(extracted).filePath("archive-only.txt")),
                 archiveOnlyPresent);
    }
    void nativeCopyFromAddsToCurrentFolder() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString root = fixture.filePath("original");
        const QString archive = fixture.filePath("original.zip");
        const QString source = fixture.filePath(QStringLiteral("новый файл.bin"));
        QVERIFY(writeFile(QDir(root).filePath("inside/keep.bin"), "keep"));
        QVERIFY(runEngine(root, {"a", "-tzip", "--", archive, "inside/keep.bin"}));
        QVERIFY(writeFile(source, "new bytes"));
        QFile timestamped(source);
        QVERIFY(timestamped.open(QIODevice::ReadWrite));
        const QDateTime modified = QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC);
        QVERIFY(timestamped.setFileTime(modified, QFileDevice::FileModificationTime));
        timestamped.close();
        ArchiveBackend backend(nullptr, enginePath());
        CNativeAddWorker worker(backend, archive, source, "zip");
        auto transport = worker.CreateProgressTransport();
        QVERIFY(transport);
        QCOMPARE(worker.Create(L"Copy into archive", nullptr), S_OK);
        QCOMPARE(worker.Result, S_OK);
        const QString extracted = fixture.filePath("extracted");
        QVERIFY(runEngine(fixture.path(), {"x", "-y", "-o" + extracted, "--", archive}));
        QCOMPARE(readFile(QDir(extracted).filePath("inside/keep.bin")), QByteArray("keep"));
        QCOMPARE(readFile(QDir(extracted).filePath(QStringLiteral("inside/новый файл.bin"))),
                 QByteArray("new bytes"));
        const qint64 difference =
            qAbs(QFileInfo(QDir(extracted).filePath(QStringLiteral("inside/новый файл.bin")))
                     .lastModified()
                     .toSecsSinceEpoch() -
                 modified.toSecsSinceEpoch());
        QVERIFY(difference <= 2);
        QCOMPARE(readFile(source), QByteArray("new bytes"));
    }
    void nativeCopyFromAddsDirectoryTree() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString root = fixture.filePath("original");
        const QString archive = fixture.filePath("original.zip");
        const QString source = fixture.filePath(QStringLiteral("папка с пробелом"));
        QVERIFY(writeFile(QDir(root).filePath("inside/keep.bin"), "keep"));
        QVERIFY(runEngine(root, {"a", "-tzip", "--", archive, "inside/keep.bin"}));
        QVERIFY(
            writeFile(QDir(source).filePath(QStringLiteral("вложенная/файл.txt")), "tree bytes"));
        QVERIFY(QDir().mkpath(QDir(source).filePath("empty")));
        ArchiveBackend backend(nullptr, enginePath());
        CNativeAddWorker worker(backend, archive, source, "zip");
        auto transport = worker.CreateProgressTransport();
        QVERIFY(transport);
        QCOMPARE(worker.Create(L"Copy directory into archive", nullptr), S_OK);
        QCOMPARE(worker.Result, S_OK);
        const QString extracted = fixture.filePath("extracted");
        QVERIFY(runEngine(fixture.path(), {"x", "-y", "-o" + extracted, "--", archive}));
        QCOMPARE(readFile(QDir(extracted).filePath(
                     QStringLiteral("inside/папка с пробелом/вложенная/файл.txt"))),
                 QByteArray("tree bytes"));
        QVERIFY(QFileInfo(QDir(extracted).filePath(QStringLiteral("inside/папка с пробелом/empty")))
                    .isDir());
    }
    void nativeCopyFromAddsMultipleSelectedFiles() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString root = fixture.filePath("original");
        const QString archive = fixture.filePath("original.zip");
        const QString first = fixture.filePath(QStringLiteral("первый.bin"));
        const QString empty = fixture.filePath("empty file.bin");
        QVERIFY(writeFile(QDir(root).filePath("inside/keep.bin"), "keep"));
        QVERIFY(runEngine(root, {"a", "-tzip", "--", archive, "inside/keep.bin"}));
        QVERIFY(writeFile(first, "first bytes"));
        QVERIFY(writeFile(empty, {}));
        ArchiveBackend backend(nullptr, enginePath());
        CNativeAddWorker worker(backend, archive, first, "zip");
        worker.Sources = {first, empty};
        auto transport = worker.CreateProgressTransport();
        QVERIFY(transport);
        QCOMPARE(worker.Create(L"Copy selected files", nullptr), S_OK);
        QCOMPARE(worker.Result, S_OK);
        const QString extracted = fixture.filePath("extracted");
        QVERIFY(runEngine(fixture.path(), {"x", "-y", "-o" + extracted, "--", archive}));
        QCOMPARE(readFile(QDir(extracted).filePath(QStringLiteral("inside/первый.bin"))),
                 QByteArray("first bytes"));
        QVERIFY(QFileInfo(QDir(extracted).filePath("inside/empty file.bin")).isFile());
        QCOMPARE(readFile(QDir(extracted).filePath("inside/empty file.bin")), QByteArray());
    }
    void nativeMovePublishesBeforeDeletingDiskSource() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString root = fixture.filePath("original");
        const QString archive = fixture.filePath("original.zip");
        const QString source = fixture.filePath(QStringLiteral("переместить.txt"));
        QVERIFY(writeFile(QDir(root).filePath("inside/keep.bin"), "keep"));
        QVERIFY(runEngine(root, {"a", "-tzip", "--", archive, "inside/keep.bin"}));
        QVERIFY(writeFile(source, "move bytes"));
        ArchiveBackend backend(nullptr, enginePath());
        CNativeAddWorker worker(backend, archive, source, "zip");
        worker.MoveMode = true;
        auto transport = worker.CreateProgressTransport();
        QVERIFY(transport);
        QCOMPARE(worker.Create(L"Move into archive", nullptr), S_OK);
        QCOMPARE(worker.Result, S_OK);
        QVERIFY(!QFileInfo::exists(source));
        const QString extracted = fixture.filePath("extracted");
        QVERIFY(runEngine(fixture.path(), {"x", "-y", "-o" + extracted, "--", archive}));
        QCOMPARE(readFile(QDir(extracted).filePath(QStringLiteral("inside/переместить.txt"))),
                 QByteArray("move bytes"));
    }
    void cancelledMoveLeavesDiskSourceAndArchiveIntact() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString root = fixture.filePath("original");
        const QString archive = fixture.filePath("original.zip");
        const QString source = fixture.filePath("large move.bin");
        QVERIFY(writeFile(QDir(root).filePath("inside/keep.bin"), "keep"));
        QVERIFY(runEngine(root, {"a", "-tzip", "--", archive, "inside/keep.bin"}));
        const QByteArray before = readFile(archive);
        QVERIFY(writeFile(source, QByteArray(8 * 1024 * 1024, 'x')));
        ArchiveBackend backend(nullptr, enginePath());
        CNativeAddWorker worker(backend, archive, source, "zip");
        worker.MoveMode = true;
        auto transport = worker.CreateProgressTransport();
        QVERIFY(transport);
        worker.CancelBeforeUpdate = true;
        QCOMPARE(worker.Create(L"Cancel archive move", nullptr), S_OK);
        QCOMPARE(worker.Result, E_ABORT);
        QCOMPARE(readFile(archive), before);
        QCOMPARE(QFileInfo(source).size(), qint64(8 * 1024 * 1024));
    }
    void protectedTopArchiveAcceptsCopyFrom() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString root = fixture.filePath("original");
        const QString archive = fixture.filePath("original.7z");
        const QString source = fixture.filePath(QStringLiteral("секретное имя.txt"));
        QVERIFY(writeFile(QDir(root).filePath("inside/keep.bin"), "keep"));
        QVERIFY(runEngine(root,
                          {"a", "-t7z", "-psecret", "-mhe=on", "--", archive, "inside/keep.bin"}));
        QVERIFY(writeFile(source, "protected bytes"));
        ArchiveBackend backend(nullptr, enginePath());
        CNativeAddWorker worker(backend, archive, source, "7z");
        worker.Password = "secret";
        auto transport = worker.CreateProgressTransport();
        QVERIFY(transport);
        QCOMPARE(worker.Create(L"Copy into protected archive", nullptr), S_OK);
        QCOMPARE(worker.Result, S_OK);
        const QString extracted = fixture.filePath("extracted");
        QVERIFY(
            runEngine(fixture.path(), {"x", "-y", "-psecret", "-o" + extracted, "--", archive}));
        QCOMPARE(readFile(QDir(extracted).filePath(QStringLiteral("inside/секретное имя.txt"))),
                 QByteArray("protected bytes"));
        QCOMPARE(readFile(QDir(extracted).filePath("inside/keep.bin")), QByteArray("keep"));
    }
    void nativeCallbackKeepsArchiveNameAndClosesInputStreams_data() {
        QTest::addColumn<bool>("publish");
        QTest::newRow("separate-output") << false;
        QTest::newRow("native-workdir-publish-reopen") << true;
    }
    void nativeCallbackKeepsArchiveNameAndClosesInputStreams() {
        QFETCH(bool, publish);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString root = fixture.filePath("original");
        const QString entry = QStringLiteral("inside/исходное имя.bin");
        const QString archive = fixture.filePath("original.zip");
        const QString output = fixture.filePath("updated.zip");
        const QString disk = fixture.filePath("different disk name.bin");
        QVERIFY(writeFile(QDir(root).filePath(entry), "old"));
        QVERIFY(writeFile(QDir(root).filePath("untouched.bin"), "keep"));
        QVERIFY(runEngine(root, {"a", "-tzip", "--", archive, entry, "untouched.bin"}));
        const QByteArray original = readFile(archive);
        QVERIFY(writeFile(disk, "replacement"));
        ArchiveBackend backend(nullptr, enginePath());
        CNativeUpdateWorker worker(backend, archive, disk, output);
        worker.Publish = publish;
        auto transport = worker.CreateProgressTransport();
        QVERIFY(transport);
        QCOMPARE(worker.Create(L"Source update", nullptr), S_OK);
        QCOMPARE(worker.Result, S_OK);
        if (publish)
            QCOMPARE(worker.ReopenedSize, UInt64(11));
        else
            QCOMPARE(readFile(archive), original);
        const QString extracted = fixture.filePath("extracted");
        QVERIFY(runEngine(fixture.path(),
                          {"x", "-y", "-o" + extracted, "--", publish ? archive : output}));
        QCOMPARE(readFile(QDir(extracted).filePath(entry)), QByteArray("replacement"));
        QCOMPARE(readFile(QDir(extracted).filePath("untouched.bin")), QByteArray("keep"));
        QVERIFY(!QFile::exists(QDir(extracted).filePath("different disk name.bin")));
    }
    void cancellationInNativeTransactionKeepsOriginalArchive() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString root = fixture.filePath("original");
        const QString entry = QStringLiteral("inside/исходное имя.bin");
        const QString archive = fixture.filePath("original.zip");
        const QString disk = fixture.filePath("different disk name.bin");
        QVERIFY(writeFile(QDir(root).filePath(entry), "old"));
        QVERIFY(writeFile(QDir(root).filePath("untouched.bin"), "keep"));
        QVERIFY(runEngine(root, {"a", "-tzip", "--", archive, entry, "untouched.bin"}));
        const QByteArray original = readFile(archive);
        QVERIFY(writeFile(disk, QByteArray(4 * 1024 * 1024, 'x')));
        ArchiveBackend backend(nullptr, enginePath());
        CNativeUpdateWorker worker(backend, archive, disk, {});
        worker.Publish = true;
        auto transport = worker.CreateProgressTransport();
        QVERIFY(transport);
        worker.CancelBeforeUpdate = true;
        QCOMPARE(worker.Create(L"Cancel source update", nullptr), S_OK);
        QCOMPARE(worker.Result, E_ABORT);
        QVERIFY(!backend.busy());
        QCOMPARE(readFile(archive), original);
        QCOMPARE(worker.ReopenedSize, UInt64(0));
    }
    void panelChangedFileUsesNativeFolderTransaction() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString root = fixture.filePath("original");
        const QString entry = QStringLiteral("inside/исходное имя.bin");
        const QString archive = fixture.filePath("original.zip");
        const QString disk = fixture.filePath("different disk name.bin");
        QVERIFY(writeFile(QDir(root).filePath(entry), "old"));
        QVERIFY(writeFile(QDir(root).filePath("untouched.bin"), "keep"));
        QVERIFY(runEngine(root, {"a", "-tzip", "--", archive, entry, "untouched.bin"}));
        QVERIFY(writeFile(disk, "source panel replacement"));
        ArchiveBackend backend(nullptr, enginePath());
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        CFfpOpen opening(manager);
        QCOMPARE(opening.OpenFileFolderPlugin(nullptr, us2fs(archive.toStdWString().c_str()),
                                              UString("zip"), nullptr),
                 S_OK);
        CMyComPtr<IFolderFolder> inside;
        QCOMPARE(opening.Folder->BindToFolder(L"inside", &inside), S_OK);
        QCOMPARE(inside->LoadItems(), S_OK);
        CPanel panel(inside);
        CTempFileInfo temp;
        temp.FileIndex = 99; // Original item index became stale during the external edit.
        temp.RelPath = L"исходное имя.bin";
        temp.FilePath = us2fs(disk.toStdWString().c_str());
        const UString prefix = panel.GetCurrentFolderPrefix();
        const auto original = readFile(archive);
        std::optional<CSelectedState> refresh = CSelectedState();
        QVERIFY(!panel.OnOpenItemChanged(temp, UString(L"different folder/"), false, UString(),
                                         &refresh));
        QVERIFY(!refresh);
        QCOMPARE(readFile(archive), original);
        temp.RelPath = L"removed.bin";
        refresh = CSelectedState();
        QVERIFY(!panel.OnOpenItemChanged(temp, prefix, false, UString(), &refresh));
        QVERIFY(!refresh);
        QCOMPARE(readFile(archive), original);
        temp.RelPath = L"исходное имя.bin";
        QVERIFY(panel.OnOpenItemChanged(temp, prefix, false, UString(), &refresh));
        QVERIFY(refresh);
        QVERIFY(!backend.busy());
        NWindows::NCOM::CPropVariant size;
        QCOMPARE(panel.GetFolder()->GetProperty(0, kpidSize, &size), S_OK);
        QCOMPARE(size.vt, VARTYPE(VT_UI8));
        QCOMPARE(size.uhVal.QuadPart, UInt64(24));
        const QString extracted = fixture.filePath("extracted");
        QVERIFY(runEngine(fixture.path(), {"x", "-y", "-o" + extracted, "--", archive}));
        QCOMPARE(readFile(QDir(extracted).filePath(entry)), QByteArray("source panel replacement"));
        QCOMPARE(readFile(QDir(extracted).filePath("untouched.bin")), QByteArray("keep"));
        panel.ReleaseFolder();
        QVERIFY(!panel.OnOpenItemChanged(temp, prefix, false, UString(), &refresh));
        QVERIFY(!refresh);
    }
    void sourceNamesRequiringCorrectionAreRejected() {
        for (const QString &path :
             {QString(), QStringLiteral("../escape"), QStringLiteral("/absolute"),
              QStringLiteral("a/../b"), QStringLiteral("a/./b"), QStringLiteral("a//b")})
            QVERIFY2(!ArchiveBackend::sourceUpdatePathIsSafe(path), qPrintable(path));
        QVERIFY(
            !ArchiveBackend::sourceUpdatePathIsSafe(QStringLiteral("safe") + QChar(0) + "tail"));
        QVERIFY(
            ArchiveBackend::sourceUpdatePathIsSafe(QStringLiteral("inside/имя с пробелом.bin")));
    }
    void sourceFolderMutation_data() {
        QTest::addColumn<int>("operation");
        QTest::newRow("delete") << int(Mutation::Delete);
        QTest::newRow("rename") << int(Mutation::Rename);
        QTest::newRow("create-folder") << int(Mutation::CreateFolder);
    }
    void sourceFolderMutationPublishesAndReopens_data() {
        QTest::addColumn<QString>("format");
        QTest::addColumn<int>("operation");
        for (const QString &format : {QStringLiteral("zip"), QStringLiteral("7z")})
            for (int kind = 0; kind < 3; ++kind)
                QTest::newRow(qPrintable(format + QString::number(kind))) << format << kind;
    }
    void sourceFolderMutationPublishesAndReopens() {
        QFETCH(QString, format);
        QFETCH(int, operation);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString root = fixture.filePath("source");
        const QString archive = fixture.filePath("original." + format);
        const QString entry = QStringLiteral("inside/имя с пробелом.bin");
        QVERIFY(writeFile(QDir(root).filePath(entry), "payload"));
        QVERIFY(writeFile(QDir(root).filePath("keep.bin"), "keep"));
        const QDateTime originalTime = QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC);
        QFile keep(QDir(root).filePath("keep.bin"));
        QVERIFY(keep.open(QIODevice::ReadWrite));
        QVERIFY(keep.setFileTime(originalTime, QFileDevice::FileModificationTime));
        keep.close();
        QVERIFY(runEngine(root, {"a", "-t" + format, "--", archive, "inside", "keep.bin"}));
        ArchiveBackend backend(nullptr, enginePath());
        CNativeMutationWorker worker(backend, archive, format);
        worker.Operation = Mutation(operation);
        auto transport = worker.CreateProgressTransport();
        QVERIFY(transport);
        QCOMPARE(worker.Create(L"Source folder mutation", nullptr), S_OK);
        QCOMPARE(worker.Result, S_OK);
        QCOMPARE(worker.RestoredPath, QStringLiteral("inside/"));
        QVERIFY(!backend.busy());
        const QString extracted = fixture.filePath("extracted");
        QVERIFY(runEngine(fixture.path(), {"x", "-y", "-o" + extracted, "--", archive}));
        QCOMPARE(readFile(QDir(extracted).filePath("keep.bin")), QByteArray("keep"));
        QCOMPARE(QFileInfo(QDir(extracted).filePath("keep.bin")).lastModified().toSecsSinceEpoch(),
                 originalTime.toSecsSinceEpoch());
        if (worker.Operation == Mutation::Delete)
            QVERIFY(!QFile::exists(QDir(extracted).filePath(entry)));
        else if (worker.Operation == Mutation::Rename) {
            QVERIFY(!QFile::exists(QDir(extracted).filePath(entry)));
            QCOMPARE(readFile(QDir(extracted).filePath("inside/новое имя.bin")),
                     QByteArray("payload"));
        } else {
            QCOMPARE(readFile(QDir(extracted).filePath(entry)), QByteArray("payload"));
            QVERIFY(QFileInfo(QDir(extracted).filePath("inside/новая папка")).isDir());
        }
    }
    void implicitFolderRenameAndDelete_data() {
        QTest::addColumn<QString>("format");
        QTest::addColumn<int>("operation");
        for (const QString &format : {QStringLiteral("zip"), QStringLiteral("7z")})
            for (int kind = 0; kind < 2; ++kind)
                QTest::newRow(qPrintable(format + QString::number(kind))) << format << kind;
    }
    void implicitFolderRenameAndDelete() {
        QFETCH(QString, format);
        QFETCH(int, operation);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString root = fixture.filePath("source");
        const QString archive = fixture.filePath("original." + format);
        const QString entry = QStringLiteral("inside/имя с пробелом.bin");
        QVERIFY(writeFile(QDir(root).filePath(entry), "payload"));
        QVERIFY(writeFile(QDir(root).filePath("keep.bin"), "keep"));
        QVERIFY(runEngine(root, {"a", "-t" + format, "--", archive, entry, "keep.bin"}));
        ArchiveBackend backend(nullptr, enginePath());
        CNativeMutationWorker worker(backend, archive, format);
        worker.RootFolder = true;
        worker.Operation = operation == 0 ? Mutation::Delete : Mutation::Rename;
        auto transport = worker.CreateProgressTransport();
        QVERIFY(transport);
        QCOMPARE(worker.Create(L"Source folder rename/delete", nullptr), S_OK);
        QCOMPARE(worker.Result, S_OK);
        QCOMPARE(worker.RestoredPath, QString());
        const QString extracted = fixture.filePath("extracted");
        QVERIFY(runEngine(fixture.path(), {"x", "-y", "-o" + extracted, "--", archive}));
        QCOMPARE(readFile(QDir(extracted).filePath("keep.bin")), QByteArray("keep"));
        QVERIFY(!QFile::exists(QDir(extracted).filePath(entry)));
        if (operation == 1)
            QCOMPARE(readFile(QDir(extracted).filePath(
                         QStringLiteral("новое имя.bin/имя с пробелом.bin"))),
                     QByteArray("payload"));
    }
    void cancelledSourceDeleteKeepsArchive() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString root = fixture.filePath("source");
        const QString archive = fixture.filePath("original.zip");
        const QString entry = QStringLiteral("inside/имя с пробелом.bin");
        QVERIFY(writeFile(QDir(root).filePath(entry), QByteArray(4 * 1024 * 1024, 'x')));
        QVERIFY(writeFile(QDir(root).filePath("keep.bin"), "keep"));
        QVERIFY(runEngine(root, {"a", "-tzip", "--", archive, entry, "keep.bin"}));
        const QByteArray original = readFile(archive);
        ArchiveBackend backend(nullptr, enginePath());
        CNativeMutationWorker worker(backend, archive, "zip");
        worker.Operation = Mutation::Delete;
        auto transport = worker.CreateProgressTransport();
        QVERIFY(transport);
        worker.CancelBeforeUpdate = true;
        QCOMPARE(worker.Create(L"Cancel source delete", nullptr), S_OK);
        QCOMPARE(worker.Result, E_ABORT);
        QCOMPARE(readFile(archive), original);
        QVERIFY(!backend.busy());
    }
    void encryptedSourceMutationPreservesProtection_data() {
        QTest::addColumn<QString>("format");
        QTest::addColumn<int>("operation");
        for (const QString &format : {QStringLiteral("zip"), QStringLiteral("7z")})
            for (int kind = 0; kind < 3; ++kind)
                QTest::newRow(qPrintable(format + QString::number(kind))) << format << kind;
    }
    void protectedGuiAddPreservesEncryption_data() {
        QTest::addColumn<QString>("format");
        QTest::newRow("zip-aes") << QStringLiteral("zip");
        QTest::newRow("7z-header") << QStringLiteral("7z");
    }
    void protectedGuiAddPreservesEncryption() {
        QFETCH(QString, format);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString root = fixture.filePath("source");
        const QString archive = fixture.filePath("protected." + format);
        const QString source = QDir(root).filePath(QStringLiteral("новый файл.txt"));
        QVERIFY(writeFile(QDir(root).filePath("keep.txt"), "protected old"));
        QVERIFY(writeFile(source, "protected new"));
        QStringList args = {"a", "-t" + format, "-pfixture123"};
        args << (format == "7z" ? "-mhe=on" : "-mem=AES256") << "--" << archive << "keep.txt";
        QVERIFY(runEngine(root, args));
        ArchiveBackend backend(nullptr, enginePath());
        CNativeAddWorker worker(backend, archive, source, format);
        worker.RootFolder = true;
        worker.ArchivePaths = {"keep.txt"};
        worker.ActionSet = &NUpdateArchive::k_ActionSet_Add;
        worker.PathMode = NWildcard::k_RelatPath;
        worker.Password = "fixture123";
        worker.DisableUserQuestions = true;
        auto transport = worker.CreateProgressTransport();
        QCOMPARE(worker.Create(L"Protected path-mode Add", nullptr), S_OK);
        QCOMPARE(worker.Result, S_OK);
        QProcess listProcess;
        listProcess.start(enginePath(), {"l", "-slt", "-sccUTF-8", "-pfixture123", "--", archive});
        QVERIFY(listProcess.waitForFinished(10000));
        QCOMPARE(listProcess.exitCode(), 0);
        const QByteArray listing = listProcess.readAllStandardOutput();
        QVERIFY(listing.contains("Path = keep.txt"));
        const QString listingText =
            QString::fromUtf8(listing).normalized(QString::NormalizationForm_C);
        QVERIFY(listingText.contains(QStringLiteral("Path = новый файл.txt")));
        QCOMPARE(listing.count("Encrypted = +"), 2);
        if (format == "zip")
            QCOMPARE(listing.count("Method = AES-256"), 2);
        else
            QVERIFY(!runEngine(fixture.path(), {"l", "-slt", "-pwrong", "--", archive}));
        const QString extracted = fixture.filePath("extracted");
        QVERIFY(runEngine(fixture.path(),
                          {"x", "-y", "-pfixture123", "-o" + extracted, "--", archive}));
        QCOMPARE(readFile(QDir(extracted).filePath("keep.txt")), QByteArray("protected old"));
        const QString addedPath = QDir(extracted).filePath(QStringLiteral("новый файл.txt"));
        QCOMPARE(readFile(addedPath), QByteArray("protected new"));
    }
    void encryptedSourceMutationPreservesProtection() {
        QFETCH(QString, format);
        QFETCH(int, operation);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString root = fixture.filePath("source");
        const QString archive = fixture.filePath("protected." + format);
        const QString entry = QStringLiteral("inside/имя с пробелом.bin");
        QVERIFY(writeFile(QDir(root).filePath(entry), "protected payload"));
        QVERIFY(writeFile(QDir(root).filePath("keep.bin"), "keep"));
        const QDateTime originalTime = QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC);
        QFile keep(QDir(root).filePath("keep.bin"));
        QVERIFY(keep.open(QIODevice::ReadWrite));
        QVERIFY(keep.setFileTime(originalTime, QFileDevice::FileModificationTime));
        keep.close();
        QStringList args = {"a", "-t" + format, "-pfixture123"};
        args << (format == "7z" ? "-mhe=on" : "-mem=AES256") << "--" << archive << entry
             << "keep.bin";
        QVERIFY(runEngine(root, args));
        ArchiveBackend backend(nullptr, enginePath());
        CNativeMutationWorker worker(backend, archive, format);
        worker.Password = "fixture123";
        worker.Operation = Mutation(operation);
        auto transport = worker.CreateProgressTransport();
        QVERIFY(transport);
        QCOMPARE(worker.Create(L"Protected source mutation", nullptr), S_OK);
        QCOMPARE(worker.Result, S_OK);
        QVERIFY(!backend.busy());
        QVERIFY(!runEngine(fixture.path(), {"t", "-pwrong", "--", archive}));
        QVERIFY(runEngine(fixture.path(), {"t", "-pfixture123", "--", archive}));
        const QString extracted = fixture.filePath("extracted");
        QVERIFY(runEngine(fixture.path(),
                          {"x", "-y", "-pfixture123", "-o" + extracted, "--", archive}));
        QCOMPARE(readFile(QDir(extracted).filePath("keep.bin")), QByteArray("keep"));
        QCOMPARE(QFileInfo(QDir(extracted).filePath("keep.bin")).lastModified().toSecsSinceEpoch(),
                 originalTime.toSecsSinceEpoch());
        if (operation == 0)
            QVERIFY(!QFile::exists(QDir(extracted).filePath(entry)));
        else if (operation == 1) {
            QVERIFY(!QFile::exists(QDir(extracted).filePath(entry)));
            QCOMPARE(readFile(QDir(extracted).filePath("inside/новое имя.bin")),
                     QByteArray("protected payload"));
        } else {
            QCOMPARE(readFile(QDir(extracted).filePath(entry)), QByteArray("protected payload"));
            QVERIFY(QFileInfo(QDir(extracted).filePath("inside/новая папка")).isDir());
        }
    }
    void wrongPasswordAndCancellationKeepProtectedArchive() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString root = fixture.filePath("source");
        const QString archive = fixture.filePath("protected.7z");
        const QString entry = QStringLiteral("inside/имя с пробелом.bin");
        QVERIFY(writeFile(QDir(root).filePath(entry), QByteArray(4 * 1024 * 1024, 'x')));
        QVERIFY(writeFile(QDir(root).filePath("keep.bin"), "keep"));
        QVERIFY(runEngine(
            root, {"a", "-t7z", "-pfixture123", "-mhe=on", "--", archive, entry, "keep.bin"}));
        const QByteArray original = readFile(archive);
        ArchiveBackend backend(nullptr, enginePath());
        CNativeMutationWorker wrong(backend, archive, "7z");
        wrong.DisableUserQuestions = true;
        wrong.Password = "wrong";
        wrong.Operation = Mutation::Delete;
        auto wrongTransport = wrong.CreateProgressTransport();
        QVERIFY(wrongTransport);
        QCOMPARE(wrong.Create(L"Wrong protected password", nullptr), S_OK);
        QVERIFY(wrong.Result != S_OK);
        QCOMPARE(readFile(archive), original);
        CNativeMutationWorker cancelled(backend, archive, "7z");
        cancelled.Password = "fixture123";
        cancelled.Operation = Mutation::Delete;
        auto cancelTransport = cancelled.CreateProgressTransport();
        QVERIFY(cancelTransport);
        cancelled.CancelBeforeUpdate = true;
        QCOMPARE(cancelled.Create(L"Cancel protected mutation", nullptr), S_OK);
        QCOMPARE(cancelled.Result, E_ABORT);
        QCOMPARE(readFile(archive), original);
    }
};

QTEST_MAIN(SourceUpdateBackendTests)
#include "source_update_backend_tests.moc"
