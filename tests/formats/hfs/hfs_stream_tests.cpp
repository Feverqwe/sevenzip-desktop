#include "7zip/Archive/IArchive.h"
#include "Common/MyCom.h"
#include "Windows/PropVariant.h"
#include "archive/backend/archivebackend.h"
#include "archive/codecs/archivecodeccatalog.h"
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibrary>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>
#include <memory>

namespace {
class Input final : public IInStream, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_2(ISequentialInStream, IInStream)
    QByteArray bytes_;
    UInt64 position_ = 0;

  public:
    HRESULT failure = S_OK;
    HRESULT seekFailure = S_OK;
    bool eof = false;
    UInt64 readBytes = 0;
    explicit Input(QByteArray bytes) : bytes_(std::move(bytes)) {}
    Z7_COM7F_IMF(Read(void *data, UInt32 size, UInt32 *processed)) override {
        if (processed)
            *processed = 0;
        if (failure != S_OK)
            return failure;
        if (eof)
            return S_OK;
        size = UInt32(qMin<UInt64>(
            size, position_ < UInt64(bytes_.size()) ? UInt64(bytes_.size()) - position_ : 0));
        if (size)
            memcpy(data, bytes_.constData() + position_, size);
        position_ += size;
        readBytes += size;
        if (processed)
            *processed = size;
        return S_OK;
    }
    Z7_COM7F_IMF(Seek(Int64 offset, UInt32 origin, UInt64 *position)) override {
        if (failure != S_OK)
            return failure;
        if (seekFailure != S_OK) {
            const HRESULT result = seekFailure;
            seekFailure = S_OK;
            return result;
        }
        if (origin > STREAM_SEEK_END)
            return E_INVALIDARG;
        const Int64 base = origin == STREAM_SEEK_SET   ? 0
                           : origin == STREAM_SEEK_CUR ? Int64(position_)
                                                       : bytes_.size();
        if (offset < -base || (offset > 0 && base > INT64_MAX - offset))
            return E_INVALIDARG;
        position_ = UInt64(base + offset);
        if (position)
            *position = position_;
        return S_OK;
    }
};
class Extract final : public IArchiveExtractCallback, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_1(IArchiveExtractCallback)
  public:
    bool cancel = false;
    Int32 result = -1;
    Z7_COM7F_IMF(SetTotal(UInt64)) override { return S_OK; }
    Z7_COM7F_IMF(SetCompleted(const UInt64 *)) override { return cancel ? E_ABORT : S_OK; }
    Z7_COM7F_IMF(GetStream(UInt32, ISequentialOutStream **stream, Int32)) override {
        *stream = nullptr;
        return S_OK;
    }
    Z7_COM7F_IMF(PrepareOperation(Int32)) override { return S_OK; }
    Z7_COM7F_IMF(SetOperationResult(Int32 value)) override {
        result = value;
        return S_OK;
    }
};
class CancelOpen final : public IArchiveOpenCallback, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_1(IArchiveOpenCallback)
  public:
    Z7_COM7F_IMF(SetTotal(const UInt64 *, const UInt64 *)) override { return S_OK; }
    Z7_COM7F_IMF(SetCompleted(const UInt64 *, const UInt64 *)) override { return E_ABORT; }
};
} // namespace

class HfsStreamTests final : public QObject {
    Q_OBJECT
    QLibrary module_{QString::fromUtf8(SEVENZIP_HFS_CORE)};
    CMyComPtr<IInArchive> archive_;
    QJsonObject record_;
    QByteArray bytes_;
    QHash<QString, QJsonObject> fixtures_;
    std::unique_ptr<ArchiveBackend> backendPrototype_;

    void createArchive() {
        archive_.Release();
        QVERIFY2(module_.load(), qPrintable(module_.errorString()));
        const auto count =
            reinterpret_cast<Func_GetNumberOfFormats>(module_.resolve("GetNumberOfFormats"));
        const auto property =
            reinterpret_cast<Func_GetHandlerProperty2>(module_.resolve("GetHandlerProperty2"));
        const auto create = reinterpret_cast<Func_CreateObject>(module_.resolve("CreateObject"));
        QVERIFY(count && property && create);
        UInt32 formats = 0;
        QCOMPARE(count(&formats), S_OK);
        for (UInt32 i = 0; i < formats; ++i) {
            NWindows::NCOM::CPropVariant name;
            QCOMPARE(property(i, NArchive::NHandlerPropID::kName, &name), S_OK);
            if (name.vt != VT_BSTR || QString::fromWCharArray(name.bstrVal) != "HFS")
                continue;
            NWindows::NCOM::CPropVariant clsid;
            QCOMPARE(property(i, NArchive::NHandlerPropID::kClassID, &clsid), S_OK);
            GUID guid;
            memcpy(&guid, clsid.bstrVal, sizeof(guid));
            QCOMPARE(create(&guid, &IID_IInArchive, reinterpret_cast<void **>(&archive_)), S_OK);
            return;
        }
        QFAIL("Prepared engine has no HFS handler");
    }
    UInt32 index(const QString &name) {
        UInt32 count = 0;
        if (archive_->GetNumberOfItems(&count) != S_OK)
            return UInt32(-1);
        for (UInt32 i = 0; i < count; ++i) {
            NWindows::NCOM::CPropVariant path;
            if (archive_->GetProperty(i, kpidPath, &path) == S_OK && path.vt == VT_BSTR &&
                QString::fromWCharArray(path.bstrVal).replace('\\', '/') == name)
                return i;
        }
        return UInt32(-1);
    }
    CMyComPtr<IInStream> stream(const QString &name) {
        CMyComPtr<IInArchiveGetStream> provider;
        if (archive_->QueryInterface(IID_IInArchiveGetStream,
                                     reinterpret_cast<void **>(&provider)) != S_OK)
            return {};
        CMyComPtr<ISequentialInStream> sequential;
        if (provider->GetStream(index(name), &sequential) != S_OK || !sequential)
            return {};
        CMyComPtr<IInStream> result;
        sequential->QueryInterface(IID_IInStream, reinterpret_cast<void **>(&result));
        return result;
    }
    void check(IInStream *stream, const QString &name, int offset, int size) {
        QVERIFY2(stream, qPrintable(name));
        QVERIFY2(record_["files"].toObject().contains(name), qPrintable(name));
        const auto expected =
            QByteArray::fromBase64(record_["files"].toObject()[name].toString().toLatin1())
                .mid(offset, size);
        QCOMPARE(stream->Seek(offset, STREAM_SEEK_SET, nullptr), S_OK);
        QByteArray actual;
        while (actual.size() < expected.size()) {
            char data[37];
            UInt32 count = 0;
            QCOMPARE(
                stream->Read(data,
                             UInt32(qMin(qsizetype(sizeof(data)), expected.size() - actual.size())),
                             &count),
                S_OK);
            QVERIFY(count > 0);
            actual.append(data, count);
        }
        QCOMPARE(actual, expected);
    }
    void loadFixture(const QString &codec) {
        if (!fixtures_.contains(codec)) {
            QProcess generator;
            QStringList arguments{QString::fromUtf8(SEVENZIP_HFS_FIXTURE), codec};
            generator.start(QString::fromUtf8(SEVENZIP_HFS_PYTHON), arguments);
            QVERIFY(generator.waitForFinished(10000));
            QCOMPARE(generator.exitCode(), 0);
            const QJsonObject record =
                QJsonDocument::fromJson(generator.readAllStandardOutput()).object();
            QVERIFY(!record.isEmpty());
            fixtures_.insert(codec, record);
        }
        record_ = fixtures_.value(codec);
        QVERIFY(!record_.isEmpty());
        QByteArray packed(4, '\0');
        qToBigEndian(quint32(record_["size"].toInt()), packed.data());
        packed += QByteArray::fromBase64(record_["image"].toString().toLatin1());
        bytes_ = qUncompress(packed);
        QVERIFY(!bytes_.isEmpty());
    }
  private slots:
    void compressedDirectReads_data() {
        QTest::addColumn<QString>("kind");
        for (const char *kind :
             {"modern-zlib", "modern-lzvn", "modern-zlib-resource", "modern-lzvn-resource"})
            QTest::newRow(kind) << QString(kind);
    }
    void compressedDirectReads() {
        QFETCH(QString, kind);
        createArchive();
        loadFixture(kind);
        CMyComPtr2<IInStream, Input> input;
        input.SetFromCls(new Input(bytes_));
        QCOMPARE(archive_->Open(input, nullptr, nullptr), S_OK);
        const QString name = "HfsFixture/alpha";
        const auto before = input->readBytes;
        auto direct = stream(name);
        QVERIFY(direct);
        const auto expected =
            QByteArray::fromBase64(record_["files"].toObject()[name].toString().toLatin1());
        if (kind.endsWith("resource")) {
            const auto resource = QByteArray::fromBase64(
                record_["files"].toObject()[name + ":rsrc"].toString().toLatin1());
            QVERIFY(input->readBytes - before < UInt64(resource.size() / 2));
            const auto initialized = input->readBytes;
            check(direct, name, int(expected.size()) - 5, 5);
            QVERIFY(input->readBytes - initialized < UInt64(resource.size() / 2));
        }
        for (int offset : {0, 17, 65530, 65536, 131060, 9})
            check(direct, name, qMin(offset, int(expected.size()) - 1), 150);
        UInt64 end = 0;
        QCOMPARE(direct->Seek(0, STREAM_SEEK_END, &end), S_OK);
        QCOMPARE(end, UInt64(expected.size()));
        char byte;
        UInt32 actual = 99;
        QCOMPARE(direct->Read(&byte, 1, &actual), S_OK);
        QCOMPARE(actual, UInt32(0));
        if (kind.endsWith("resource")) {
            input->failure = E_ABORT;
            QCOMPARE(direct->Seek(65536, STREAM_SEEK_SET, nullptr), S_OK);
            QCOMPARE(direct->Read(&byte, 1, &actual), E_ABORT);
            QCOMPARE(actual, UInt32(0));
        }
    }
    void initTestCase() {
        createArchive();
        const QDir runtime(QString::fromUtf8(SEVENZIP_HFS_RUNTIME));
#ifdef Q_OS_WIN
        const QString core = runtime.filePath("7z.dll");
        const QString engine = runtime.filePath("7z.exe");
#else
        const QString core = runtime.filePath("7z.so");
        const QString engine = runtime.filePath("7z");
#endif
        backendPrototype_ = std::make_unique<ArchiveBackend>(nullptr, engine);
        QSignalSpy ready(backendPrototype_.get(), &ArchiveBackend::codecCatalogReady);
        QVERIFY(backendPrototype_->discoverCodecCatalog(core));
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 15000);
        QVERIFY2(ready[0][0].toBool(), qPrintable(ready[0][1].toString()));
    }
    void rejectsClassicHfs() {
        loadFixture("classic");
        CMyComPtr<IInStream> input = new Input(bytes_);
        QVERIFY(archive_->Open(input, nullptr, nullptr) != S_OK);
        UInt32 count = 99;
        QCOMPARE(archive_->GetNumberOfItems(&count), S_OK);
        QCOMPARE(count, UInt32(0));
    }
    void readLimitationsFollowOpening() {
        for (const char *kind : {"wrapper-no-slock", "base", "wrapper-no-spared", "unclean",
                                 "free-overflow", "base"}) {
            loadFixture(kind);
            CMyComPtr2<IInStream, Input> input;
            input.SetFromCls(new Input(bytes_));
            QCOMPARE(archive_->Open(input, nullptr, nullptr), S_OK);
            NWindows::NCOM::CPropVariant readOnly;
            QCOMPARE(archive_->GetArchiveProperty(kpidReadOnly, &readOnly), S_OK);
            QCOMPARE(readOnly.vt, VARTYPE(VT_BOOL));
            QVERIFY(readOnly.boolVal != VARIANT_FALSE);
            UInt32 count = 0;
            QCOMPARE(archive_->GetNumberOfArchiveProperties(&count), S_OK);
            bool found = false;
            for (UInt32 i = 0; i < count; ++i) {
                BSTR name = nullptr;
                PROPID id;
                VARTYPE type;
                QCOMPARE(archive_->GetArchivePropertyInfo(i, &name, &id, &type), S_OK);
                const QString label = name ? QString::fromWCharArray(name) : QString();
                SysFreeString(name);
                if (label != "Read-only limitations")
                    continue;
                found = true;
                NWindows::NCOM::CPropVariant value;
                QCOMPARE(archive_->GetArchiveProperty(id, &value), S_OK);
                const QString layout = QString::fromLatin1(kind);
                if (layout == "base")
                    QCOMPARE(value.vt, VARTYPE(VT_EMPTY));
                else {
                    QCOMPARE(value.vt, VARTYPE(VT_BSTR));
                    const QString reason = QString::fromWCharArray(value.bstrVal);
                    QCOMPARE(reason.contains("not properly unmounted"), layout.contains("unclean"));
                    QCOMPARE(reason.contains("wrapper lacks protection"),
                             layout.startsWith("wrapper-"));
                    QCOMPARE(reason.contains("Free space is unknown"),
                             layout.contains("free-overflow"));
                }
            }
            QVERIFY(found);
            NWindows::NCOM::CPropVariant freeSpace;
            QCOMPARE(archive_->GetArchiveProperty(kpidFreeSpace, &freeSpace), S_OK);
            QCOMPARE(
                freeSpace.vt,
                VARTYPE(QString::fromLatin1(kind).contains("free-overflow") ? VT_EMPTY : VT_UI8));
            QCOMPARE(archive_->Close(), S_OK);
        }
    }
    void independentAndRetainedForks_data() {
        QTest::addColumn<QString>("kind");
        QTest::newRow("overflow") << QString("overflow-leaves");
        QTest::newRow("hardlinks") << QString("modern");
        QTest::newRow("inline-reserved") << QString("xattr-reserved");
        QTest::newRow("inline-start") << QString("xattr-start");
        QTest::newRow("nested-wrapper") << QString("wrapper-nested");
        QTest::newRow("unprotected-wrapper-lock") << QString("wrapper-no-slock");
        QTest::newRow("unprotected-wrapper-spared") << QString("wrapper-no-spared");
        QTest::newRow("unclean") << QString("unclean");
        QTest::newRow("unknown-free-space") << QString("free-overflow");
        QTest::newRow("backup-version") << QString("wrapper-backup-version");
        QTest::newRow("hplus5") << QString("wrapper-version-hplus5");
        QTest::newRow("hfsx4") << QString("wrapper-version-hfsx4");
    }
    void independentAndRetainedForks() {
        QFETCH(QString, kind);
        loadFixture(kind);
        CMyComPtr2<IInStream, Input> input;
        input.SetFromCls(new Input(bytes_));
        QCOMPARE(archive_->Open(input, nullptr, nullptr), S_OK);
        const QString second = QString::fromUtf8("HfsFixture/данные 空");
        const QString attr = "HfsFixture/alpha:com.sevenzip.fixture";
        auto alpha = stream("HfsFixture/alpha");
        auto beta = stream(second);
        auto resource = stream("HfsFixture/alpha:rsrc");
        auto attribute = stream(attr);
        QVERIFY(alpha && beta && resource && attribute);
        check(alpha, "HfsFixture/alpha", 0, 37);
        check(beta, second, 0, 74);
        check(alpha, "HfsFixture/alpha", 37, 37);
        check(resource, "HfsFixture/alpha:rsrc", 0, 37);
        check(alpha, "HfsFixture/alpha", 74, 37);
        QCOMPARE(archive_->Close(), S_OK);
        const QJsonObject retained = record_;
        loadFixture("base");
        CMyComPtr2<IInStream, Input> replacement;
        replacement.SetFromCls(new Input(bytes_));
        QCOMPARE(archive_->Open(replacement, nullptr, nullptr), S_OK);
        archive_.Release();
        input.SetFromCls(nullptr);
        record_ = retained;
        check(alpha, "HfsFixture/alpha", 32760, 32780);
        check(resource, "HfsFixture/alpha:rsrc", 0, 2422);
        check(attribute, attr, 0, 24);
        createArchive();
    }
    void modernMetadataAndNames_data() {
        QTest::addColumn<QString>("kind");
        for (const char *kind : {"modern", "modern-nul", "modern-case", "modern-xattr",
                                 "modern-metadata", "modern-binary", "modern-hidden-case"})
            QTest::newRow(kind) << QString::fromLatin1(kind);
    }

    void modernMetadataAndNames() {
        QFETCH(QString, kind);
        loadFixture(kind);
        CMyComPtr<IInStream> input = new Input(bytes_);
        QCOMPARE(archive_->Open(input, nullptr, nullptr), S_OK);
        for (const QString &name : {QString("HfsFixture/alpha"), QString("HfsFixture/hard-alpha"),
                                    QString("HfsFixture/links/second-link")}) {
            for (const auto &property :
                 {qMakePair(kpidINode, UInt32(20)),
                  qMakePair(kpidLinks, UInt32(kind == "modern-metadata" ? 70000 : 3)),
                  qMakePair(kpidUserId, UInt32(kind == "modern-metadata" ? 0x12345678 : 501)),
                  qMakePair(kpidGroupId, UInt32(kind == "modern-metadata" ? 0x87654321 : 20))}) {
                NWindows::NCOM::CPropVariant value;
                QCOMPARE(archive_->GetProperty(index(name), property.first, &value), S_OK);
                QCOMPARE(value.vt, VARTYPE(VT_UI4));
                QCOMPARE(value.ulVal, property.second);
            }
        }
        const auto files = record_["files"].toObject();
        for (auto it = files.begin(); it != files.end(); ++it) {
            auto file = stream(it.key());
            QVERIFY2(file, qPrintable(it.key()));
            check(file, it.key(), 0, 200000);
        }
        NWindows::NCOM::CPropVariant target;
        QCOMPARE(archive_->GetProperty(index("HfsFixture/symlink"), kpidSymLink, &target), S_OK);
        QCOMPARE(target.vt, VARTYPE(VT_BSTR));
        QCOMPARE(QString::fromWCharArray(target.bstrVal), QString("alpha"));
    }
    void pageBackedSymlinkTargets_data() {
        QTest::addColumn<QString>("kind");
        QTest::addColumn<QString>("target");
        QTest::newRow("empty") << QString("modern-symlink-empty") << QString();
        QTest::newRow("nul") << QString("modern-symlink-nul") << QString("alpha");
        QTest::newRow("first-nul") << QString("modern-symlink-first-nul") << QString();
        for (const char *suffix : {"limit", "page", "large"})
            QTest::newRow(suffix) << "modern-symlink-" + QString::fromLatin1(suffix)
                                  << QString(4095, 'T');
    }
    void pageBackedSymlinkTargets() {
        QFETCH(QString, kind);
        QFETCH(QString, target);
        loadFixture(kind);
        CMyComPtr2<IInStream, Input> input;
        input.SetFromCls(new Input(bytes_));
        QCOMPARE(archive_->Open(input, nullptr, nullptr), S_OK);
        const QString name = "HfsFixture/symlink";
        NWindows::NCOM::CPropVariant value;
        QCOMPARE(archive_->GetProperty(index(name), kpidSymLink, &value), S_OK);
        QCOMPARE(value.vt, VARTYPE(VT_BSTR));
        QCOMPARE(QString::fromWCharArray(value.bstrVal), target);
        if (kind != "modern-symlink-empty") {
            for (HRESULT failure : {E_FAIL, E_ABORT}) {
                input->failure = failure;
                QCOMPARE(archive_->GetProperty(index(name), kpidSymLink, &value), failure);
            }
            input->failure = S_OK;
            QCOMPARE(archive_->GetProperty(index(name), kpidSymLink, &value), S_OK);
        }
        auto raw = stream(name);
        QVERIFY(raw);
        QCOMPARE(archive_->Close(), S_OK);
        check(raw, name, 0, 100000);
    }
    void permissionsAndFinderInfo() {
        loadFixture("modern-permissions");
        CMyComPtr<IInStream> input = new Input(bytes_);
        QCOMPARE(archive_->Open(input, nullptr, nullptr), S_OK);
        for (const QString &name :
             {QString("HfsFixture/alpha"), QString("HfsFixture/hard-alpha"),
              QString("HfsFixture/links/second-link"), QString("HfsFixture/links")}) {
            for (const auto &property :
                 {qMakePair(kpidUserId, UInt32(0)), qMakePair(kpidGroupId, UInt32(0)),
                  qMakePair(kpidPosixAttrib,
                            UInt32(name == "HfsFixture/links" ? 0040755 : 0100644)),
                  qMakePair(kpidLinks, UInt32(name == "HfsFixture/links" ? 1 : 3))}) {
                NWindows::NCOM::CPropVariant value;
                QCOMPARE(archive_->GetProperty(index(name), property.first, &value), S_OK);
                QCOMPARE(value.vt, VARTYPE(VT_UI4));
                QCOMPARE(value.ulVal, property.second);
            }
        }
        NWindows::NCOM::CPropVariant flags;
        QCOMPARE(archive_->GetProperty(index("HfsFixture/alpha"), kpidCharacts, &flags), S_OK);
        QCOMPARE(flags.vt, VARTYPE(VT_BSTR));
        const QString text = QString::fromWCharArray(flags.bstrVal);
        QVERIFY(text.contains("User NoDump"));
        QVERIFY(text.contains("System Immutable"));
        QVERIFY(text.contains("System Append"));
        auto fileInfo = stream("HfsFixture/alpha:com.apple.FinderInfo");
        auto folderInfo = stream("HfsFixture/links:com.apple.FinderInfo");
        QVERIFY(fileInfo && folderInfo);
        QCOMPARE(archive_->Close(), S_OK);
        check(fileInfo, "HfsFixture/alpha:com.apple.FinderInfo", 0, 32);
        check(folderInfo, "HfsFixture/links:com.apple.FinderInfo", 0, 32);
    }
    void inodeTimesAndSpecialFiles() {
        loadFixture("modern-times");
        CMyComPtr<IInStream> input = new Input(bytes_);
        QCOMPARE(archive_->Open(input, nullptr, nullptr), S_OK);
        for (const QString &name :
             {QString("HfsFixture/alpha"), QString("HfsFixture/hard-alpha"),
              QString("HfsFixture/links/second-link"), QString("HfsFixture/links")}) {
            for (const auto &entry :
                 {qMakePair(kpidCTime, UInt64(2212122496)),
                  qMakePair(kpidMTime, UInt64(4294967295)), qMakePair(kpidChangeTime, UInt64(0)),
                  qMakePair(kpidATime, UInt64(2212122495))}) {
                NWindows::NCOM::CPropVariant value;
                QCOMPARE(archive_->GetProperty(index(name), entry.first, &value), S_OK);
                QCOMPARE(value.vt, VARTYPE(VT_FILETIME));
                const UInt64 actual =
                    UInt64(value.filetime.dwHighDateTime) << 32 | value.filetime.dwLowDateTime;
                QCOMPARE(actual, (entry.second + 11644473600ULL) * 10000000ULL);
            }
        }
        NWindows::NCOM::CPropVariant modified;
        QCOMPARE(archive_->GetArchiveProperty(kpidMTime, &modified), S_OK);
        QCOMPARE(modified.vt, VARTYPE(VT_FILETIME));
        QCOMPARE((UInt64(modified.filetime.dwHighDateTime) << 32) | modified.filetime.dwLowDateTime,
                 (2212122496ULL + 11644473600ULL) * 10000000ULL);
        for (PROPID property : {PROPID(kpidMTime), PROPID(kpidATime), PROPID(kpidChangeTime)}) {
            NWindows::NCOM::CPropVariant value;
            QCOMPARE(archive_->GetProperty(index("HfsFixture/null-name"), property, &value), S_OK);
            QCOMPARE(value.vt, VARTYPE(VT_FILETIME));
            QCOMPARE((UInt64(value.filetime.dwHighDateTime) << 32) | value.filetime.dwLowDateTime,
                     (2212122496ULL + 11644473600ULL) * 10000000ULL);
        }
        QCOMPARE(archive_->Close(), S_OK);
        loadFixture("modern-special");
        input = new Input(bytes_);
        QCOMPARE(archive_->Open(input, nullptr, nullptr), S_OK);
        for (const auto &entry : {qMakePair(QString("HfsFixture/null-name"), UInt32(0020640)),
                                  qMakePair(QString("HfsFixture/slash_name"), UInt32(0060600)),
                                  qMakePair(QString::fromUtf8("HfsFixture/café"), UInt32(0010644)),
                                  qMakePair(QString::fromUtf8("HfsFixture/각"), UInt32(0140600))}) {
            const unsigned item = index(entry.first);
            for (const auto &property :
                 {qMakePair(kpidPosixAttrib, entry.second), qMakePair(kpidLinks, UInt32(1))}) {
                NWindows::NCOM::CPropVariant value;
                QCOMPARE(archive_->GetProperty(item, property.first, &value), S_OK);
                QCOMPARE(value.vt, VARTYPE(VT_UI4));
                QCOMPARE(value.ulVal, property.second);
            }
            for (PROPID property : {PROPID(kpidDeviceMajor), PROPID(kpidDeviceMinor)}) {
                NWindows::NCOM::CPropVariant value;
                QCOMPARE(archive_->GetProperty(item, property, &value), S_OK);
                if ((entry.second & 0170000) == 0020000 || (entry.second & 0170000) == 0060000) {
                    QCOMPARE(value.vt, VARTYPE(VT_UI4));
                    QCOMPARE(value.ulVal,
                             property == kpidDeviceMajor
                                 ? (entry.first.endsWith("null-name") ? 0xabcU : 0x123U)
                                 : (entry.first.endsWith("null-name") ? 0x54321U : 0xabcdeU));
                } else
                    QCOMPARE(value.vt, VARTYPE(VT_EMPTY));
            }
        }
    }
    void brokenBackingReportsDataError() {
        for (const char *kind : {"modern-missing", "modern-bounds", "modern-binary-case"}) {
            loadFixture(kind);
            CMyComPtr<IInStream> input = new Input(bytes_);
            QCOMPARE(archive_->Open(input, nullptr, nullptr), S_OK);
            const UInt32 item = index("HfsFixture/alpha");
            QVERIFY(item != UInt32(-1));
            CMyComPtr<IInArchiveGetStream> provider;
            QCOMPARE(archive_->QueryInterface(IID_IInArchiveGetStream,
                                              reinterpret_cast<void **>(&provider)),
                     S_OK);
            CMyComPtr<ISequentialInStream> sequential;
            QCOMPARE(provider->GetStream(item, &sequential), S_FALSE);
            QVERIFY(!sequential);
            CMyComPtr2<IArchiveExtractCallback, Extract> callback;
            callback.SetFromCls(new Extract);
            QCOMPARE(archive_->Extract(&item, 1, 1, callback), S_OK);
            QCOMPARE(callback->result, Int32(NArchive::NExtract::NOperationResult::kDataError));
        }
    }
    void errorsAndSeekBoundaries() {
        loadFixture("base");
        CMyComPtr2<IInStream, Input> input;
        input.SetFromCls(new Input(bytes_));
        QCOMPARE(archive_->Open(input, nullptr, nullptr), S_OK);
        auto alpha = stream("HfsFixture/alpha");
        QVERIFY(alpha);
        char data[37];
        UInt32 count = 99;
        input->eof = true;
        QCOMPARE(alpha->Read(data, sizeof(data), &count), S_FALSE);
        QCOMPARE(count, UInt32(0));
        input->eof = false;
        input->seekFailure = E_NOTIMPL;
        QCOMPARE(alpha->Read(data, sizeof(data), &count), E_NOTIMPL);
        QCOMPARE(count, UInt32(0));
        check(alpha, "HfsFixture/alpha", 0, 37);
        input->failure = E_FAIL;
        QCOMPARE(alpha->Read(data, sizeof(data), &count), E_FAIL);
        input->failure = S_OK;
        check(alpha, "HfsFixture/alpha", 37, 37);
        UInt64 position = 0;
        QVERIFY(alpha->Seek(INT64_MIN, STREAM_SEEK_CUR, nullptr) != S_OK);
        QCOMPARE(alpha->Seek(0, STREAM_SEEK_CUR, &position), S_OK);
        QCOMPARE(position, UInt64(74));
        QCOMPARE(alpha->Seek(INT64_MAX, STREAM_SEEK_SET, nullptr), S_OK);
        QVERIFY(alpha->Seek(1, STREAM_SEEK_CUR, nullptr) != S_OK);
        check(alpha, "HfsFixture/alpha", 0, 128);
        CMyComPtr2<IArchiveExtractCallback, Extract> callback;
        callback.SetFromCls(new Extract);
        callback->cancel = true;
        const UInt32 item = index("HfsFixture/alpha");
        QCOMPARE(archive_->Extract(&item, 1, 1, callback), E_ABORT);
        callback->cancel = false;
        input->eof = true;
        QCOMPARE(archive_->Extract(&item, 1, 1, callback), S_OK);
        QCOMPARE(callback->result, Int32(NArchive::NExtract::NOperationResult::kDataError));
        input->eof = false;
        QCOMPARE(archive_->Close(), S_OK);
    }
    void failedOpenClearsCatalog() {
        for (const char *kind :
             {"wrapper-zero-step", "wrapper-nested-short", "wrapper-backup-signature",
              "wrapper-version-old", "wrapper-version-new", "leaf-height", "metadata-short-map",
              "parent-cycle", "wrapped-short", "modern-no-root", "modern-invalid-file-mode",
              "modern-invalid-dir-mode", "modern-thread-parent", "modern-thread-name",
              "modern-thread-type", "modern-thread-length"}) {
            loadFixture(kind);
            CMyComPtr2<IInStream, Input> input;
            input.SetFromCls(new Input(bytes_));
            QVERIFY(archive_->Open(input, nullptr, nullptr) != S_OK);
            UInt32 count = 99;
            QCOMPARE(archive_->GetNumberOfItems(&count), S_OK);
            QCOMPARE(count, UInt32(0));
        }
        CMyComPtr2<IInStream, Input> input;
        CMyComPtr2<IArchiveOpenCallback, CancelOpen> callback;
        callback.SetFromCls(new CancelOpen);
        UInt32 count = 99;
        for (const char *kind : {"base", "wrapper-nested"}) {
            loadFixture(kind);
            input.SetFromCls(new Input(bytes_));
            QCOMPARE(archive_->Open(input, nullptr, callback), E_ABORT);
            QCOMPARE(archive_->GetNumberOfItems(&count), S_OK);
            QCOMPARE(count, UInt32(0));
        }
        loadFixture("base");
        input.SetFromCls(new Input(bytes_));
        input->failure = E_FAIL;
        QCOMPARE(archive_->Open(input, nullptr, nullptr), E_FAIL);
        QCOMPARE(archive_->GetNumberOfItems(&count), S_OK);
        QCOMPARE(count, UInt32(0));
        input.SetFromCls(new Input(bytes_));
        QCOMPARE(archive_->Open(input, nullptr, nullptr), S_OK);
        check(stream("HfsFixture/alpha"), "HfsFixture/alpha", 0, 4096);
    }
    void backendExtraction_data() {
        QTest::addColumn<QString>("kind");
        for (const char *kind :
             {"preallocated", "overflow-leaves", "extent-over-volume", "xattr-padding", "modern",
              "modern-nul", "modern-zlib", "modern-lzvn", "modern-permissions", "modern-times",
              "modern-symlink-nul", "wrapper-nested", "wrapper-version-hplus5",
              "wrapper-version-hfsx4", "free-overflow"})
            QTest::newRow(kind) << QString::fromLatin1(kind);
    }
    void backendExtraction() {
        QFETCH(QString, kind);
        loadFixture(kind);
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString path = temporary.filePath(QString::fromUtf8("образ 空.hfs"));
        QFile image(path);
        QVERIFY(image.open(QIODevice::WriteOnly));
        QCOMPARE(image.write(bytes_), bytes_.size());
        image.close();
        // Each case owns its operation state while reusing the immutable format
        // catalog. Repeated discovery tests the loader's disk deadline, not HFS.
        std::unique_ptr<ArchiveBackend> independent(backendPrototype_->createIndependentBackend());
        ArchiveBackend &backend = *independent;
        QSignalSpy finished(&backend, &ArchiveBackend::finished);
        QList<ArchiveEntry> listedEntries;
        connect(&backend, &ArchiveBackend::entriesReady, &backend,
                [&](const QList<ArchiveEntry> &entries) { listedEntries = entries; });
        backend.list(path);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
        QVERIFY2(finished[0][0].toBool(), qPrintable(finished[0][1].toString()));
        QVERIFY(!backend.codecCatalog()->canUpdate("HFS"));
        if (kind == "modern-symlink-nul") {
            bool found = false;
            for (const auto &entry : listedEntries)
                if (QString(entry.path).replace('\\', '/') == "HfsFixture/symlink") {
                    found = true;
                    QVERIFY(entry.link);
                    QCOMPARE(entry.properties.value("Symbolic Link"), QString("alpha"));
                }
            QVERIFY(found);
        }
        finished.clear();
        QString transcript;
        connect(&backend, &ArchiveBackend::outputReceived, &backend,
                [&](const QString &text) { transcript += text; });
        const QString name = "HfsFixture/alpha";
        const QString output = temporary.filePath("output");
        backend.extract(path, output, {name});
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
        const bool corrupt = kind == "extent-over-volume";
        QCOMPARE(finished[0][0].toBool(), !corrupt);
        if (!corrupt) {
            QFile file(QDir(output).filePath(name));
            QVERIFY(file.open(QIODevice::ReadOnly));
            const QByteArray actual = file.readAll();
            const QByteArray expected =
                QByteArray::fromBase64(record_["files"].toObject()[name].toString().toLatin1());
            QVERIFY2(actual == expected, qPrintable(QString("Expected %1 bytes, got %2.\n%3")
                                                        .arg(expected.size())
                                                        .arg(actual.size())
                                                        .arg(transcript)));
            if (kind == "modern-times")
                QCOMPARE(QFileInfo(file).lastModified().toSecsSinceEpoch(), qint64(4294967295));
            if (kind == "modern-symlink-nul") {
                finished.clear();
                backend.extract(path, output, {"HfsFixture/symlink"});
                QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
                QVERIFY(!finished[0][0].toBool());
                const QFileInfo link(QDir(output).filePath("HfsFixture/symlink"));
                QVERIFY(!link.exists());
                QVERIFY(!link.isSymLink());
            }
            QStringList entries{QString("HfsFixture/alpha:rsrc"),
                                QString("HfsFixture/alpha:com.sevenzip.fixture"),
                                QString::fromUtf8("HfsFixture/данные 空")};
            if (kind == "modern-zlib" || kind == "modern-lzvn")
                entries.removeAll("HfsFixture/alpha:com.sevenzip.fixture");
            if (kind.startsWith("modern")) {
                entries << QString("HfsFixture/hard-alpha") << QString::fromUtf8("HfsFixture/café")
                        << QString::fromUtf8("HfsFixture/각");
                if (kind == "modern-nul")
                    entries << QString::fromUtf8("HfsFixture/null␀name");
                if (kind == "modern-permissions")
                    entries << "HfsFixture/alpha:com.apple.FinderInfo"
                            << "HfsFixture/links:com.apple.FinderInfo";
            }
            for (const QString &entry : entries) {
                finished.clear();
                const QString saved = temporary.filePath("stream.bin");
                QFile::remove(saved);
                backend.extractSourceEntry(path, entry, saved, "HFS", {}, entry.contains(':'));
                QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
                QVERIFY2(finished[0][0].toBool(), qPrintable(finished[0][1].toString()));
                QFile source(saved);
                QVERIFY(source.open(QIODevice::ReadOnly));
                QCOMPARE(source.readAll(),
                         QByteArray::fromBase64(
                             record_["files"].toObject()[entry].toString().toLatin1()));
            }
        }
        QVERIFY(image.open(QIODevice::ReadOnly));
        QCOMPARE(image.readAll(), bytes_);
    }
    void cleanupTestCase() { archive_.Release(); }
};

QTEST_GUILESS_MAIN(HfsStreamTests)
#include "hfs_stream_tests.moc"
