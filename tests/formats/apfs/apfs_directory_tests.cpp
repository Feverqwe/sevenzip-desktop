#include "7zip/Common/StreamUtils.h"
#include "Windows/PropVariant.h"
#include "archive/backend/archivebackend.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/opening/archivedirectoryaccess.h"
#include "archive/resources/archivenativeopen.h"
#include "archive/resources/archivereadonlyview.h"
#include "archive/resources/qtarchivefileresource.h"
#include "support/codec_runtime.h"
#include <QFile>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

namespace {
class Source final : public IInStream, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_2(IInStream, ISequentialInStream)
    QByteArray bytes_;
    UInt64 position_ = 0;

  public:
    UInt64 Bytes = 0, Fault = UINT64_MAX;
    QSet<quint64> Blocks;
    explicit Source(QByteArray bytes) : bytes_(std::move(bytes)) {}
    Z7_COM7F_IMF(Read(void *data, UInt32 size, UInt32 *processed)) override {
        if (processed)
            *processed = 0;
        const UInt32 count =
            position_ >= UInt64(bytes_.size())
                ? 0
                : UInt32(std::min<UInt64>(size, UInt64(bytes_.size()) - position_));
        if (count && position_ / 4096 <= Fault && (position_ + count - 1) / 4096 >= Fault)
            return E_FAIL;
        for (UInt64 n = position_ / 4096; count && n <= (position_ + count - 1) / 4096; ++n)
            Blocks.insert(n);
        if (count)
            memcpy(data, bytes_.constData() + position_, count);
        position_ += count;
        Bytes += count;
        if (processed)
            *processed = count;
        return S_OK;
    }
    Z7_COM7F_IMF(Seek(Int64 offset, UInt32 origin, UInt64 *position)) override {
        const UInt64 base = origin == STREAM_SEEK_SET   ? 0
                            : origin == STREAM_SEEK_CUR ? position_
                                                        : UInt64(bytes_.size());
        if (origin > STREAM_SEEK_END || (offset < 0 && UInt64(-(offset + 1)) + 1 > base))
            return E_INVALIDARG;
        position_ = base + UInt64(offset);
        if (position)
            *position = position_;
        return S_OK;
    }
};
QString Property(IInArchive *archive, UInt32 index, PROPID id) {
    NWindows::NCOM::CPropVariant value;
    return archive->GetProperty(index, id, &value) == S_OK && value.vt == VT_BSTR
               ? QString::fromWCharArray(value.bstrVal)
               : QString();
}
UInt32 Find(IInArchive *archive, const QString &path) {
    UInt32 count = 0;
    if (archive->GetNumberOfItems(&count) != S_OK)
        return UINT32_MAX;
    for (UInt32 i = 0; i < count; ++i)
        if (Property(archive, i, kpidPath) == path)
            return i;
    return UINT32_MAX;
}
QSet<QString> Paths(IInArchive *archive) {
    QSet<QString> paths;
    UInt32 count = 0;
    archive->GetNumberOfItems(&count);
    for (UInt32 i = 0; i < count; ++i)
        paths.insert(Property(archive, i, kpidPath));
    return paths;
}
QByteArray ReadFile(IInArchive *archive, UInt32 index) {
    CMyComPtr<IInArchiveGetStream> access;
    archive->QueryInterface(IID_IInArchiveGetStream, (void **)&access);
    CMyComPtr<ISequentialInStream> stream;
    if (!access || access->GetStream(index, &stream) != S_OK || !stream)
        return {};
    QByteArray bytes(4096, '\0');
    size_t size = size_t(bytes.size());
    if (ReadStream(stream, bytes.data(), &size) != S_OK)
        return {};
    bytes.resize(qsizetype(size));
    return bytes;
}
class Cancel final : public IProgress, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_1(IProgress)
    unsigned remaining_ = 8;
    Z7_COM7F_IMF(SetTotal(UInt64)) override { return S_OK; }
    Z7_COM7F_IMF(SetCompleted(const UInt64 *)) override { return --remaining_ ? S_OK : E_ABORT; }
};
} // namespace

class ApfsDirectoryTests final : public QObject {
    Q_OBJECT
    QTemporaryDir runtime_;
    std::shared_ptr<const ArchiveCodecCatalog> catalog_;
    QByteArray image_;
    HRESULT Open(IInStream *input, bool lazy, std::shared_ptr<ArchiveNativeOpen> &reader) {
        COpenOptions options;
        CObjectVector<COpenType> types;
        CIntVector excluded;
        if (!catalog_->parseOpenTypes("APFS", types))
            return E_FAIL;
        options.props = nullptr;
        options.types = &types;
        options.excludedFormats = &excluded;
        options.stream = input;
        options.PreferDirectoryRead = lazy;
        return ArchiveBackend::openNativeArchive(catalog_, "memory.apfs", "APFS", nullptr, reader,
                                                 &options);
    }
  private slots:
    void initTestCase() {
        QFile fixture(QString::fromUtf8(SEVENZIP_SPARSE_FIXTURES) + "/apfs-directories.img.z");
        QVERIFY(fixture.open(QIODevice::ReadOnly));
        image_ = qUncompress(fixture.readAll());
        QVERIFY(!image_.isEmpty());
        QVERIFY(
            CopyCodecRuntime(QDir(QString::fromUtf8(SEVENZIP_DMG_RUNTIME)), QDir(runtime_.path())));
#ifdef Q_OS_WIN
        const auto engine = runtime_.filePath("7z.exe"), module = runtime_.filePath("7z.dll");
#else
        const auto engine = runtime_.filePath("7z"), module = runtime_.filePath("7z.so");
#endif
        ArchiveBackend backend(nullptr, engine);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(module));
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 15000);
        QVERIFY(ready[0][0].toBool());
        catalog_ = backend.codecCatalog();
    }
    void rootAndExplicitTraversal() {
        CMyComPtr2<IInStream, Source> eager, lazy;
        eager.SetFromCls(new Source(image_));
        lazy.SetFromCls(new Source(image_));
        std::shared_ptr<ArchiveNativeOpen> all, root;
        QCOMPARE(Open(eager, false, all), S_OK);
        QCOMPARE(Open(lazy, true, root), S_OK);
        auto *archive = root->arc().Archive.Interface();
        const auto expected = Paths(all->arc().Archive);
        const auto first = Paths(archive);
        QVERIFY(first.contains("root.txt"));
        QVERIFY(first.contains("directory-00"));
        QVERIFY(!first.contains("directory-00/file-000.txt"));
        QVERIFY(first.size() * 8 < expected.size());
        qInfo() << "Eager bytes" << eager->Bytes << "root bytes" << lazy->Bytes;
        QVERIFY(lazy->Bytes < eager->Bytes);
        const auto directory = Find(archive, "directory-00"), rootFile = Find(archive, "root.txt");
        CMyComPtr<IArchiveDirectoryAccess> access;
        archive->QueryInterface(IID_IArchiveDirectoryAccess, (void **)&access);
        QVERIFY(access);
        CMyComPtr<IProgress> cancel = new Cancel;
        QCOMPARE(access->LoadDirectory(directory, false, cancel), E_ABORT);
        QCOMPARE(Paths(archive), first);
        QCOMPARE(access->LoadDirectory(directory, false, nullptr), S_OK);
        QVERIFY(Find(archive, "directory-00/file-000.txt") != UINT32_MAX);
        QVERIFY(Find(archive, "directory-00/nested") != UINT32_MAX);
        QCOMPARE(Find(archive, "directory-00/nested/данные 空.txt"), UINT32_MAX);
        QCOMPARE(Find(archive, "root.txt"), rootFile);
        QCOMPARE(ReadFile(archive, rootFile), QByteArray("APFS root payload\n"));
        const auto before = lazy->Bytes;
        QCOMPARE(access->LoadDirectory(directory, false, nullptr), S_OK);
        QCOMPARE(lazy->Bytes, before);
        QCOMPARE(access->LoadDirectory(rootFile, false, nullptr), E_INVALIDARG);
        CMyComPtr2<IUnknown, QtArchiveFileResource> resource;
        resource.SetFromCls(new QtArchiveFileResource("memory.apfs"));
        resource->AttachNativeOpen(root);
        resource->UseNativeMetadata();
        QString error;
        std::shared_ptr<ArchiveReadOnlyView> view;
        QCOMPARE(ArchiveBackend::createReadOnlyView(resource, 0, {}, view, error, {}, {},
                                                    "directory-00"),
                 S_OK);
        QVERIFY(Find(archive, "directory-00/nested/данные 空.txt") != UINT32_MAX);
        QCOMPARE(Find(archive, "directory-11/file-000.txt"), UINT32_MAX);
        QCOMPARE(access->LoadDirectory(UINT32_MAX, true, nullptr), S_OK);
        qInfo() << "Directory workflow input bytes" << lazy->Bytes << "eager bytes" << eager->Bytes;
        QVERIFY2(lazy->Bytes <= eager->Bytes * 4,
                 "Directory enumeration repeatedly rereads the same metadata pages");
        QCOMPARE(Paths(archive), expected);
        QCOMPARE(Property(archive, Find(archive, "directory-02/root-link"), kpidSymLink),
                 QString("../root.txt"));
        QCOMPARE(ReadFile(archive, Find(archive, "directory-00/nested/данные 空.txt")),
                 QByteArray("Nested APFS payload\n"));
    }
    void lazyMountReadsOnlyRequestedDirectories() {
        CMyComPtr2<IInStream, Source> source;
        source.SetFromCls(new Source(image_));
        std::shared_ptr<ArchiveNativeOpen> reader;
        QCOMPARE(Open(source, true, reader), S_OK);
        CMyComPtr2<IUnknown, QtArchiveFileResource> resource;
        resource.SetFromCls(new QtArchiveFileResource("memory.apfs"));
        resource->AttachNativeOpen(reader);
        resource->UseNativeMetadata();
        std::shared_ptr<ArchiveReadOnlyView> view;
        QString error;
        QCOMPARE(ArchiveBackend::createReadOnlyView(resource, 0, {}, view, error, {}, {}, {}, true),
                 S_OK);
        const auto before = source->Bytes;
        bool cache = false;
        QCOMPARE(view->needsCache(cache), S_OK);
        QCOMPARE(source->Bytes, before);
        QCOMPARE(view->entries().size(), 1);
        const auto root = view->listDirectory(1);
        QCOMPARE(root.Result, S_OK);
        quint64 directory = 0;
        for (const auto &entry : root.Entries)
            if (entry.Name == "directory-00")
                directory = entry.Id;
        QVERIFY(directory);
        const auto listed = view->listDirectory(directory);
        QCOMPARE(listed.Result, S_OK);
        quint64 nested = 0;
        for (const auto &entry : listed.Entries)
            if (entry.Name == "nested")
                nested = entry.Id;
        QVERIFY(nested);
        QCOMPARE(Find(reader->arc().Archive, "directory-00/nested/данные 空.txt"), UINT32_MAX);
        QCOMPARE(Find(reader->arc().Archive, "directory-11/file-000.txt"), UINT32_MAX);
        const auto cached = source->Bytes;
        QCOMPARE(view->listDirectory(directory).Result, S_OK);
        QCOMPARE(source->Bytes, cached);
        const auto deep = view->listDirectory(nested);
        QCOMPARE(deep.Result, S_OK);
        QCOMPARE(deep.Entries.size(), 1);
        const auto payload = view->read(deep.Entries[0].Id, 0, 4096);
        QCOMPARE(payload.Result, S_OK);
        QCOMPARE(payload.Data, QByteArray("Nested APFS payload\n"));
        // Background mounting must not mutate the panel's native metadata.
        QCOMPARE(Find(reader->arc().Archive, "directory-00/file-000.txt"), UINT32_MAX);
        view->cancel();
        QCOMPARE(view->listDirectory(directory).Result, E_ABORT);
    }
    void deferredFailureAndRetry() {
        CMyComPtr2<IInStream, Source> reference;
        reference.SetFromCls(new Source(image_));
        std::shared_ptr<ArchiveNativeOpen> reader;
        QCOMPARE(Open(reference, true, reader), S_OK);
        const auto rootBlocks = reference->Blocks;
        CMyComPtr<IArchiveDirectoryAccess> access;
        reader->arc().Archive.QueryInterface(IID_IArchiveDirectoryAccess, (void **)&access);
        const auto directory = Find(reader->arc().Archive, "directory-00");
        QCOMPARE(access->LoadDirectory(directory, false, nullptr), S_OK);
        const auto deferred = reference->Blocks - rootBlocks;
        QVERIFY(!deferred.isEmpty());
        CMyComPtr2<IInStream, Source> faulty;
        faulty.SetFromCls(new Source(image_));
        faulty->Fault = *deferred.cbegin();
        std::shared_ptr<ArchiveNativeOpen> retry;
        QCOMPARE(Open(faulty, true, retry), S_OK);
        CMyComPtr2<IUnknown, QtArchiveFileResource> mountResource;
        mountResource.SetFromCls(new QtArchiveFileResource("memory.apfs"));
        mountResource->AttachNativeOpen(retry);
        mountResource->UseNativeMetadata();
        std::shared_ptr<ArchiveReadOnlyView> mount;
        QString error;
        QCOMPARE(ArchiveBackend::createReadOnlyView(mountResource, 0, {}, mount, error, {}, {}, {},
                                                    true),
                 S_OK);
        const auto mountRoot = mount->listDirectory(1);
        QCOMPARE(mountRoot.Result, S_OK);
        quint64 mountDirectory = 0;
        for (const auto &entry : mountRoot.Entries)
            if (entry.Name == "directory-00")
                mountDirectory = entry.Id;
        QVERIFY(mountDirectory);
        const auto published = mount->entries().size();
        QCOMPARE(mount->listDirectory(mountDirectory).Result, E_FAIL);
        QCOMPARE(mount->entries().size(), published);
        const auto before = Paths(retry->arc().Archive);
        CMyComPtr<IArchiveDirectoryAccess> retryAccess;
        retry->arc().Archive.QueryInterface(IID_IArchiveDirectoryAccess, (void **)&retryAccess);
        QCOMPARE(retryAccess->LoadDirectory(directory, false, nullptr), E_FAIL);
        QCOMPARE(Paths(retry->arc().Archive), before);
        BoolInt loaded = true;
        QCOMPARE(retryAccess->IsDirectoryLoaded(directory, false, &loaded), S_OK);
        QVERIFY(!loaded);
        faulty->Fault = UINT64_MAX;
        QCOMPARE(mount->listDirectory(mountDirectory).Result, S_OK);
        QCOMPARE(retryAccess->LoadDirectory(directory, false, nullptr), S_OK);
        QCOMPARE(
            ReadFile(retry->arc().Archive, Find(retry->arc().Archive, "directory-00/file-000.txt")),
            QByteArray("Directory 0, file 0\n"));
    }
};
QTEST_GUILESS_MAIN(ApfsDirectoryTests)
#include "apfs_directory_tests.moc"
