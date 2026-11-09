#include "7zip/Common/StreamObjects.h"
#include "archive/agent/Agent.h"
#include "archive/backend/archivebackend.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/resources/qtarchivefileresource.h"
#include "archive/resources/qtarchivefoldermanager.h"
#include "archive/resources/qtarchivereopen.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/providers/FileFolderPluginOpen.h"
#include <QCryptographicHash>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>
#include <future>

namespace {
class CObservedBackend final : public ArchiveBackend {
  public:
    unsigned Starts = 0;
    using ArchiveBackend::ArchiveBackend;
    void openFolder(const QString &path, const QString &type, const QString &password) override {
        ++Starts;
        ArchiveBackend::openFolder(path, type, password);
    }
};
class COpenProgress final : public IProgress, public ICryptoGetTextPassword, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_2(IProgress, ICryptoGetTextPassword)

  public:
    bool Cancel = false;
    unsigned PasswordCalls = 0;
    Z7_COM7F_IMF(SetTotal(UInt64)) override { return Cancel ? E_ABORT : S_OK; }
    Z7_COM7F_IMF(SetCompleted(const UInt64 *)) override { return Cancel ? E_ABORT : S_OK; }
    Z7_COM7F_IMF(CryptoGetTextPassword(BSTR *password)) override {
        ++PasswordCalls;
        return StringToBstr(UString(L"local fixture password"), password);
    }
};
class CExtractToMemory final : public IArchiveExtractCallback, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_1(IArchiveExtractCallback)

  public:
    CMyComPtr2<ISequentialOutStream, CDynBufSeqOutStream> Stream;
    CExtractToMemory() { Stream.Create_if_Empty(); }
    Z7_COM7F_IMF(SetTotal(UInt64)) override { return S_OK; }
    Z7_COM7F_IMF(SetCompleted(const UInt64 *)) override { return S_OK; }
    Z7_COM7F_IMF(GetStream(UInt32, ISequentialOutStream **stream, Int32)) override {
        *stream = Stream.Interface();
        (*stream)->AddRef();
        return S_OK;
    }
    Z7_COM7F_IMF(PrepareOperation(Int32)) override { return S_OK; }
    Z7_COM7F_IMF(SetOperationResult(Int32 result)) override {
        return result == NArchive::NExtract::NOperationResult::kOK ? S_OK : E_FAIL;
    }
};
} // namespace

class NativeFolderOpeningTests final : public QObject {
    Q_OBJECT
    QString engine_, module_;
    quint32 version_ = 0;
    bool createFixture(const QString &path, const QString &input, bool encrypted = false) {
        QProcess creator;
        QStringList arguments{"a", encrypted ? "-t7z" : "-tzip"};
        if (encrypted)
            arguments << "-plocal fixture password" << "-mhe=on";
        arguments << "--" << path << input;
        creator.start(engine_, arguments);
        return creator.waitForFinished(10000) && creator.exitCode() == 0;
    }

    void extractToMemory(IInArchive *archive, const QByteArray &expected) {
        auto *callback = new CExtractToMemory;
        CMyComPtr<IArchiveExtractCallback> owner = callback;
        auto result = std::async(std::launch::async,
                                 [&] { return archive->Extract(nullptr, UInt32(-1), 0, owner); });
        QTRY_VERIFY_WITH_TIMEOUT(
            result.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready, 10000);
        QCOMPARE(result.get(), S_OK);
        QCOMPARE(QByteArray(reinterpret_cast<const char *>(callback->Stream->GetBuffer()),
                            qsizetype(callback->Stream->GetSize())),
                 expected);
    }

  private slots:
    void initTestCase() {
#ifndef SEVENZIP_CODEC_MODULE
        QSKIP("Official module unavailable; native application opening unverified");
#else
        engine_ = QStandardPaths::findExecutable("7z");
        if (engine_.isEmpty())
            QSKIP("Official engine unavailable; native application opening unverified");
        module_ = QString::fromUtf8(SEVENZIP_CODEC_MODULE);
        ArchiveBackend backend(nullptr, engine_);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(module_));
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY2(ready[0][0].toBool(), qPrintable(ready[0][1].toString()));
        version_ = backend.codecCatalog()->version();
#endif
    }
    void sourceFolderRetainsNativePropertiesAndRefusesWrongType() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QFile input(fixture.filePath(QStringLiteral("имя 空 with spaces.bin")));
        QVERIFY(input.open(QIODevice::WriteOnly));
        QCOMPARE(input.write("native folder"), qint64(13));
        input.close();
        const QString path = fixture.filePath("archive.unknown");
        QVERIFY(createFixture(path, input.fileName()));
        CObservedBackend backend(nullptr, engine_);
        QCOMPARE(backend.loadCodecCatalog(module_, version_), S_OK);
        std::weak_ptr<const ArchiveCodecCatalog> old = backend.codecCatalog();
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        CFfpOpen opening(manager);
        QCOMPARE(opening.OpenFileFolderPlugin(nullptr, us2fs(path.toStdWString().c_str()),
                                              UString(L"*"), UString(), nullptr),
                 S_OK);
        QCOMPARE(backend.Starts, 0u);
        auto *folder = dynamic_cast<CAgentFolder *>(opening.Folder.Interface());
        QVERIFY(folder);
        IInArchive *archive = folder->GetArchiveProperties();
        QVERIFY(archive);
        UInt32 count = 0;
        QCOMPARE(archive->GetNumberOfArchiveProperties(&count), S_OK);
        QVERIFY(count > 0);
        QCOMPARE(backend.loadCodecCatalog(fixture.filePath("missing module"), version_), E_FAIL);
        QVERIFY(!old.expired());
        NWindows::NCOM::CPropVariant size;
        QCOMPARE(archive->GetArchiveProperty(kpidPhySize, &size), S_OK);
        QCOMPARE(size.vt, VARTYPE(VT_UI8));
        QCOMPARE(size.uhVal.QuadPart, UInt64(QFileInfo(path).size()));
        QCOMPARE(archive->Close(), S_OK);
        opening.Folder.Release();
        QVERIFY(old.expired());
        QCOMPARE(backend.loadCodecCatalog(module_, version_), S_OK);
        CFfpOpen wrong(manager);
        QCOMPARE(wrong.OpenFileFolderPlugin(nullptr, us2fs(path.toStdWString().c_str()),
                                            UString(L"7z"), UString(), nullptr),
                 S_FALSE);
        QVERIFY(!wrong.Folder);
        QCOMPARE(backend.Starts, 0u);
    }
    void nativeWimTreeRetainsRawParentsAndAgentBinding() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QVERIFY(QDir().mkpath(fixture.filePath(QStringLiteral("tree/папка δ"))));
        QFile input(fixture.filePath(QStringLiteral("tree/папка δ/имя space.bin")));
        QVERIFY(input.open(QIODevice::WriteOnly));
        QCOMPARE(input.write("tree bytes"), qint64(10));
        input.close();
        const QString path = fixture.filePath("tree.wim");
        QProcess creator;
        creator.setWorkingDirectory(fixture.path());
        creator.start(engine_, {"a", "-twim", "--", path, "tree"});
        QVERIFY(creator.waitForFinished(10000));
        QCOMPARE(creator.exitCode(), 0);
        CObservedBackend backend(nullptr, engine_);
        QCOMPARE(backend.loadCodecCatalog(module_, version_), S_OK);
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        CFfpOpen opening(manager);
        QCOMPARE(opening.OpenFileFolderPlugin(nullptr, us2fs(path.toStdWString().c_str()),
                                              UString(L"wim"), UString(), nullptr),
                 S_OK);
        auto *folder = dynamic_cast<CAgentFolder *>(opening.Folder.Interface());
        QVERIFY(folder);
        auto *resource = dynamic_cast<QtArchiveFileResource *>(folder->GetEngineResource());
        QVERIFY(resource);
        QVERIFY(resource->NativeItemProperties());
        CMyComPtr<IInArchive> archive = folder->GetArchiveProperties();
        CMyComPtr<IArchiveGetRawProps> raw;
        QCOMPARE(archive.QueryInterface(IID_IArchiveGetRawProps, &raw), S_OK);
        UInt32 count = 0;
        QCOMPARE(archive->GetNumberOfItems(&count), S_OK);
        QVERIFY(count >= 3);
        bool hasParent = false;
        for (UInt32 index = 0; index < count; ++index) {
            UInt32 parent = 0, type = 0;
            QCOMPARE(raw->GetParent(index, &parent, &type), S_OK);
            hasParent |= parent != UInt32(-1);
            QVERIFY(parent == UInt32(-1) || parent < count);
        }
        QVERIFY(hasParent);
        CMyComPtr<IFolderAltStreams> streams;
        QCOMPARE(opening.Folder.QueryInterface(IID_IFolderAltStreams, &streams), S_OK);
        Int32 supported = 0;
        QCOMPARE(streams->AreAltStreamsSupported(UInt32(-1), &supported), S_OK);
        QCOMPARE(supported, Int32(1));
        CMyComPtr<IFolderSetFlatMode> flat;
        QCOMPARE(opening.Folder.QueryInterface(IID_IFolderSetFlatMode, &flat), S_OK);
        QCOMPARE(flat->SetFlatMode(1), S_OK);
        QCOMPARE(opening.Folder->LoadItems(), S_OK);
        UInt32 listed = 0;
        QCOMPARE(opening.Folder->GetNumberOfItems(&listed), S_OK);
        QCOMPARE(listed, count);
        extractToMemory(archive, QByteArray("tree bytes"));

        QCOMPARE(backend.loadCodecCatalog(fixture.filePath("missing module"), version_), E_FAIL);
        CMyComPtr<IFolderFolder> reopened;
        QCOMPARE(ReOpenArchiveFolder(*folder, {}, nullptr, &reopened, false, UString()), S_OK);
        CMyComPtr<IFolderAltStreams> reopenedStreams;
        QCOMPARE(reopened.QueryInterface(IID_IFolderAltStreams, &reopenedStreams), S_OK);
        supported = 0;
        QCOMPARE(reopenedStreams->AreAltStreamsSupported(UInt32(-1), &supported), S_OK);
        QCOMPARE(supported, Int32(1));
        raw.Release();
        QCOMPARE(archive.QueryInterface(IID_IArchiveGetRawProps, &raw), S_OK);
        hasParent = false;
        for (UInt32 index = 0; index < count; ++index) {
            UInt32 parent = 0, type = 0;
            QCOMPARE(raw->GetParent(index, &parent, &type), S_OK);
            hasParent |= parent != UInt32(-1);
            QVERIFY(parent == UInt32(-1) || parent < count);
        }
        QVERIFY(hasParent);
        extractToMemory(archive, QByteArray("tree bytes"));
    }

    void nativeWimComparatorUsesTreeNamesSizesAndRawHashes() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QVERIFY(QDir().mkpath(fixture.filePath(QStringLiteral("tree/папка δ"))));
        const QStringList names = {QStringLiteral("a 文件.txt"), QStringLiteral("b space δ.txt")};
        const QList<QByteArray> bytes = {QByteArray("a"), QByteArray("bc")};
        for (int i = 0; i < names.size(); ++i) {
            QFile input(fixture.filePath(QStringLiteral("tree/папка δ/") + names[i]));
            QVERIFY(input.open(QIODevice::WriteOnly));
            QCOMPARE(input.write(bytes[i]), qint64(bytes[i].size()));
        }
        const QString path = fixture.filePath("sort tree.wim");
        QProcess creator;
        creator.setWorkingDirectory(fixture.path());
        creator.start(engine_, {"a", "-twim", "--", path, "tree"});
        QVERIFY(creator.waitForFinished(10000));
        QCOMPARE(creator.exitCode(), 0);
        CObservedBackend backend(nullptr, engine_);
        QCOMPARE(backend.loadCodecCatalog(module_, version_), S_OK);
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        CFfpOpen opening(manager);
        QCOMPARE(opening.OpenFileFolderPlugin(nullptr, us2fs(path.toStdWString().c_str()),
                                              UString(L"wim"), UString(), nullptr),
                 S_OK);
        CMyComPtr<IFolderSetFlatMode> flat;
        QCOMPARE(opening.Folder.QueryInterface(IID_IFolderSetFlatMode, &flat), S_OK);
        QCOMPARE(flat->SetFlatMode(1), S_OK);
        CPanel panel(opening.Folder);
        panel.ChangeFlatMode();
        QCOMPARE(panel.LoadListItems(), S_OK);
        UInt32 count = 0;
        QCOMPARE(opening.Folder->GetNumberOfItems(&count), S_OK);
        QList<UInt32> indices;
        for (const QString &name : names) {
            bool found = false;
            for (UInt32 i = 0; i < count; ++i)
                if (ToQString(panel.GetItemName(i)) == name) {
                    indices.append(i);
                    found = true;
                    break;
                }
            QVERIFY(found);
        }
        CMyComPtr<IFolderCompare> compare;
        QCOMPARE(opening.Folder.QueryInterface(IID_IFolderCompare, &compare), S_OK);
        CMyComPtr<IArchiveGetRawProps> raw;
        QCOMPARE(opening.Folder.QueryInterface(IID_IArchiveGetRawProps, &raw), S_OK);
        QList<QByteArray> hashes;
        for (int i = 0; i < indices.size(); ++i) {
            const void *data = nullptr;
            UInt32 size = 0, type = 0;
            QCOMPARE(raw->GetRawProp(indices[i], kpidSha1, &data, &size, &type), S_OK);
            QCOMPARE(type, UInt32(NPropDataType::kRaw));
            hashes.append(QByteArray(static_cast<const char *>(data), size));
            QCOMPARE(hashes.back(), QCryptographicHash::hash(bytes[i], QCryptographicHash::Sha1));
        }
        const int expected = hashes[0] < hashes[1] ? -1 : 1;
        QCOMPARE(compare->CompareItems(indices[0], indices[1], kpidSha1, 1), expected);
        QCOMPARE(panel.CompareItemIndices(indices[0], indices[1], kpidSha1, true), expected);
        QVERIFY(compare->CompareItems(indices[0], indices[1], kpidSize, 0) < 0);
        QVERIFY(compare->CompareItems(indices[0], indices[1], kpidName, 0) < 0);
        QCOMPARE(compare->CompareItems(indices[0], indices[1], kpidExtension, 0), 0);
        QCOMPARE(compare->CompareItems(indices[0], indices[1], kpidPrefix, 0), 0);
    }

#ifdef Q_OS_WIN
    void nativeWimAlternateStreamBindsToOriginalFileOwner() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QVERIFY(QDir().mkpath(fixture.filePath("tree")));
        const QString name = QStringLiteral("имя δ space.bin");
        const QString base = fixture.filePath("tree/" + name);
        QFile input(base);
        QVERIFY(input.open(QIODevice::WriteOnly));
        QCOMPARE(input.write("main bytes"), qint64(10));
        input.close();
        QFile stream(base + QStringLiteral(":поток δ"));
        QVERIFY(stream.open(QIODevice::WriteOnly));
        QCOMPARE(stream.write("alternate bytes"), qint64(15));
        stream.close();
        const QString path = fixture.filePath("streams.wim");
        QProcess creator;
        creator.setWorkingDirectory(fixture.path());
        creator.start(engine_, {"a", "-twim", "-sns", "--", path, "tree"});
        QVERIFY(creator.waitForFinished(10000));
        QCOMPARE(creator.exitCode(), 0);
        CObservedBackend backend(nullptr, engine_);
        QCOMPARE(backend.loadCodecCatalog(module_, version_), S_OK);
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        CFfpOpen opening(manager);
        QCOMPARE(opening.OpenFileFolderPlugin(nullptr, us2fs(path.toStdWString().c_str()),
                                              UString(L"wim"), UString(), nullptr),
                 S_OK);
        auto *folder = dynamic_cast<CAgentFolder *>(opening.Folder.Interface());
        QVERIFY(folder);
        CMyComPtr<IFolderSetFlatMode> flat;
        QCOMPARE(opening.Folder.QueryInterface(IID_IFolderSetFlatMode, &flat), S_OK);
        QCOMPARE(flat->SetFlatMode(1), S_OK);
        QCOMPARE(opening.Folder->LoadItems(), S_OK);
        UInt32 count = 0;
        QCOMPARE(opening.Folder->GetNumberOfItems(&count), S_OK);
        UInt32 fileIndex = UInt32(-1);
        for (UInt32 index = 0; index < count; ++index)
            if (QString::fromWCharArray(folder->GetName(index)) == name)
                fileIndex = index;
        QVERIFY(fileIndex != UInt32(-1));
        CMyComPtr<IFolderAltStreams> streams;
        QCOMPARE(opening.Folder.QueryInterface(IID_IFolderAltStreams, &streams), S_OK);
        Int32 supported = 0;
        QCOMPARE(streams->AreAltStreamsSupported(fileIndex, &supported), S_OK);
        QCOMPARE(supported, Int32(1));
        CUIntVector selected;
        folder->GetRealIndices(&fileIndex, 1, false, false, selected);
        QCOMPARE(selected.Size(), 1u);
        folder->GetRealIndices(&fileIndex, 1, true, false, selected);
        QCOMPARE(selected.Size(), 2u);
        CMyComPtr<IFolderFolder> altFolder;
        QCOMPARE(streams->BindToAltStreams(fileIndex, &altFolder), S_OK);
        QVERIFY(altFolder);
        QCOMPARE(altFolder->LoadItems(), S_OK);
        QCOMPARE(altFolder->GetNumberOfItems(&count), S_OK);
        QCOMPARE(count, UInt32(1));
        NWindows::NCOM::CPropVariant altName, altSize, isAlt;
        QCOMPARE(altFolder->GetProperty(0, kpidName, &altName), S_OK);
        QCOMPARE(altName.vt, VARTYPE(VT_BSTR));
        QCOMPARE(QString::fromWCharArray(altName.bstrVal), QStringLiteral("поток δ"));
        QCOMPARE(altFolder->GetProperty(0, kpidSize, &altSize), S_OK);
        QCOMPARE(altSize.vt, VARTYPE(VT_UI8));
        QCOMPARE(altSize.uhVal.QuadPart, UInt64(15));
        QCOMPARE(altFolder->GetProperty(0, kpidIsAltStream, &isAlt), S_OK);
        QCOMPARE(isAlt.boolVal, VARIANT_TRUE);
        auto *altSpec = dynamic_cast<CAgentFolder *>(altFolder.Interface());
        QVERIFY(altSpec);
        const int realStreamIndex = altSpec->GetRealIndex(0);
        QVERIFY(realStreamIndex >= 0);
        auto *extract = new CExtractToMemory;
        CMyComPtr<IArchiveExtractCallback> extractOwner = extract;
        const UInt32 streamIndex = UInt32(realStreamIndex);
        IInArchive *archive = folder->GetArchiveProperties();
        auto extracted = std::async(
            std::launch::async, [&] { return archive->Extract(&streamIndex, 1, 0, extractOwner); });
        QTRY_VERIFY_WITH_TIMEOUT(
            extracted.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready, 10000);
        QCOMPARE(extracted.get(), S_OK);
        QCOMPARE(QByteArray(reinterpret_cast<const char *>(extract->Stream->GetBuffer()),
                            qsizetype(extract->Stream->GetSize())),
                 QByteArray("alternate bytes"));
        CMyComPtr<IFolderFolder> parent;
        QCOMPARE(altFolder->BindToParentFolder(&parent), S_OK);
        QVERIFY(parent);
        const UString streamFolderPath = GetFolderPath(altFolder);
        CMyComPtr<IFolderFolder> reopened;
        QCOMPARE(ReOpenArchiveFolder(*altSpec, QString(), nullptr, &reopened, false, UString()),
                 S_OK);
        QVERIFY(reopened);
        QCOMPARE(QString::fromWCharArray(GetFolderPath(reopened)),
                 QString::fromWCharArray(streamFolderPath));
        QCOMPARE(reopened->LoadItems(), S_OK);
        QCOMPARE(reopened->GetNumberOfItems(&count), S_OK);
        QCOMPARE(count, UInt32(1));
        QCOMPARE(reopened->GetProperty(0, kpidName, &altName), S_OK);
        QCOMPARE(QString::fromWCharArray(altName.bstrVal), QStringLiteral("поток δ"));
        QCOMPARE(reopened->GetProperty(0, kpidIsAltStream, &isAlt), S_OK);
        QCOMPARE(isAlt.boolVal, VARIANT_TRUE);
        // Rollback must close the physical reader before replacing its file.
        // The next open must retain the ADS folder even when raw names belonged
        // to the now-closed native handler.
        auto *reopenedSpec = dynamic_cast<CAgentFolder *>(reopened.Interface());
        QVERIFY(reopenedSpec);
        QCOMPARE(reopenedSpec->CloseArchive(), S_OK);
        QCOMPARE(GetFolderPath(reopened), streamFolderPath);
        CMyComPtr<IFolderFolder> afterClose;
        QCOMPARE(
            ReOpenArchiveFolder(*reopenedSpec, QString(), nullptr, &afterClose, false, UString()),
            S_OK);
        QCOMPARE(GetFolderPath(afterClose), streamFolderPath);
        QCOMPARE(afterClose->LoadItems(), S_OK);
        QCOMPARE(afterClose->GetNumberOfItems(&count), S_OK);
        QCOMPARE(count, UInt32(1));
        QCOMPARE(afterClose->GetProperty(0, kpidName, &altName), S_OK);
        QCOMPARE(QString::fromWCharArray(altName.bstrVal), QStringLiteral("поток δ"));
        QFile verify(base);
        QVERIFY(verify.open(QIODevice::ReadOnly));
        QCOMPARE(verify.readAll(), QByteArray("main bytes"));
    }
#endif

    void workerCancelAndNativePassword_data() {
        QTest::addColumn<bool>("cancel");
        QTest::newRow("native cancel before process") << true;
        QTest::newRow("native encrypted headers password") << false;
    }
    void compressedTarRetainsSourceGzipLevel() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QFile input(fixture.filePath(QStringLiteral("вложенный 空.bin")));
        QVERIFY(input.open(QIODevice::WriteOnly));
        QCOMPARE(input.write("nested tar bytes"), qint64(16));
        input.close();
        const QString tar = fixture.filePath("inner.tar");
        const QString path = fixture.filePath("outer.tgz");
        for (const auto &command : {QStringList{"a", "-ttar", "--", tar, input.fileName()},
                                    QStringList{"a", "-tgzip", "--", path, tar}}) {
            QProcess creator;
            creator.start(engine_, command);
            QVERIFY(creator.waitForFinished(10000));
            QCOMPARE(creator.exitCode(), 0);
        }
        CObservedBackend backend(nullptr, engine_);
        QCOMPARE(backend.loadCodecCatalog(module_, version_), S_OK);
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        CFfpOpen opening(manager);
        QCOMPARE(opening.OpenFileFolderPlugin(nullptr, us2fs(path.toStdWString().c_str()),
                                              UString(), UString(), nullptr),
                 S_OK);
        auto *folder = dynamic_cast<CAgentFolder *>(opening.Folder.Interface());
        QVERIFY(folder);
        auto *resource = dynamic_cast<QtArchiveFileResource *>(folder->GetEngineResource());
        QVERIFY(resource);
        // Upstream only recurses when IInArchiveGetStream supplies IInStream.
        // This gzip module exposes its TAR item, not a seekable decoded stream.
        QCOMPARE(resource->Info.format, QString("gzip"));
        QVERIFY(!resource->RequiresOuterOpen());
        QVERIFY(resource->HasUpdateService());
        NWindows::NCOM::CPropVariant readOnly;
        QCOMPARE(folder->GetFolderProperty(kpidReadOnly, &readOnly), S_OK);
        QCOMPARE(readOnly.vt, VARTYPE(VT_BOOL));
        QCOMPARE(readOnly.boolVal, VARIANT_FALSE);
        QFile tarBytes(tar);
        QVERIFY(tarBytes.open(QIODevice::ReadOnly));
        extractToMemory(folder->GetArchiveProperties(), tarBytes.readAll());
        QCOMPARE(folder->GetArchiveProperties()->Close(), S_OK);
        QVERIFY(!resource->RequiresOuterOpen());
        QVERIFY(resource->HasUpdateService());
    }
    void nativeSplitChainKeepsReadOnlyStateAfterClose() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QFile input(fixture.filePath(QStringLiteral("тома 空.bin")));
        QVERIFY(input.open(QIODevice::WriteOnly));
        const QByteArray payload(4096, 'v');
        QCOMPARE(input.write(payload), qint64(payload.size()));
        input.close();
        const QString base = fixture.filePath("volumes.7z");
        const QString path = base + ".001";
        QProcess creator;
        creator.start(engine_, {"a", "-t7z", "-mx0", "-v1024b", "--", base, input.fileName()});
        QVERIFY(creator.waitForFinished(10000));
        QCOMPARE(creator.exitCode(), 0);
        QVERIFY(QFileInfo::exists(base + ".003"));
        CObservedBackend backend(nullptr, engine_);
        QCOMPARE(backend.loadCodecCatalog(module_, version_), S_OK);
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        CFfpOpen opening(manager);
        QCOMPARE(opening.OpenFileFolderPlugin(nullptr, us2fs(path.toStdWString().c_str()),
                                              UString(), UString(), nullptr),
                 S_OK);
        auto *folder = dynamic_cast<CAgentFolder *>(opening.Folder.Interface());
        QVERIFY(folder);
        auto *resource = dynamic_cast<QtArchiveFileResource *>(folder->GetEngineResource());
        QVERIFY(resource);
        QCOMPARE(resource->Info.format, QString("7z"));
        QVERIFY(resource->RequiresOuterOpen());
        QVERIFY(!resource->HasUpdateService());
        NWindows::NCOM::CPropVariant readOnly;
        QCOMPARE(folder->GetFolderProperty(kpidReadOnly, &readOnly), S_OK);
        QCOMPARE(readOnly.vt, VARTYPE(VT_BOOL));
        QCOMPARE(readOnly.boolVal, VARIANT_TRUE);
        extractToMemory(folder->GetArchiveProperties(), payload);
        QCOMPARE(folder->GetArchiveProperties()->Close(), S_OK);
        QVERIFY(resource->RequiresOuterOpen());
        QVERIFY(!resource->HasUpdateService());
    }
    void workerCancelAndNativePassword() {
        QFETCH(bool, cancel);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QFile input(fixture.filePath(QStringLiteral("пароль 空.bin")));
        QVERIFY(input.open(QIODevice::WriteOnly));
        QCOMPARE(input.write("protected fixture"), qint64(17));
        input.close();
        const QString path = fixture.filePath("encrypted.unknown");
        QVERIFY(createFixture(path, input.fileName(), true));
        QFile bytes(path);
        QVERIFY(bytes.open(QIODevice::ReadOnly));
        const QByteArray original = bytes.readAll();
        bytes.close();
        CObservedBackend backend(nullptr, engine_);
        QCOMPARE(backend.loadCodecCatalog(module_, version_), S_OK);
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        auto *progress = new COpenProgress;
        CMyComPtr<IProgress> progressOwner = progress;
        progress->Cancel = cancel;
        CMyComPtr<IFolderFolder> folder;
        auto result = std::async(std::launch::async, [&] {
            return manager->OpenFolderFile(nullptr, path.toStdWString().c_str(), L"7z", &folder,
                                           progressOwner);
        });
        QTRY_VERIFY_WITH_TIMEOUT(
            result.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready, 10000);
        QCOMPARE(result.get(), cancel ? E_ABORT : S_OK);
        QCOMPARE(backend.Starts, 0u);
        QCOMPARE(bool(folder), !cancel);
        QCOMPARE(progress->PasswordCalls, cancel ? 0u : 1u);
        QVERIFY(bytes.open(QIODevice::ReadOnly));
        QCOMPARE(bytes.readAll(), original);
    }
};
QTEST_MAIN(NativeFolderOpeningTests)
#include "native_folder_opening_tests.moc"
