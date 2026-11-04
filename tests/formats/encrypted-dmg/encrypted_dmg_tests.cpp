#include "7zip/Common/StreamUtils.h"
#include "7zip/IPassword.h"
#include "Windows/PropVariant.h"
#include "archive/backend/archivebackend.h"
#include "archive/callbacks/ArchiveOpenCallback.h"
#include "archive/callbacks/archivepasswordretry.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/resources/archivenativeopen.h"
#include "archive/resources/archivereadonlyview.h"
#include "archive/resources/qtarchivefileresource.h"
#include "dmgfixtures.h"
#include "formats/encrypted-dmg/dmgdecryptstream.h"
#include "formats/exfat/support/exfat_fixture.h"
#include "support/codec_runtime.h"
#include <QDirIterator>
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

namespace {
class Input final : public IInStream, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_2(IInStream, ISequentialInStream)
    QByteArray bytes_;
    UInt64 position_ = 0;

  public:
    UInt64 ReadBytes = 0;
    bool Fail = false;
    explicit Input(QByteArray bytes) : bytes_(std::move(bytes)) {}
    Z7_COM7F_IMF(Read(void *data, UInt32 size, UInt32 *processed)) override {
        if (processed)
            *processed = 0;
        if (Fail)
            return E_FAIL;
        const UInt32 count =
            position_ >= UInt64(bytes_.size())
                ? 0
                : UInt32(qMin<UInt64>(qMin<UInt32>(size, 13), UInt64(bytes_.size()) - position_));
        if (count)
            memcpy(data, bytes_.constData() + position_, count);
        position_ += count;
        ReadBytes += count;
        if (processed)
            *processed = count;
        return S_OK;
    }
    Z7_COM7F_IMF(Seek(Int64 offset, UInt32 origin, UInt64 *position)) override {
        const Int64 base = origin == 0 ? 0 : origin == 1 ? Int64(position_) : bytes_.size();
        if (origin > 2 || offset < -base)
            return E_INVALIDARG;
        position_ = UInt64(base + offset);
        if (position)
            *position = position_;
        return S_OK;
    }
};
class Password final : public IArchiveOpenCallback,
                       public ICryptoGetTextPassword,
                       public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_2(IArchiveOpenCallback, ICryptoGetTextPassword)
  public:
    QString Value = "fixture-password";
    bool Cancel = false;
    int Asked = 0;
    Z7_COM7F_IMF(SetTotal(const UInt64 *, const UInt64 *)) override { return S_OK; }
    Z7_COM7F_IMF(SetCompleted(const UInt64 *, const UInt64 *)) override {
        return Cancel ? E_ABORT : S_OK;
    }
    Z7_COM7F_IMF(CryptoGetTextPassword(BSTR *password)) override {
        ++Asked;
        return Cancel ? E_ABORT : StringToBstr(Value.toStdWString().c_str(), password);
    }
};
class OpenPassword final : public IOpenCallbackUI {
  public:
    HRESULT Open_CheckBreak() override { return S_OK; }
    HRESULT Open_SetTotal(const UInt64 *, const UInt64 *) override { return S_OK; }
    HRESULT Open_SetCompleted(const UInt64 *, const UInt64 *) override { return S_OK; }
    HRESULT Open_Finished() override { return S_OK; }
    HRESULT Open_CryptoGetTextPassword(BSTR *value) override {
        return StringToBstr(L"fixture-password", value);
    }
};
QByteArray ReadFile(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
bool WriteFile(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QString Fixture(const char *name) { return QString::fromUtf8(SEVENZIP_DMG_FIXTURES) + '/' + name; }
QByteArray Pattern() {
    QByteArray data(32768 + 37, '\0');
    quint32 value = 0x12345678;
    for (char &c : data) {
        value = value * 1664525 + 1013904223;
        c = char(value >> 24);
    }
    return data;
}
} // namespace

class EncryptedDmgTests final : public QObject {
    Q_OBJECT
    QTemporaryDir runtime_;
    QString engine_, module_;
    std::shared_ptr<const ArchiveCodecCatalog> catalog_;
    void CheckReads(const QByteArray &image, const QByteArray &plain) {
        CMyComPtr2<IInStream, Input> input;
        input.SetFromCls(new Input(image));
        CMyComPtr2_Create<IArchiveOpenCallback, Password> password;
        CMyComPtr2_Create<IInStream, NArchive::NEncryptedDmg::DecryptStream> stream;
        QCOMPARE(stream->Open(input, password), S_OK);
        // Opening must not eagerly decrypt the body.
        QVERIFY(input->ReadBytes < UInt64(image.size()));
        QCOMPARE(stream->Size(), UInt64(plain.size()));
        const QList<qsizetype> offsets{0,
                                       1,
                                       15,
                                       16,
                                       511,
                                       512,
                                       513,
                                       4095,
                                       32760,
                                       524271,
                                       4194335,
                                       0,
                                       plain.size() - 1,
                                       plain.size(),
                                       plain.size() + 9};
        for (qsizetype offset : offsets) {
            QCOMPARE(stream.Interface()->Seek(offset, STREAM_SEEK_SET, nullptr), S_OK);
            QByteArray data(777, '\0');
            size_t count = size_t(data.size());
            QCOMPARE(ReadStream(stream, data.data(), &count), S_OK);
            data.resize(qsizetype(count));
            QCOMPARE(data, plain.mid(qMin(offset, plain.size()), 777));
        }
        QCOMPARE(stream.Interface()->Seek(-17, STREAM_SEEK_END, nullptr), S_OK);
        char tail[100];
        size_t count = sizeof(tail);
        QCOMPARE(ReadStream(stream, tail, &count), S_OK);
        QCOMPARE(QByteArray(tail, qsizetype(count)), plain.right(17));
        QCOMPARE(stream.Interface()->Seek(-1, STREAM_SEEK_SET, nullptr),
                 HRESULT_WIN32_ERROR_NEGATIVE_SEEK);
        QCOMPARE(stream.Interface()->Seek(0, STREAM_SEEK_SET, nullptr), S_OK);
        // Reopen discards buffered ciphertext; an uncached source error must
        // still reach the caller without reporting decoded bytes.
        QCOMPARE(stream->Open(input, password), S_OK);
        input->Fail = true;
        UInt32 processed = 55;
        QCOMPARE(stream.Interface()->Read(tail, sizeof(tail), &processed), E_FAIL);
        QCOMPARE(processed, UInt32(0));
    }
  private slots:
    void initTestCase() {
        QVERIFY(runtime_.isValid());
        const QDir prepared(QString::fromUtf8(SEVENZIP_DMG_RUNTIME));
#ifdef Q_OS_WIN
        const QString engineName = "7z.exe", moduleName = "7z.dll", suffix = ".dll";
#else
        const QString engineName = "7z", moduleName = "7z.so", suffix = ".so";
#endif
        engine_ = runtime_.filePath(engineName);
        module_ = runtime_.filePath(moduleName);
        QVERIFY(CopyCodecRuntime(prepared, QDir(runtime_.path())));
        QVERIFY(QDir(runtime_.path()).mkdir("Formats"));
        QVERIFY(QFile::copy(QString::fromUtf8(SEVENZIP_DMG_MODULE),
                            runtime_.filePath("Formats/EncryptedDmg" + suffix)));
        QVERIFY(QFile::copy(QString::fromUtf8(SEVENZIP_DMG_HFS_MODULE),
                            runtime_.filePath("Formats/Hfs" + suffix)));
        QVERIFY(QFile::copy(QString::fromUtf8(SEVENZIP_DMG_EXFAT_MODULE),
                            runtime_.filePath("Formats/Exfat" + suffix)));
        ArchiveBackend backend(nullptr, engine_);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(module_));
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 15000);
        QVERIFY2(ready[0][0].toBool(), qPrintable(ready[0][1].toString()));
        catalog_ = backend.codecCatalog();
    }
    void randomReads() {
        const QByteArray plain = Pattern();
        for (unsigned bits : {128u, 256u})
            for (bool aes : {false, true})
                CheckReads(DmgFixture(plain, bits, aes), plain);
        CheckReads(DmgFixture(plain, 128, false, true), plain);
    }
    void largeRandomReads() {
        const auto plain = Pattern().repeated(160);
        CheckReads(DmgFixture(plain, 256, true), plain);
    }
    void nativeReferences() {
        for (const char *name : {"native-raw128.dmg", "native-raw256.dmg"}) {
            const auto bytes = ReadFile(Fixture(name));
            QVERIFY(!bytes.isEmpty());
            CheckReads(bytes, QByteArray(65536, '\0'));
        }
    }
    void mountedRangesCrossEncryptionBlocks() {
        OpenPassword password;
        std::shared_ptr<ArchiveNativeOpen> reader;
        const auto path = Fixture("native-raw256.dmg");
        QCOMPARE(ArchiveBackend::openNativeArchive(catalog_, path, "EncryptedDmg", &password,
                                                   reader, nullptr),
                 S_OK);
        auto *owner = new QtArchiveFileResource(path);
        CMyComPtr<IUnknown> retained = owner;
        owner->AttachNativeOpen(reader);
        owner->UseNativeMetadata();
        std::shared_ptr<ArchiveReadOnlyView> view;
        QString error;
        QCOMPARE(ArchiveBackend::createReadOnlyView(retained, 0, {}, view, error), S_OK);
        const auto file = std::find_if(view->entries().begin(), view->entries().end(),
                                       [](const auto &entry) { return !entry.Directory; });
        QVERIFY(file != view->entries().end());
        for (quint64 offset : {quint64(4093), quint64(0), quint64(60000)}) {
            const auto read = view->read(file->Id, offset, 8197);
            QCOMPARE(read.Result, S_OK);
            QCOMPARE(read.Data, QByteArray(qMin<quint64>(8197, 65536 - offset), '\0'));
        }
        view->cancel();
        QCOMPARE(view->read(file->Id, 0, 8197).Result, E_ABORT);
    }
    void compressedApfsMountWithoutExtraction() {
        QTemporaryDir inputs;
        const auto path = inputs.filePath("encrypted-apfs.dmg");
        const auto image = DmgFixture(ReadFile(Fixture("native-apfs-clear.dmg")), 256, true);
        QVERIFY(WriteFile(path, image));
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
        int extractions = 0;
        QCOMPARE(ArchiveBackend::createReadOnlyView(retained, 0, {}, view, error,
                                                    [&](const QString &, quint64) {
                                                        ++extractions;
                                                        return false;
                                                    }),
                 S_OK);
        bool cache = true;
        QCOMPARE(view->needsCache(cache), S_OK);
        QVERIFY(!cache);
        const auto file =
            std::find_if(view->entries().begin(), view->entries().end(),
                         [](const auto &entry) { return entry.Name == "inline.txt"; });
        QVERIFY(file != view->entries().end());
        QCOMPARE(file->Size, quint64(32768));
        for (quint64 offset : {quint64(31001), quint64(0), quint64(777)}) {
            const auto read = view->read(file->Id, offset, 513);
            QCOMPARE(read.Result, S_OK);
            QByteArray expected(513, '\0');
            for (qsizetype i = 0; i < expected.size(); ++i)
                expected[i] = char((offset + quint64(i)) & 255);
            QCOMPARE(read.Data, expected);
        }
        QCOMPARE(extractions, 0);
        QCOMPARE(ReadFile(path), image);
    }
    void nativeReadOnlyMount_data() {
        QTest::addColumn<QString>("format");
        QTest::addColumn<QString>("filesystem");
        QTest::newRow("encrypted read-only") << QString("UDRO") << QString("APFS");
        QTest::newRow("encrypted compressed") << QString("UDZO") << QString("APFS");
        QTest::newRow("encrypted exfat read-only") << QString("UDRO") << QString("ExFAT");
        QTest::newRow("encrypted exfat compressed") << QString("UDZO") << QString("ExFAT");
    }
    void nativeReadOnlyMount() {
#ifndef Q_OS_MACOS
        QSKIP("Native macOS image producer integration.");
#else
        QFETCH(QString, format);
        QFETCH(QString, filesystem);
        QTemporaryDir inputs;
        QVERIFY(QDir(inputs.path()).mkdir("files"));
        const auto payload = Pattern().repeated(120);
        QVERIFY(WriteFile(inputs.filePath("files/large.bin"), payload));
        const auto path = inputs.filePath("readonly.dmg");
        QProcess producer;
        QStringList arguments{"create",     "-srcfolder",  inputs.filePath("files"),
                              "-format",    format,        "-fs",
                              filesystem,   "-encryption", "AES-256",
                              "-stdinpass", path};
        if (filesystem == "ExFAT")
            arguments << "-size" << "64m";
        producer.start("/usr/bin/hdiutil", arguments);
        QVERIFY(producer.waitForStarted());
        producer.write(QByteArray("fixture-password\0", 17));
        producer.closeWriteChannel();
        QVERIFY(producer.waitForFinished(30000));
        QCOMPARE(producer.exitCode(), 0);
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
        int extractions = 0;
        QCOMPARE(ArchiveBackend::createReadOnlyView(retained, 1, {}, view, error,
                                                    [&](const QString &, quint64) {
                                                        ++extractions;
                                                        return false;
                                                    }),
                 S_OK);
        bool cache = true;
        QCOMPARE(view->needsCache(cache), S_OK);
        QVERIFY(!cache);
        const auto file = std::find_if(view->entries().begin(), view->entries().end(),
                                       [](const auto &entry) { return entry.Name == "large.bin"; });
        QVERIFY(file != view->entries().end());
        for (quint64 offset : {quint64(payload.size() - 513), quint64(0), quint64(777)}) {
            const auto read = view->read(file->Id, offset, 513);
            QVERIFY2(read.Result == S_OK, qPrintable(read.Error));
            QCOMPARE(read.Data, payload.mid(qsizetype(offset), 513));
        }
        QCOMPARE(extractions, 0);
#endif
    }
    void encryptedExfatMountWithoutExtraction() {
        ExfatFixture fixture;
        QTemporaryDir inputs;
        const auto path = inputs.filePath("encrypted-exfat.dmg");
        const auto image = DmgFixture(fixture.image, 256, true);
        QVERIFY(WriteFile(path, image));
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
        int extractions = 0;
        QCOMPARE(ArchiveBackend::createReadOnlyView(retained, 0, {}, view, error,
                                                    [&](const QString &, quint64) {
                                                        ++extractions;
                                                        return false;
                                                    }),
                 S_OK);
        bool cache = true;
        QCOMPARE(view->needsCache(cache), S_OK);
        QVERIFY(!cache);
        const auto file =
            std::find_if(view->entries().begin(), view->entries().end(),
                         [&](const auto &entry) { return entry.Name == fixture.fragmentedName; });
        QVERIFY(file != view->entries().end());
        const auto range = view->read(file->Id, 500, 200);
        QCOMPARE(range.Result, S_OK);
        QCOMPARE(range.Data, QByteArray(12, 'A') + QByteArray(188, 'B'));
        QCOMPARE(extractions, 0);
        QCOMPARE(ReadFile(path), image);
    }
    void passwordAndMalformed() {
        const auto good = DmgFixture(Pattern());
        const auto attempt = [](const QByteArray &bytes, const QString &value, bool cancel,
                                int *asked = nullptr) {
            CMyComPtr2_Create<IInStream, NArchive::NEncryptedDmg::DecryptStream> stream;
            CMyComPtr2<IInStream, Input> input;
            input.SetFromCls(new Input(bytes));
            CMyComPtr2_Create<IArchiveOpenCallback, Password> password;
            password->Value = value;
            password->Cancel = cancel;
            const HRESULT result = stream->Open(input, password);
            if (asked)
                *asked = password->Asked;
            return result;
        };
        for (bool v1 : {false, true}) {
            const auto bytes = DmgFixture(Pattern(), 128, false, v1);
            QCOMPARE(attempt(bytes, "wrong", false), kArchiveWrongPassword);
            QCOMPARE(attempt(bytes, "fixture-password", true), E_ABORT);
            QCOMPARE(attempt(bytes, "fixture-password", false), S_OK);
        }
        for (qsizetype size : {qsizetype(0), qsizetype(7), qsizetype(8), qsizetype(75),
                               qsizetype(100), good.size() - 1})
            QVERIFY(attempt(good.left(size), "fixture-password", false) != S_OK);
        for (size_t at :
             {size_t(52), size_t(64), size_t(80), size_t(88), size_t(108), size_t(196)}) {
            auto damaged = good;
            qToBigEndian(quint32(0xffffffff), damaged.data() + at);
            int asked = -1;
            QVERIFY(attempt(damaged, "fixture-password", false, &asked) != S_OK);
            QCOMPARE(asked, 0);
        }
        auto unsupported = good;
        qToBigEndian(quint32(0xdeadbeef), unsupported.data() + 184);
        QCOMPARE(attempt(unsupported, "fixture-password", false), E_NOTIMPL);
        auto badBlob = good;
        // Flip the preceding CBC block so the final padding byte changes
        // while its neighbours retain their old padding value.
        badBlob[239] ^= 1;
        QCOMPARE(attempt(badBlob, "fixture-password", false), kArchiveWrongPassword);
    }
    void backendRetryAndExtract_data() {
        QTest::addColumn<QByteArray>("image");
        QTest::newRow("native-modern256") << ReadFile(Fixture("native-hfs256.dmg"));
        const auto plain = ReadFile(Fixture("native-hfs-clear.dmg"));
        QTest::newRow("v1") << DmgFixture(plain, 128, false, true);
        QTest::newRow("v2-des128") << DmgFixture(plain, 128, false);
        QTest::newRow("v2-des256") << DmgFixture(plain, 256, false);
        QTest::newRow("v2-aes128") << DmgFixture(plain, 128, true);
    }
    void backendRetryAndExtract() {
        QFETCH(QByteArray, image);
        ArchiveBackend backend(nullptr, engine_);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(module_));
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 15000);
        QSignalSpy finished(&backend, &ArchiveBackend::finished);
        QSignalSpy entries(&backend, &ArchiveBackend::entriesReady);
        QSignalSpy info(&backend, &ArchiveBackend::infoReady);
        int asked = 0;
        connect(&backend, &ArchiveBackend::passwordRequired, &backend,
                [&] { backend.providePassword(++asked == 1 ? "wrong" : "fixture-password"); });
        QTemporaryDir inputs;
        const auto source = inputs.filePath(QString::fromUtf8("образ 空.dmg"));
        QVERIFY(WriteFile(source, image));
        const auto original = ReadFile(source);
        backend.list(source);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 20000);
        QVERIFY2(finished[0][0].toBool(), qPrintable(finished[0][1].toString()));
        QCOMPARE(asked, 2);
        QCOMPARE(info.size(), 1);
        QVERIFY(qvariant_cast<ArchiveInfo>(info[0][0]).encrypted);
        QCOMPARE(entries.size(), 1);
        const auto listed = qvariant_cast<QList<ArchiveEntry>>(entries[0][0]);
        QVERIFY(std::any_of(listed.begin(), listed.end(), [](const auto &entry) {
            return entry.path.endsWith(QString::fromUtf8("hello 空.txt"));
        }));
        QTemporaryDir output;
        finished.clear();
        backend.extract(source, output.path(), {}, "fixture-password");
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 20000);
        QVERIFY2(finished[0][0].toBool(), qPrintable(finished[0][1].toString()));
        QDirIterator files(output.path(), QDir::Files, QDirIterator::Subdirectories);
        QByteArray contents;
        while (files.hasNext()) {
            const auto path = files.next();
            if (path.endsWith(QString::fromUtf8("hello 空.txt")))
                contents = ReadFile(path);
        }
        QCOMPARE(contents, QByteArray("Encrypted DMG fixture\n"));
        QCOMPARE(ReadFile(source), original);
    }
    void backendCancel() {
        ArchiveBackend backend(nullptr, engine_);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(module_));
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 15000);
        QSignalSpy finished(&backend, &ArchiveBackend::finished);
        connect(&backend, &ArchiveBackend::passwordRequired, &backend, &ArchiveBackend::cancel);
        backend.list(Fixture("native-hfs256.dmg"));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 15000);
        QVERIFY(!finished[0][0].toBool());
        QCOMPARE(backend.lastResult(), E_ABORT);
    }
    void console() {
        QProcess client;
        client.start(engine_, {"t", "-pfixture-password", Fixture("native-hfs256.dmg")});
        QVERIFY(client.waitForFinished(20000));
        QCOMPARE(client.exitCode(), 0);
        client.start(engine_, {"t", "-pwrong", Fixture("native-hfs256.dmg")});
        QVERIFY(client.waitForFinished(20000));
        QVERIFY(client.exitCode() != 0);
        const auto diagnostic = client.readAllStandardOutput() + client.readAllStandardError();
        QVERIFY(diagnostic.contains("Wrong password?"));
        QVERIFY(!diagnostic.contains("errno=86"));
    }
    void ordinaryDmg() {
        ArchiveBackend backend(nullptr, engine_);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(module_));
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 15000);
        QSignalSpy password(&backend, &ArchiveBackend::passwordRequired);
        QSignalSpy finished(&backend, &ArchiveBackend::finished);
        QSignalSpy entries(&backend, &ArchiveBackend::entriesReady);
        backend.list(Fixture("native-hfs-clear.dmg"));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 15000);
        QVERIFY2(finished[0][0].toBool(), qPrintable(finished[0][1].toString()));
        QCOMPARE(password.size(), 0);
        QCOMPARE(entries.size(), 1);
        const auto listed = qvariant_cast<QList<ArchiveEntry>>(entries[0][0]);
        QVERIFY(std::any_of(listed.begin(), listed.end(), [](const auto &entry) {
            return entry.path.endsWith(QString::fromUtf8("hello 空.txt"));
        }));
    }
};
QTEST_GUILESS_MAIN(EncryptedDmgTests)
#include "encrypted_dmg_tests.moc"
