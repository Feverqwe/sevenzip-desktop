#include "7zip/Common/StreamObjects.h"
#include "7zip/Common/StreamUtils.h"
#include "7zip/IPassword.h"
#include "Windows/PropVariant.h"
#include "archive/backend/archivebackend.h"
#include "archive/callbacks/archivepasswordretry.h"
#include "archive/resources/archivenativeopen.h"
#include "archive/resources/archivereadonlyview.h"
#include "archive/resources/qtarchivefileresource.h"
#include "formats/encrypted-dmg/dmgfixtures.h"
#include "formats/exfat/support/exfat_fixture.h"
#include "formats/sparse-image/sparsebundlestream.h"
#include "formats/sparse-image/sparseimagestream.h"
#include "sparsefixtures.h"
#include "support/codec_runtime.h"
#include <QCryptographicHash>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

namespace {
CMyComPtr<IInStream> Input(const QByteArray &bytes) {
    CMyComPtr<ISequentialInStream> buffer;
    Create_BufInStream_WithNewBuffer(bytes.constData(), size_t(bytes.size()), &buffer);
    CMyComPtr<IInStream> stream;
    buffer.QueryInterface(IID_IInStream, &stream);
    return stream;
}
QByteArray Pattern(qsizetype size) {
    QByteArray result(size, '\0');
    quint32 value = 731;
    for (char &c : result) {
        value = value * 1664525 + 1013904223;
        c = char(value >> 24);
    }
    return result;
}
QByteArray Sparse(const QByteArray &plain, unsigned band = 4096, bool index = false) {
    const qsizetype base = index ? 8192 : 4096;
    const qsizetype count = (plain.size() + band - 1) / band;
    QByteArray out(base + count * band, '\0');
    memcpy(out.data(), "sprs", 4);
    qToBigEndian<quint32>(3, out.data() + 4);
    qToBigEndian<quint32>(band / 512, out.data() + 8);
    qToBigEndian<quint64>(quint64(plain.size() / 512), out.data() + 28);
    if (index) {
        qToBigEndian<quint64>(4096, out.data() + 20);
        memcpy(out.data() + 4096, "sprs", 4);
    }
    for (qsizetype i = 0; i < count; ++i) {
        const auto bytes = plain.mid(i * band, band);
        if (bytes == QByteArray(bytes.size(), '\0'))
            continue;
        qToBigEndian<quint32>(quint32(i + 1), out.data() + (index ? 4096 + 56 : 64) + 4 * i);
        memcpy(out.data() + base + i * band, bytes.constData(), size_t(bytes.size()));
    }
    return out;
}
QByteArray Plist(UInt64 size, UInt64 band) {
    return "<?xml version=\"1.0\"?><plist version=\"1.0\"><dict>"
           "<key>diskimage-bundle-type</key><string>com.apple.diskimage.sparsebundle</string>"
           "<key>band-size</key><integer>" +
           QByteArray::number(band) + "</integer><key>size</key><integer>" +
           QByteArray::number(size) + "</integer></dict></plist>";
}
CMyComPtr<IInStream> FailedSource(const QByteArray &bytes);
CMyComPtr<IInStream> CountedSource(const QByteArray &bytes, std::shared_ptr<UInt64> reads,
                                   std::shared_ptr<UInt64> transferred);
class Volumes final : public IArchiveOpenCallback,
                      public IArchiveOpenVolumeCallback,
                      public ICryptoGetTextPassword,
                      public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_3(IArchiveOpenCallback, IArchiveOpenVolumeCallback, ICryptoGetTextPassword)
  public:
    QMap<QString, QByteArray> Files;
    QString Password = "fixture-password", FailPath, FailReadPath;
    bool Cancel = false;
    int Asked = 0, Opens = 0;
    std::shared_ptr<UInt64> Reads = std::make_shared<UInt64>(0);
    std::shared_ptr<UInt64> Transferred = std::make_shared<UInt64>(0);
    Z7_COM7F_IMF(SetTotal(const UInt64 *, const UInt64 *)) override { return S_OK; }
    Z7_COM7F_IMF(SetCompleted(const UInt64 *, const UInt64 *)) override {
        return Cancel ? E_ABORT : S_OK;
    }
    Z7_COM7F_IMF(CryptoGetTextPassword(BSTR *value)) override {
        ++Asked;
        return Cancel ? E_ABORT : StringToBstr(Password.toStdWString().c_str(), value);
    }
    Z7_COM7F_IMF(GetProperty(PROPID id, PROPVARIANT *value)) override {
        NWindows::NCOM::CPropVariant prop;
        if (id == kpidName)
            prop = L"Info.plist";
        prop.Detach(value);
        return S_OK;
    }
    Z7_COM7F_IMF(GetStream(const wchar_t *name, IInStream **stream)) override {
        *stream = nullptr;
        ++Opens;
        if (Cancel)
            return E_ABORT;
        const auto path = QString::fromWCharArray(name);
        if (path == FailPath)
            return static_cast<HRESULT>(0x80070005L);
        const auto found = Files.constFind(path);
        if (found == Files.cend())
            return S_FALSE;
        *stream = (path == FailReadPath ? FailedSource(found.value())
                                        : CountedSource(found.value(), Reads, Transferred))
                      .Detach();
        return S_OK;
    }
};
class OpenPassword final : public IOpenCallbackUI, public IArchivePasswordRetryUI {
  public:
    int Asked = 0, Retries = 0;
    bool WrongFirst = false, Cancel = false;
    HRESULT Open_CheckBreak() override { return Cancel ? E_ABORT : S_OK; }
    HRESULT Open_SetTotal(const UInt64 *, const UInt64 *) override { return S_OK; }
    HRESULT Open_SetCompleted(const UInt64 *, const UInt64 *) override { return Open_CheckBreak(); }
    HRESULT Open_Finished() override { return S_OK; }
    HRESULT Open_CryptoGetTextPassword(BSTR *value) override {
        ++Asked;
        return Cancel
                   ? E_ABORT
                   : StringToBstr(WrongFirst && Asked == 1 ? L"wrong" : L"fixture-password", value);
    }
    HRESULT ResetArchivePassword() override {
        ++Retries;
        return S_OK;
    }
};
QByteArray FileBytes(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
class FailedRead final : public IInStream, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_2(IInStream, ISequentialInStream)
    CMyComPtr<IInStream> input_;

  public:
    explicit FailedRead(IInStream *input) : input_(input) {}
    Z7_COM7F_IMF(Read(void *, UInt32, UInt32 *processed)) override {
        if (processed)
            *processed = 0;
        return E_FAIL;
    }
    Z7_COM7F_IMF(Seek(Int64 offset, UInt32 origin, UInt64 *position)) override {
        return input_->Seek(offset, origin, position);
    }
};
CMyComPtr<IInStream> FailedSource(const QByteArray &bytes) { return new FailedRead(Input(bytes)); }
class CountedRead final : public IInStream, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_2(IInStream, ISequentialInStream)
    CMyComPtr<IInStream> input_;
    std::shared_ptr<UInt64> reads_;
    std::shared_ptr<UInt64> transferred_;

  public:
    CountedRead(IInStream *input, std::shared_ptr<UInt64> reads,
                std::shared_ptr<UInt64> transferred)
        : input_(input), reads_(std::move(reads)), transferred_(std::move(transferred)) {}
    Z7_COM7F_IMF(Read(void *data, UInt32 size, UInt32 *processed)) override {
        ++*reads_;
        UInt32 count = 0;
        const auto result = input_->Read(data, size, &count);
        *transferred_ += count;
        if (processed)
            *processed = count;
        return result;
    }
    Z7_COM7F_IMF(Seek(Int64 offset, UInt32 origin, UInt64 *position)) override {
        return input_->Seek(offset, origin, position);
    }
};
CMyComPtr<IInStream> CountedSource(const QByteArray &bytes, std::shared_ptr<UInt64> reads,
                                   std::shared_ptr<UInt64> transferred) {
    return new CountedRead(Input(bytes), std::move(reads), std::move(transferred));
}
} // namespace

class SparseImageTests final : public QObject {
    Q_OBJECT
    QTemporaryDir runtime_;
    QString engine_, module_;
    std::shared_ptr<const ArchiveCodecCatalog> catalog_;
    void Check(IInStream *stream, const QByteArray &plain) {
        for (qsizetype offset :
             {qsizetype(0), qsizetype(1), qsizetype(511), qsizetype(4093), qsizetype(8191),
              qsizetype(16381), qsizetype(0), plain.size() - 1, plain.size(), plain.size() + 3}) {
            QCOMPARE(stream->Seek(offset, STREAM_SEEK_SET, nullptr), S_OK);
            QByteArray bytes(5003, '\0');
            size_t size = size_t(bytes.size());
            QCOMPARE(ReadStream(stream, bytes.data(), &size), S_OK);
            bytes.resize(qsizetype(size));
            QCOMPARE(bytes, plain.mid(qMin(offset, plain.size()), 5003));
        }
        QCOMPARE(stream->Seek(-1, STREAM_SEEK_SET, nullptr), HRESULT_WIN32_ERROR_NEGATIVE_SEEK);
        QCOMPARE(stream->Seek(-13, STREAM_SEEK_END, nullptr), S_OK);
        char bytes[100];
        size_t size = sizeof(bytes);
        QCOMPARE(ReadStream(stream, bytes, &size), S_OK);
        QCOMPARE(QByteArray(bytes, qsizetype(size)), plain.right(13));
    }
  private slots:
    void initTestCase() {
        QVERIFY(runtime_.isValid());
        QVERIFY(
            CopyCodecRuntime(QDir(QString::fromUtf8(SEVENZIP_DMG_RUNTIME)), QDir(runtime_.path())));
        QVERIFY(QDir(runtime_.path()).mkpath("Formats"));
#ifdef Q_OS_WIN
        const QString engine = "7z.exe", module = "7z.dll", suffix = ".dll";
#else
        const QString engine = "7z", module = "7z.so", suffix = ".so";
#endif
        engine_ = runtime_.filePath(engine);
        module_ = runtime_.filePath(module);
        QVERIFY(QFile::copy(QString::fromUtf8(SEVENZIP_DMG_MODULE),
                            runtime_.filePath("Formats/EncryptedDmg" + suffix)));
        QVERIFY(QFile::copy(QString::fromUtf8(SEVENZIP_DMG_HFS_MODULE),
                            runtime_.filePath("Formats/Hfs" + suffix)));
        QVERIFY(QFile::copy(QString::fromUtf8(SEVENZIP_DMG_EXFAT_MODULE),
                            runtime_.filePath("Formats/Exfat" + suffix)));
        ArchiveBackend backend(nullptr, engine_);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(runtime_.filePath(module)));
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 15000);
        QVERIFY2(ready[0][0].toBool(), qPrintable(ready[0][1].toString()));
        catalog_ = backend.codecCatalog();
    }
    void sparseMapping() {
        auto plain = Pattern(32768);
        plain.replace(4096, 8192, QByteArray(8192, '\0'));
        for (unsigned band : {4096u, 6144u, 8192u})
            for (bool index : {false, true}) {
                const auto bytes = Sparse(plain, band, index);
                CMyComPtr2_Create<IInStream, NArchive::NSparseImage::SparseImageStream> stream;
                QCOMPARE(stream->Open(Input(bytes), nullptr), S_OK);
                Check(stream, plain);
                for (unsigned bits : {128u, 256u})
                    for (bool aes : {false, true}) {
                        CMyComPtr2_Create<IArchiveOpenCallback, Volumes> password;
                        CMyComPtr2_Create<IInStream, NArchive::NEncryptedDmg::DecryptStream> crypto;
                        QCOMPARE(crypto->Open(Input(DmgFixture(bytes, bits, aes)), password), S_OK);
                        QCOMPARE(stream->Open(crypto, password), S_OK);
                        Check(stream, plain);
                    }
            }
    }
    void bundleMapping() {
        auto plain = Pattern(32768);
        plain.replace(8192, 8192, QByteArray(8192, '\0'));
        for (unsigned bits : {0u, 128u, 256u})
            for (bool aes : {false, true}) {
                CMyComPtr2_Create<IArchiveOpenCallback, Volumes> volumes;
                const auto plist = Plist(UInt64(plain.size()), 8192);
                volumes->Files["token"] = bits ? DmgFixture({}, bits, aes) : QByteArray();
                for (unsigned band : {0u, 2u, 3u}) {
                    const auto bytes = plain.mid(band * 8192, 8192);
                    volumes->Files["bands/" + QString::number(band, 16)] =
                        bits ? DmgFixture(bytes, bits, aes).mid(4096) : bytes;
                }
                CMyComPtr2_Create<IInStream, NArchive::NSparseImage::SparseBundleStream> stream;
                QCOMPARE(stream->Open(Input(plist), volumes), S_OK);
                Check(stream, plain);
                QCOMPARE(volumes->Asked, bits ? 1 : 0);
                volumes->FailPath = "bands/0";
                QCOMPARE(stream->Open(Input(plist), volumes), S_OK);
                UInt32 count = 111;
                char buffer[32];
                QCOMPARE(stream.Interface()->Read(buffer, sizeof(buffer), &count),
                         static_cast<HRESULT>(0x80070005L));
                QCOMPARE(count, UInt32(0));
                volumes->FailPath.clear();
                volumes->FailReadPath = "bands/0";
                QCOMPARE(stream->Open(Input(plist), volumes), S_OK);
                QCOMPARE(stream.Interface()->Read(buffer, sizeof(buffer), &count), E_FAIL);
                QCOMPARE(count, UInt32(0));
                volumes->FailReadPath.clear();
                // A transient failed band handle must not poison this opening.
                QCOMPARE(stream.Interface()->Read(buffer, sizeof(buffer), &count), S_OK);
                QCOMPARE(QByteArray(buffer, int(count)), plain.left(int(count)));
                QCOMPARE(stream->Open(Input(plist), volumes), S_OK);
                volumes->Cancel = true;
                QCOMPARE(stream.Interface()->Read(buffer, sizeof(buffer), &count), E_ABORT);
                volumes->Cancel = false;
                if (bits) {
                    volumes->Password = "wrong";
                    QCOMPARE(stream->Open(Input(plist), volumes), kArchiveWrongPassword);
                    volumes->Password = "fixture-password";
                    QCOMPARE(stream->Open(Input(plist), volumes), S_OK);
                    volumes->Cancel = true;
                    QCOMPARE(stream->Open(Input(plist), volumes), E_ABORT);
                }
            }
    }
    void malformedIndex() {
        auto bytes = Sparse(Pattern(16384), 4096, true);
        CMyComPtr2_Create<IInStream, NArchive::NSparseImage::SparseImageStream> stream;
        const auto check = [&](QByteArray damaged) {
            QVERIFY(stream->Open(Input(damaged), nullptr) != S_OK);
        };
        auto damaged = bytes;
        qToBigEndian<quint64>(4096, damaged.data() + 4096 + 12);
        check(damaged); // index cycle
        damaged = bytes;
        qToBigEndian<quint32>(UINT32_MAX, damaged.data() + 4096 + 56);
        check(damaged); // out-of-range logical band
        damaged = bytes;
        qToBigEndian<quint32>(1, damaged.data() + 4096 + 60);
        check(damaged); // duplicate band
        check(bytes.left(bytes.size() - 1));
        check(bytes.left(80));
        damaged = bytes;
        qToBigEndian<quint32>(0, damaged.data() + 8);
        check(damaged);
    }
    void bufferedBandTransport() {
        const auto plain = Pattern(2u << 20);
        CMyComPtr2_Create<IArchiveOpenCallback, Volumes> volumes;
        volumes->Files["token"] = DmgFixture({}, 256, true);
        volumes->Files["bands/0"] = DmgFixture(plain.left(1u << 20), 256, true).mid(4096);
        volumes->Files["bands/1"] = DmgFixture(plain.mid(1u << 20), 256, true).mid(4096);
        CMyComPtr2_Create<IInStream, NArchive::NSparseImage::SparseBundleStream> stream;
        QCOMPARE(stream->Open(Input(Plist(UInt64(plain.size()), 1u << 20)), volumes), S_OK);
        *volumes->Reads = 0;
        QByteArray data(1u << 20, '\0');
        QCOMPARE(stream.Interface()->Seek((1u << 19) + 13, STREAM_SEEK_SET, nullptr), S_OK);
        QCOMPARE(ReadStream_FALSE(stream, data.data(), size_t(data.size())), S_OK);
        QCOMPARE(data, plain.mid((1u << 19) + 13, data.size()));
        // Alternating filesystem metadata and file data must retain useful
        // buffering across bands on a high-latency source.
        for (int n = 0; n < 24; ++n) {
            const UInt64 offset = UInt64(n % 2) * (1u << 20) + 1024;
            QCOMPARE(stream.Interface()->Seek(Int64(offset), STREAM_SEEK_SET, nullptr), S_OK);
            QByteArray fragment(4096, '\0');
            QCOMPARE(ReadStream_FALSE(stream, fragment.data(), size_t(fragment.size())), S_OK);
            QCOMPARE(fragment, plain.mid(qsizetype(offset), fragment.size()));
        }
        // A slow multi-file source must not incur a physical read per crypto
        // chunk. This deliberately allows many possible bounded cache designs.
        QVERIFY(*volumes->Reads < 128);
    }
    void scatteredMetadataTransport() {
        const auto plain = Pattern(8u << 20);
        CMyComPtr2_Create<IArchiveOpenCallback, Volumes> volumes;
        volumes->Files["token"] = DmgFixture({}, 256, true);
        volumes->Files["bands/0"] = DmgFixture(plain, 256, true).mid(4096);
        CMyComPtr2_Create<IInStream, NArchive::NSparseImage::SparseBundleStream> stream;
        QCOMPARE(stream->Open(Input(Plist(UInt64(plain.size()), UInt64(plain.size()))), volumes),
                 S_OK);
        *volumes->Transferred = 0;
        UInt64 useful = 0;
        for (int n = 0; n < 128; ++n) {
            const UInt64 offset = UInt64((n * 13) % 16) * (1u << 19) + 1024;
            QCOMPARE(stream.Interface()->Seek(Int64(offset), STREAM_SEEK_SET, nullptr), S_OK);
            QByteArray data(512, '\0');
            QCOMPARE(ReadStream_FALSE(stream, data.data(), size_t(data.size())), S_OK);
            QCOMPARE(data, plain.mid(qsizetype(offset), data.size()));
            useful += data.size();
        }
        qInfo() << "Scattered metadata: useful" << useful << "transport" << *volumes->Transferred;
        // Count bytes, not just requests: large speculative fills can saturate
        // SMB while barely advancing useful metadata reading.
        QVERIFY(*volumes->Transferred <= useful * 32);
    }
    void apfsObjectVersions_data() {
        QTest::addColumn<QString>("suffix");
        QTest::addColumn<bool>("readable");
        QTest::newRow("visible-version") << QString() << true;
        QTest::newRow("deleted") << QString("-deleted") << false;
        QTest::newRow("unordered") << QString("-unordered") << false;
    }
    void apfsObjectVersions() {
        QFETCH(QString, suffix);
        QFETCH(bool, readable);
        const auto image = qUncompress(FileBytes(QString::fromUtf8(SEVENZIP_SPARSE_FIXTURES) +
                                                 "/apfs-versions" + suffix + ".img.z"));
        QVERIFY(!image.isEmpty());
        QTemporaryDir scratch;
        const auto path = scratch.filePath("versioned.apfs");
        QFile output(path);
        QVERIFY(output.open(QIODevice::WriteOnly));
        QCOMPARE(output.write(image), image.size());
        output.close();
        std::shared_ptr<ArchiveNativeOpen> reader;
        if (!readable) {
            QVERIFY(ArchiveBackend::openNativeArchive(catalog_, path, "APFS", nullptr, reader,
                                                      nullptr) != S_OK);
            return;
        }
        QCOMPARE(
            ArchiveBackend::openNativeArchive(catalog_, path, "APFS", nullptr, reader, nullptr),
            S_OK);
        QVERIFY(reader->resolvedOpenType().contains("APFS"));
        auto *owner = new QtArchiveFileResource(path);
        CMyComPtr<IUnknown> retained = owner;
        owner->AttachNativeOpen(reader);
        owner->UseNativeMetadata();
        std::shared_ptr<ArchiveReadOnlyView> view;
        QString error;
        QCOMPARE(ArchiveBackend::createReadOnlyView(retained, 0, {}, view, error), S_OK);
        const auto found =
            std::find_if(view->entries().begin(), view->entries().end(), [](const auto &entry) {
                return entry.Name == QString::fromUtf8("данные 空.txt");
            });
        QVERIFY(found != view->entries().end());
        const auto data = view->read(found->Id, 0, 4096);
        QCOMPARE(data.Result, S_OK);
        QCOMPARE(data.Data, QByteArray("APFS generated device payload\n"));
        // Exercise the reported nesting and a multi-terabyte sparse address
        // space, without allocating or reading its empty logical capacity.
        const auto package = scratch.filePath("large encrypted.sparsebundle");
        QVERIFY(QDir().mkpath(package + "/bands"));
        const auto write = [&](const QString &name, const QByteArray &bytes) {
            QFile file(package + '/' + name);
            return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
        };
        QVERIFY(write("Info.plist", Plist(16000000000000ULL, UInt64(image.size()))));
        QVERIFY(write("token", DmgFixture({}, 256, true)));
        QVERIFY(write("bands/0", DmgFixture(image, 256, true).mid(4096)));
        OpenPassword password;
        std::shared_ptr<ArchiveNativeOpen> nested;
        QCOMPARE(
            ArchiveBackend::openNativeArchive(catalog_, package, {}, &password, nested, nullptr),
            S_OK);
        QVERIFY(nested->resolvedOpenType().contains("APFS"));
        auto *nestedOwner = new QtArchiveFileResource(package);
        CMyComPtr<IUnknown> nestedRetained = nestedOwner;
        nestedOwner->AttachNativeOpen(nested);
        nestedOwner->UseNativeMetadata();
        std::shared_ptr<ArchiveReadOnlyView> nestedView;
        QCOMPARE(ArchiveBackend::createReadOnlyView(nestedRetained, 0, {}, nestedView, error),
                 S_OK);
        const auto nestedFile = std::find_if(
            nestedView->entries().begin(), nestedView->entries().end(),
            [](const auto &entry) { return entry.Name == QString::fromUtf8("данные 空.txt"); });
        QVERIFY(nestedFile != nestedView->entries().end());
        const auto nestedData = nestedView->read(nestedFile->Id, 0, 4096);
        QCOMPARE(nestedData.Result, S_OK);
        QCOMPARE(nestedData.Data, data.Data);
    }
    void retainedBundleStream() {
        for (const auto &value : SparseFixtureManifest()["fixtures"].toArray()) {
            const auto fixture = value.toObject();
            if (fixture["type"].toString() != "SPARSEBUNDLE")
                continue;
            QTemporaryDir scratch;
            const auto path = UnpackSparseFixture(fixture, scratch);
            QVERIFY(!path.isEmpty());
            auto password = std::make_unique<OpenPassword>();
            std::shared_ptr<ArchiveNativeOpen> reader;
            QCOMPARE(ArchiveBackend::openNativeArchive(catalog_, path, {}, password.get(), reader,
                                                       nullptr),
                     S_OK);
            CMyComPtr<IInArchiveGetStream> access;
            reader->archiveLink().Arcs[0].Archive.QueryInterface(IID_IInArchiveGetStream, &access);
            QVERIFY(access);
            CMyComPtr<ISequentialInStream> sequential;
            QCOMPARE(access->GetStream(0, &sequential), S_OK);
            CMyComPtr<IInStream> stream;
            QCOMPARE(sequential.QueryInterface(IID_IInStream, &stream), S_OK);
            access.Release();
            QCOMPARE(reader->Close(), S_OK);
            reader.reset();
            password.reset();
            for (const auto &rangeValue : fixture["ranges"].toArray()) {
                const auto range = rangeValue.toObject();
                QCOMPARE(stream->Seek(range["offset"].toInteger(), STREAM_SEEK_SET, nullptr), S_OK);
                QByteArray data(range["size"].toInt(), '\0');
                QCOMPARE(ReadStream_FALSE(stream, data.data(), size_t(data.size())), S_OK);
                QCOMPARE(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex(),
                         range["sha256"].toString().toLatin1());
            }
        }
    }
    void nativeReferences() {
        const auto manifest = SparseFixtureManifest();
        QVERIFY(!manifest.isEmpty());
        for (const auto &value : manifest["fixtures"].toArray()) {
            const auto fixture = value.toObject();
            QTemporaryDir scratch;
            const auto path = UnpackSparseFixture(fixture, scratch);
            QVERIFY(!path.isEmpty());
            CMyComPtr2_Create<IArchiveOpenCallback, Volumes> volumes;
            CMyComPtr<IInStream> decoded;
            const bool bundle = fixture["type"].toString() == "SPARSEBUNDLE";
            if (bundle) {
                for (const auto &file : fixture["files"].toArray()) {
                    const auto relative = file.toObject()["path"].toString();
                    volumes->Files[relative] = FileBytes(QDir(path).filePath(relative));
                }
                CMyComPtr2_Create<IInStream, NArchive::NSparseImage::SparseBundleStream> mapped;
                QCOMPARE(mapped->Open(Input(volumes->Files["Info.plist"]), volumes), S_OK);
                decoded = mapped;
            } else {
                CMyComPtr<IInStream> input = Input(FileBytes(path));
                if (fixture["bits"].toInt()) {
                    CMyComPtr2_Create<IInStream, NArchive::NEncryptedDmg::DecryptStream> crypto;
                    QCOMPARE(crypto->Open(input, volumes), S_OK);
                    input = crypto;
                }
                CMyComPtr2_Create<IInStream, NArchive::NSparseImage::SparseImageStream> mapped;
                QCOMPARE(mapped->Open(input, volumes), S_OK);
                decoded = mapped;
            }
            UInt64 length = 0;
            QCOMPARE(decoded->Seek(0, STREAM_SEEK_END, &length), S_OK);
            QCOMPARE(length, UInt64(fixture["size"].toInteger()));
            for (const auto &item : fixture["ranges"].toArray()) {
                const auto range = item.toObject();
                QCOMPARE(decoded->Seek(range["offset"].toInteger(), STREAM_SEEK_SET, nullptr),
                         S_OK);
                QByteArray bytes(range["size"].toInt(), '\0');
                QCOMPARE(ReadStream_FALSE(decoded, bytes.data(), size_t(bytes.size())), S_OK);
                QCOMPARE(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex(),
                         range["sha256"].toString().toLatin1());
            }
        }
    }
    void nativeBackendAndConsole() {
        for (const auto &value : SparseFixtureManifest()["fixtures"].toArray()) {
            const auto fixture = value.toObject();
            const bool encrypted = fixture["bits"].toInt() != 0;
            QTemporaryDir scratch;
            const auto path = UnpackSparseFixture(fixture, scratch);
            QVERIFY(!path.isEmpty());
            OpenPassword password;
            password.WrongFirst = encrypted;
            std::shared_ptr<ArchiveNativeOpen> reader;
            QCOMPARE(
                ArchiveBackend::openNativeArchive(catalog_, path, {}, &password, reader, nullptr),
                S_OK);
            QCOMPARE(password.Asked, encrypted ? 2 : 0);
            QCOMPARE(password.Retries, encrypted ? 1 : 0);
            QVERIFY(reader->resolvedOpenType().contains(
                fixture["type"].toString() == "SPARSEBUNDLE" ? "SparseBundle" : "SparseImage"));
            auto *owner = new QtArchiveFileResource(path);
            CMyComPtr<IUnknown> retained = owner;
            owner->AttachNativeOpen(reader);
            owner->UseNativeMetadata();
            std::shared_ptr<ArchiveReadOnlyView> view;
            QString error;
            QCOMPARE(ArchiveBackend::createReadOnlyView(retained, 0, {}, view, error), S_OK);
            const auto entries = SparseFixtureManifest()["entries"].toObject();
            for (auto entry = entries.begin(); entry != entries.end(); ++entry) {
                const auto found =
                    std::find_if(view->entries().begin(), view->entries().end(),
                                 [&](const auto &file) { return file.Name == entry.key(); });
                QVERIFY2(found != view->entries().end(), qPrintable(entry.key()));
                QByteArray bytes;
                for (quint64 offset = 0; offset < found->Size; offset += 1u << 20) {
                    const auto data = view->read(found->Id, offset, 1u << 20);
                    QCOMPARE(data.Result, S_OK);
                    bytes += data.Data;
                }
                QCOMPARE(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex(),
                         entry.value().toString().toLatin1());
            }
            // The same format modules serve the independent official console.
            QProcess console;
            const auto consolePath = fixture["type"].toString() == "SPARSEBUNDLE"
                                         ? QDir(path).filePath("Info.plist")
                                         : path;
            console.start(engine_, {"t", "-pfixture-password", consolePath});
            QVERIFY(console.waitForFinished(30000));
            QCOMPARE(console.exitCode(), 0);
            if (encrypted) {
                password.Cancel = true;
                std::shared_ptr<ArchiveNativeOpen> cancelled;
                QCOMPARE(ArchiveBackend::openNativeArchive(catalog_, path, {}, &password, cancelled,
                                                           nullptr),
                         E_ABORT);
            }
        }
    }
    void backendListAndExtract() {
        ArchiveBackend backend(nullptr, engine_);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(module_));
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 15000);
        QSignalSpy finished(&backend, &ArchiveBackend::finished);
        QSignalSpy listed(&backend, &ArchiveBackend::entriesReady);
        int asked = 0;
        connect(&backend, &ArchiveBackend::passwordRequired, &backend,
                [&] { backend.providePassword(++asked == 1 ? "wrong" : "fixture-password"); });
        for (const auto &value : SparseFixtureManifest()["fixtures"].toArray()) {
            const auto fixture = value.toObject();
            qInfo() << "Extracting native fixture" << fixture["name"].toString();
            QTemporaryDir scratch, output;
            const auto path = UnpackSparseFixture(fixture, scratch);
            QVERIFY(!path.isEmpty());
            finished.clear();
            listed.clear();
            asked = 0;
            backend.list(path);
            QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 20000);
            QVERIFY2(finished[0][0].toBool(), qPrintable(finished[0][1].toString()));
            QCOMPARE(asked, fixture["bits"].toInt() ? 2 : 0);
            QCOMPARE(listed.size(), 1);
            finished.clear();
            backend.extract(path, output.path(), {}, "fixture-password");
            QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 20000);
            QVERIFY2(finished[0][0].toBool(), qPrintable(finished[0][1].toString()));
            const auto entries = SparseFixtureManifest()["entries"].toObject();
            for (auto entry = entries.begin(); entry != entries.end(); ++entry) {
                QDirIterator files(output.path(), QDir::Files, QDirIterator::Subdirectories);
                QByteArray contents;
                while (files.hasNext()) {
                    const auto candidate = files.next();
                    if (QFileInfo(candidate).fileName() == entry.key())
                        contents = FileBytes(candidate);
                }
                QCOMPARE(QCryptographicHash::hash(contents, QCryptographicHash::Sha256).toHex(),
                         entry.value().toString().toLatin1());
            }
        }
    }
    void damagedBandKeepsReadableFiles() {
        QTemporaryDir scratch, output;
        const auto path = DamagedSparseBundle(scratch);
        QVERIFY(!path.isEmpty());
        const auto original = FileBytes(QDir(path).filePath("bands/8"));
        OpenPassword password;
        std::shared_ptr<ArchiveNativeOpen> reader;
        QCOMPARE(ArchiveBackend::openNativeArchive(catalog_, path, {}, &password, reader, nullptr),
                 S_OK);
        auto *owner = new QtArchiveFileResource(path);
        CMyComPtr<IUnknown> retained = owner;
        owner->AttachNativeOpen(reader);
        owner->UseNativeMetadata();
        std::shared_ptr<ArchiveReadOnlyView> view;
        QString error;
        QCOMPARE(ArchiveBackend::createReadOnlyView(retained, 0, {}, view, error), S_OK);
        const ExfatFixture names;
        const auto find = [&](const QString &name) {
            return std::find_if(view->entries().begin(), view->entries().end(),
                                [&](const auto &file) { return file.Name == name; });
        };
        const auto damaged = find(names.fragmentedName), readable = find(names.nestedName);
        QVERIFY(damaged != view->entries().end());
        QVERIFY(readable != view->entries().end());
        QCOMPARE(view->read(damaged->Id, 0, 512).Data, QByteArray(512, 'A'));
        QVERIFY(view->read(damaged->Id, 512, 188).Result != S_OK);
        const auto good = view->read(readable->Id, 0, 4);
        QCOMPARE(good.Result, S_OK);
        QCOMPARE(good.Data, QByteArray("tail"));
        ArchiveBackend backend(nullptr, engine_);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(module_));
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 15000);
        QSignalSpy finished(&backend, &ArchiveBackend::finished);
        const auto partial = output.filePath("partial.bin");
        backend.extractSourceEntry(path, names.fragmentedName, partial, {}, "fixture-password");
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 15000);
        QVERIFY(!finished[0][0].toBool());
        QCOMPARE(backend.lastResult(), S_FALSE);
        QVERIFY(finished[0][1].toString().contains(names.fragmentedName));
        QCOMPARE(FileBytes(partial), QByteArray(512, 'A'));
        QCOMPARE(FileBytes(QDir(path).filePath("bands/8")), original);
    }
};
QTEST_GUILESS_MAIN(SparseImageTests)
#include "sparse_image_tests.moc"
