// clang-format off
#include "Common/MyInitGuid.h"
// clang-format on
#include "7zip/Archive/IArchive.h"
#include "Common/MyCom.h"
#include "Windows/PropVariant.h"
#include "formats/exfat/support/exfat_fixture.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QLibrary>
#include <QProcess>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>
#include <map>

namespace {
class Input final : public IInStream, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_2(ISequentialInStream, IInStream)
    QByteArray bytes_;
    UInt64 position_ = 0;

  public:
    bool *cancelOnRead = nullptr;
    UInt64 failureOffset = UINT64_MAX;
    UInt64 failureEnd = UINT64_MAX;
    HRESULT failureResult = E_FAIL;
    explicit Input(QByteArray bytes) : bytes_(std::move(bytes)) {}
    Z7_COM7F_IMF(Read(void *data, UInt32 size, UInt32 *processed)) override {
        if (processed)
            *processed = 0;
        if (position_ >= failureOffset && position_ < failureEnd)
            return failureResult;
        const UInt64 remaining = position_ < UInt64(bytes_.size()) ? bytes_.size() - position_ : 0;
        size = UInt32(qMin<UInt64>(size, remaining));
        if (size)
            memcpy(data, bytes_.constData() + position_, size);
        position_ += size;
        if (processed)
            *processed = size;
        if (cancelOnRead)
            *cancelOnRead = true;
        return S_OK;
    }
    Z7_COM7F_IMF(Seek(Int64 offset, UInt32 origin, UInt64 *position)) override {
        const Int64 base = origin == STREAM_SEEK_SET   ? 0
                           : origin == STREAM_SEEK_CUR ? Int64(position_)
                                                       : bytes_.size();
        if (origin > STREAM_SEEK_END || offset < -base || (offset > 0 && base > INT64_MAX - offset))
            return E_INVALIDARG;
        position_ = UInt64(base + offset);
        if (position)
            *position = position_;
        return S_OK;
    }
};
class Output final : public ISequentialOutStream, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_1(ISequentialOutStream)
  public:
    QByteArray bytes;
    QByteArray *destination = nullptr;
    bool fail = false;
    Z7_COM7F_IMF(Write(const void *data, UInt32 size, UInt32 *processed)) override {
        if (processed)
            *processed = 0;
        if (fail)
            return E_FAIL;
        (destination ? *destination : bytes).append(static_cast<const char *>(data), size);
        if (processed)
            *processed = size;
        return S_OK;
    }
};
class Extract final : public IArchiveExtractCallback, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_1(IArchiveExtractCallback)
  public:
    CMyComPtr2<ISequentialOutStream, Output> output;
    bool cancel = false;
    Int32 result = -1;
    bool collect = false;
    UInt32 current = 0;
    std::map<UInt32, QByteArray> files;
    std::map<UInt32, Int32> results;
    Extract() { output.Create_if_Empty(); }
    Z7_COM7F_IMF(SetTotal(UInt64)) override { return S_OK; }
    Z7_COM7F_IMF(SetCompleted(const UInt64 *)) override { return cancel ? E_ABORT : S_OK; }
    Z7_COM7F_IMF(GetStream(UInt32 index, ISequentialOutStream **stream, Int32 mode)) override {
        current = index;
        if (collect)
            output->destination = &files[index];
        *stream = nullptr;
        if (mode == NArchive::NExtract::NAskMode::kExtract) {
            *stream = output.Interface();
            (*stream)->AddRef();
        }
        return S_OK;
    }
    Z7_COM7F_IMF(PrepareOperation(Int32)) override { return S_OK; }
    Z7_COM7F_IMF(SetOperationResult(Int32 value)) override {
        result = value;
        results[current] = value;
        return S_OK;
    }
};
class OpenProgress final : public IArchiveOpenCallback, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_1(IArchiveOpenCallback)
  public:
    HRESULT failure = E_ABORT;
    Z7_COM7F_IMF(SetTotal(const UInt64 *, const UInt64 *)) override { return S_OK; }
    Z7_COM7F_IMF(SetCompleted(const UInt64 *, const UInt64 *)) override { return failure; }
};
QString path(IInArchive *archive, UInt32 index) {
    NWindows::NCOM::CPropVariant prop;
    if (archive->GetProperty(index, kpidPath, &prop) != S_OK || prop.vt != VT_BSTR)
        return {};
    return QString::fromWCharArray(prop.bstrVal);
}
QByteArray read(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
void setVolumeLabel(ExfatFixture &fixture, int entry, const QString &label) {
    const int offset = fixture.rootOffset(entry);
    std::fill_n(fixture.image.data() + offset, 32, '\0');
    fixture.image[offset] = char(0x83);
    fixture.image[offset + 1] = char(label.size());
    for (int i = 0; i < label.size(); ++i)
        qToLittleEndian(label[i].unicode(), fixture.image.data() + offset + 2 + i * 2);
}
} // namespace

class ExfatTests final : public QObject {
    Q_OBJECT
    QLibrary module_{QString::fromUtf8(SEVENZIP_EXFAT_MODULE)};
    CMyComPtr<IInArchive> archive_;
    ExfatFixture fixture_;
    HRESULT open(const QByteArray &bytes, IArchiveOpenCallback *callback = nullptr) {
        CMyComPtr<IInStream> input = new Input(bytes);
        return archive_->Open(input, nullptr, callback);
    }
    UInt32 indexOf(const QString &name) {
        UInt32 count = 0;
        archive_->GetNumberOfItems(&count);
        for (UInt32 i = 0; i < count; ++i)
            if (path(archive_, i) == name)
                return i;
        return UInt32(-1);
    }
  private slots:
    void initTestCase() {
        QVERIFY2(module_.load(), qPrintable(module_.errorString()));
        const auto property =
            reinterpret_cast<Func_GetHandlerProperty2>(module_.resolve("GetHandlerProperty2"));
        const auto create = reinterpret_cast<Func_CreateObject>(module_.resolve("CreateObject"));
        QVERIFY(property && create);
        NWindows::NCOM::CPropVariant clsid;
        QCOMPARE(property(0, NArchive::NHandlerPropID::kClassID, &clsid), S_OK);
        QCOMPARE(clsid.vt, VARTYPE(VT_BSTR));
        GUID guid;
        memcpy(&guid, clsid.bstrVal, sizeof(guid));
        QCOMPARE(create(&guid, &IID_IInArchive, reinterpret_cast<void **>(&archive_)), S_OK);
        void *writer = nullptr;
        QCOMPARE(create(&guid, &IID_IOutArchive, &writer), CLASS_E_CLASSNOTAVAILABLE);
        QVERIFY(!writer);
    }
    void treeAndFileContents_data() {
        QTest::addColumn<int>("layout");
        QTest::newRow("exfat") << 0;
        QTest::newRow("texfat-first") << 1;
        QTest::newRow("texfat-second") << 2;
        QTest::newRow("texfat-fragmented-bitmap-first") << 3;
        QTest::newRow("texfat-fragmented-bitmap-second") << 4;
        QTest::newRow("texfat-4096-byte-sector") << 5;
    }
    void directReads_data() { treeAndFileContents_data(); }
    void directReads() {
        QFETCH(int, layout);
        ExfatFixture fixture;
        if (layout)
            fixture = TexfatFixture(layout == 5 ? 1 : (layout - 1) % 2, layout == 5 ? 4096 : 512,
                                    layout == 3 || layout == 4);
        QCOMPARE(open(fixture.image), S_OK);
        CMyComPtr<IInArchiveGetStream> streams;
        QCOMPARE(archive_.QueryInterface(IID_IInArchiveGetStream, &streams), S_OK);
        const std::map<QString, QByteArray> expected{
            {fixture.fragmentedName, QByteArray(fixture.sectorSize, 'A') + QByteArray(188, 'B')},
            {fixture.directoryName + '/' + fixture.nestedName,
             QByteArray("tail") + QByteArray(2 * fixture.sectorSize - 4, '\0')},
            {"empty.txt", {}}};
        for (const auto &[name, bytes] : expected) {
            CMyComPtr<ISequentialInStream> sequential;
            QCOMPARE(streams->GetStream(indexOf(name), &sequential), S_OK);
            CMyComPtr<IInStream> input;
            QCOMPARE(sequential.QueryInterface(IID_IInStream, &input), S_OK);
            for (qint64 offset : {qMax<qint64>(0, bytes.size() - 45), qint64(0),
                                  qint64(fixture.sectorSize - 8), qint64(2)}) {
                QCOMPARE(input->Seek(offset, STREAM_SEEK_SET, nullptr), S_OK);
                QByteArray result;
                const int wanted = int(qMax<qint64>(0, qMin<qint64>(83, bytes.size() - offset)));
                while (result.size() < wanted) {
                    char data[83];
                    UInt32 count = 0;
                    QCOMPARE(input->Read(data, wanted - result.size(), &count), S_OK);
                    QVERIFY(count > 0);
                    result.append(data, int(count));
                }
                QCOMPARE(result, bytes.mid(offset, wanted));
            }
            QCOMPARE(input->Seek(0, STREAM_SEEK_END, nullptr), S_OK);
            char data;
            UInt32 count = 1;
            QCOMPARE(input->Read(&data, 1, &count), S_OK);
            QCOMPARE(count, 0u);
            QCOMPARE(input->Seek(-1, STREAM_SEEK_SET, nullptr), HRESULT_WIN32_ERROR_NEGATIVE_SEEK);
        }
    }
    void directStreamRetainsGenerationAndReportsErrors() {
        ExfatFixture fixture;
        CMyComPtr2<IInStream, Input> input;
        input.SetFromCls(new Input(fixture.image));
        QCOMPARE(archive_->Open(input, nullptr, nullptr), S_OK);
        CMyComPtr<IInArchiveGetStream> streams;
        QCOMPARE(archive_.QueryInterface(IID_IInArchiveGetStream, &streams), S_OK);
        CMyComPtr<ISequentialInStream> sequential;
        QCOMPARE(streams->GetStream(indexOf(fixture.fragmentedName), &sequential), S_OK);
        CMyComPtr<IInStream> file;
        QCOMPARE(sequential.QueryInterface(IID_IInStream, &file), S_OK);
        CMyComPtr<ISequentialInStream> other;
        QCOMPARE(streams->GetStream(indexOf(fixture.fragmentedName), &other), S_OK);
        QCOMPARE(archive_->Close(), S_OK);
        auto replacement = fixture.image;
        replacement[fixture.clusterOffset(5)] = 'X';
        QCOMPARE(open(replacement), S_OK);
        char bytes[4] = {};
        UInt32 count = 0;
        QCOMPARE(file->Read(bytes, sizeof(bytes), &count), S_OK);
        QCOMPARE(QByteArray(bytes, count), QByteArray("AAAA"));
        QCOMPARE(other->Read(bytes, sizeof(bytes), &count), S_OK);
        QCOMPARE(QByteArray(bytes, count), QByteArray("AAAA"));
        input->failureOffset = fixture.clusterOffset(9);
        input->failureEnd = input->failureOffset + fixture.sectorSize;
        for (HRESULT failure : {E_ABORT, E_OUTOFMEMORY, E_FAIL}) {
            input->failureResult = failure;
            QCOMPARE(file->Seek(fixture.sectorSize, STREAM_SEEK_SET, nullptr), S_OK);
            count = 4;
            QCOMPARE(file->Read(bytes, sizeof(bytes), &count), failure);
            QCOMPARE(count, 0u);
        }
        QCOMPARE(file->Seek(0, STREAM_SEEK_SET, nullptr), S_OK);
        QCOMPARE(file->Read(bytes, sizeof(bytes), &count), S_OK);
        QCOMPARE(QByteArray(bytes, count), QByteArray("AAAA"));
    }
    void randomSeekRejectsCyclicChain() {
        ExfatFixture fixture;
        fixture.fat(5, 5);
        QCOMPARE(open(fixture.image), S_OK);
        CMyComPtr<IInArchiveGetStream> streams;
        QCOMPARE(archive_.QueryInterface(IID_IInArchiveGetStream, &streams), S_OK);
        CMyComPtr<ISequentialInStream> sequential;
        QCOMPARE(streams->GetStream(indexOf(fixture.fragmentedName), &sequential), S_OK);
        CMyComPtr<IInStream> file;
        QCOMPARE(sequential.QueryInterface(IID_IInStream, &file), S_OK);
        QCOMPARE(file->Seek(fixture.sectorSize, STREAM_SEEK_SET, nullptr), S_OK);
        char byte = 'x';
        UInt32 count = 1;
        QVERIFY(file->Read(&byte, 1, &count) != S_OK);
        QCOMPARE(count, 0u);
        QCOMPARE(byte, 'x');
    }
    void treeAndFileContents() {
        QFETCH(int, layout);
        ExfatFixture fixture;
        if (layout)
            fixture = TexfatFixture(layout == 5 ? 1 : (layout - 1) % 2, layout == 5 ? 4096 : 512,
                                    layout == 3 || layout == 4);
        QCOMPARE(open(fixture.image), S_OK);
        UInt32 count = 0;
        QCOMPARE(archive_->GetNumberOfItems(&count), S_OK);
        QCOMPARE(count, 5u);
        const std::map<QString, QByteArray> expected{
            {fixture.fragmentedName, QByteArray(fixture.sectorSize, 'A') + QByteArray(188, 'B')},
            {fixture.longName, "end"},
            {"empty.txt", {}},
            {fixture.directoryName + '/' + fixture.nestedName,
             QByteArray("tail") + QByteArray(2 * fixture.sectorSize - 4, '\0')}};
        for (const auto &[name, bytes] : expected) {
            const UInt32 index = indexOf(name);
            QVERIFY2(index != UInt32(-1), qPrintable(name));
            CMyComPtr2<IArchiveExtractCallback, Extract> callback;
            callback.Create_if_Empty();
            QCOMPARE(archive_->Extract(&index, 1, 0, callback), S_OK);
            QCOMPARE(callback->result, Int32(NArchive::NExtract::NOperationResult::kOK));
            QCOMPARE(callback->output->bytes, bytes);
        }
        NWindows::NCOM::CPropVariant timestamp;
        QCOMPARE(archive_->GetProperty(indexOf(fixture.fragmentedName), kpidMTime, &timestamp),
                 S_OK);
        QCOMPARE(timestamp.vt, VARTYPE(VT_FILETIME));
        const quint64 ticks =
            (quint64(timestamp.filetime.dwHighDateTime) << 32) | timestamp.filetime.dwLowDateTime;
        const auto utc = QDateTime(QDate(2026, 10, 17), QTime(9, 34, 57, 230), QTimeZone::UTC);
        QCOMPARE(qint64(ticks / 10000) - 11644473600000LL, utc.toMSecsSinceEpoch());
    }
    void zeroSizeDirectories_data() {
        QTest::addColumn<int>("layout");
        QTest::addColumn<quint32>("cluster");
        QTest::newRow("unallocated") << 0 << quint32(0);
        QTest::newRow("stale-data") << 0 << quint32(6);
        QTest::newRow("root-reference") << 0 << quint32(2);
        QTest::newRow("out-of-range-reference") << 0 << quint32(0xfffffffe);
        QTest::newRow("texfat-first") << 1 << quint32(0);
        QTest::newRow("texfat-second") << 2 << quint32(0);
        QTest::newRow("sector4096") << 3 << quint32(0);
    }
    void zeroSizeDirectories() {
        QFETCH(int, layout);
        QFETCH(quint32, cluster);
        ExfatFixture fixture(layout == 3 ? 4096 : 512);
        if (layout == 1 || layout == 2)
            fixture = TexfatFixture(layout - 1);
        else {
            fixture.fat(10, 11);
            fixture.fat(11, 0xffffffff);
            const int bitmap = fixture.clusterOffset(3) + 1;
            fixture.image[bitmap] = char(quint8(fixture.image[bitmap]) | 2);
        }
        fixture.emptyDirectory(6, fixture.directoryName, cluster);
        const QString second = QStringLiteral("empty 空");
        fixture.emptyDirectory(layout == 1 || layout == 2 ? 34 : 31, second);
        QCOMPARE(open(fixture.image), S_OK);
        UInt32 count;
        QCOMPARE(archive_->GetNumberOfItems(&count), S_OK);
        QCOMPARE(count, 5u);
        QVERIFY(indexOf(fixture.directoryName + '/' + fixture.nestedName) == UInt32(-1));
        for (const QString &name : {fixture.directoryName, second}) {
            const UInt32 index = indexOf(name);
            QVERIFY(index != UInt32(-1));
            NWindows::NCOM::CPropVariant directory;
            QCOMPARE(archive_->GetProperty(index, kpidIsDir, &directory), S_OK);
            QCOMPARE(directory.vt, VARTYPE(VT_BOOL));
            QCOMPARE(directory.boolVal, VARIANT_TRUE);
            CMyComPtr2<IArchiveExtractCallback, Extract> callback;
            callback.Create_if_Empty();
            QCOMPARE(archive_->Extract(&index, 1, 0, callback), S_OK);
            QCOMPARE(callback->result, Int32(NArchive::NExtract::NOperationResult::kOK));
            QVERIFY(callback->output->bytes.isEmpty());
        }
    }
    void volumeAllocation_data() {
        QTest::addColumn<int>("layout");
        QTest::addColumn<quint64>("freeClusters");
        QTest::newRow("exfat") << 0 << quint64(119);
        QTest::newRow("sector4096") << 1 << quint64(119);
        QTest::newRow("texfat-first") << 2 << quint64(117);
        QTest::newRow("texfat-second") << 3 << quint64(117);
        QTest::newRow("fragmented-bitmap-first") << 4 << quint64(7666);
        QTest::newRow("fragmented-bitmap-second") << 5 << quint64(7666);
        QTest::newRow("partial-word-padding") << 6 << quint64(132);
        QTest::newRow("partial-word-last-cluster") << 7 << quint64(131);
        QTest::newRow("multi-sector-clusters") << 8 << quint64(119);
    }
    void volumeAllocation() {
        QFETCH(int, layout);
        QFETCH(quint64, freeClusters);
        ExfatFixture fixture(layout == 1 ? 4096 : 512);
        if (layout >= 2 && layout <= 5)
            fixture = TexfatFixture((layout - 2) % 2, 512, layout >= 4);
        if (layout == 6 || layout == 7) {
            // 397 clusters leave a partial final bitmap word. Bits beyond the
            // heap deliberately look allocated and must not reduce free space.
            fixture.set32(92, 397);
            fixture.set64(fixture.rootOffset(0) + 24, 50);
            const int bitmap = fixture.clusterOffset(3);
            std::fill_n(fixture.image.data() + bitmap + 49, fixture.sectorSize - 49, char(0xff));
            fixture.image[bitmap + 49] = char(layout == 6 ? 0xe0 : 0xf0);
            fixture.refreshBootChecksum();
        }
        if (layout == 8) {
            // A metadata-only volume with 1024-byte clusters and 512-byte
            // sectors distinguishes allocation units from I/O geometry.
            const QByteArray original = fixture.image;
            fixture.image.resize((fixture.dataSector + 384 * 2) * fixture.sectorSize);
            std::fill(fixture.image.begin() + fixture.dataSector * fixture.sectorSize,
                      fixture.image.end(), '\0');
            std::copy_n(original.constData() + fixture.clusterOffset(2), 64,
                        fixture.image.data() + fixture.clusterOffset(2));
            std::copy_n(original.constData() + fixture.clusterOffset(3), 48,
                        fixture.image.data() + (fixture.dataSector + 2) * fixture.sectorSize);
            std::copy_n(original.constData() + fixture.clusterOffset(20), 65536 * 2,
                        fixture.image.data() + (fixture.dataSector + 36) * fixture.sectorSize);
            fixture.fat(2, 0xffffffff);
            fixture.image[109] = 1;
            fixture.set64(72, fixture.image.size() / fixture.sectorSize);
            fixture.refreshBootChecksum();
        }
        const unsigned clusterSize = fixture.sectorSize * (layout == 8 ? 2 : 1);
        QCOMPARE(open(fixture.image), S_OK);
        NWindows::NCOM::CPropVariant value;
        QCOMPARE(archive_->GetArchiveProperty(kpidFreeSpace, &value), S_OK);
        QCOMPARE(value.vt, VARTYPE(VT_UI8));
        QCOMPARE(value.uhVal.QuadPart, UInt64(freeClusters * clusterSize));
        for (PROPID property : {kpidClusterSize, kpidSectorSize}) {
            QCOMPARE(archive_->GetArchiveProperty(property, &value), S_OK);
            QCOMPARE(value.vt, VARTYPE(VT_UI4));
            QCOMPARE(value.ulVal,
                     UInt32(property == kpidClusterSize ? clusterSize : fixture.sectorSize));
        }
        QCOMPARE(archive_->Close(), S_OK);
        QCOMPARE(archive_->GetArchiveProperty(kpidFreeSpace, &value), S_OK);
        QCOMPARE(value.vt, VARTYPE(VT_EMPTY));
    }
    void volumeLabels_data() {
        QTest::addColumn<int>("layout");
        QTest::addColumn<QString>("scenario");
        QTest::addColumn<QString>("label");
        QTest::addColumn<QString>("expected");
        const QString unicode = QStringLiteral("том 空 😀");
        QTest::newRow("first-cluster") << 0 << "first" << unicode << unicode;
        QTest::newRow("fragmented-root") << 0 << "late" << unicode << unicode;
        QTest::newRow("texfat-first") << 1 << "late" << unicode << unicode;
        QTest::newRow("texfat-second") << 2 << "late" << unicode << unicode;
        QTest::newRow("sector4096") << 3 << "late" << unicode << unicode;
        QTest::newRow("maximum") << 0 << "late" << "12345678901" << "12345678901";
        QTest::newRow("empty") << 0 << "late" << "" << "";
        QTest::newRow("absent") << 0 << "absent" << "" << "";
        QTest::newRow("deleted") << 0 << "deleted" << unicode << "";
        QTest::newRow("after-unused") << 1 << "after-unused" << unicode << "";
        QTest::newRow("first-wins") << 1 << "duplicate" << unicode << unicode;
        QTest::newRow("embedded-nul") << 0 << "late" << QString(QChar('A')) + QChar(0) + 'B' << "A";
    }
    void volumeLabels() {
        QFETCH(int, layout);
        QFETCH(QString, scenario);
        QFETCH(QString, label);
        QFETCH(QString, expected);
        ExfatFixture fixture(layout == 3 ? 4096 : 512);
        if (layout == 1 || layout == 2)
            fixture = TexfatFixture(layout - 1);
        int entry = layout == 1 || layout == 2 ? 34 : 31;
        if (scenario == "first") {
            for (int i = 30; i >= 2; --i)
                memcpy(fixture.image.data() + fixture.rootOffset(i + 1),
                       fixture.image.constData() + fixture.rootOffset(i), 32);
            entry = 2;
        }
        if (scenario == "after-unused")
            ++entry;
        if (scenario != "absent")
            setVolumeLabel(fixture, entry, label);
        if (scenario == "deleted")
            fixture.image[fixture.rootOffset(entry)] = 3;
        if (scenario == "duplicate") {
            setVolumeLabel(fixture, entry + 1, "other");
            fixture.image[fixture.rootOffset(entry + 1) + 1] = 12; // Ignored later malformed label.
        }
        QCOMPARE(open(fixture.image), S_OK);
        NWindows::NCOM::CPropVariant volume;
        QCOMPARE(archive_->GetArchiveProperty(kpidVolumeName, &volume), S_OK);
        QCOMPARE(volume.vt, VARTYPE(VT_BSTR));
        QString text;
        for (const wchar_t *p = volume.bstrVal; p && *p; ++p)
            text += QChar(ushort(*p));
        QCOMPARE(text, expected);
        UInt32 count;
        QCOMPARE(archive_->GetNumberOfItems(&count), S_OK);
        QCOMPARE(count, 5u);
        const UInt32 index = indexOf(fixture.fragmentedName);
        QVERIFY(index != UInt32(-1));
        CMyComPtr2<IArchiveExtractCallback, Extract> callback;
        callback.Create_if_Empty();
        QCOMPARE(archive_->Extract(&index, 1, 0, callback), S_OK);
        QCOMPARE(callback->result, Int32(NArchive::NExtract::NOperationResult::kOK));
        QCOMPARE(callback->output->bytes,
                 QByteArray(fixture.sectorSize, 'A') + QByteArray(188, 'B'));
        QCOMPARE(archive_->Close(), S_OK);
        QCOMPARE(archive_->GetArchiveProperty(kpidVolumeName, &volume), S_OK);
        QCOMPARE(volume.vt, VARTYPE(VT_EMPTY));
    }
    void volumeLabelSourceErrors() {
        ExfatFixture fixture;
        setVolumeLabel(fixture, 31, "label");
        for (HRESULT error : {E_FAIL, E_ABORT}) {
            Input *input = new Input(fixture.image);
            CMyComPtr<IInStream> source = input;
            input->failureOffset = fixture.clusterOffset(10);
            input->failureResult = error;
            QCOMPARE(archive_->Open(source, nullptr, nullptr), error == E_ABORT ? E_ABORT : S_OK);
            UInt32 count;
            QCOMPARE(archive_->GetNumberOfItems(&count), S_OK);
            if (error == E_ABORT)
                QCOMPARE(count, 0u);
            else {
                const UInt32 index = indexOf(fixture.fragmentedName);
                QVERIFY(index != UInt32(-1));
                CMyComPtr2<IArchiveExtractCallback, Extract> callback;
                callback.Create_if_Empty();
                QCOMPARE(archive_->Extract(&index, 1, 0, callback), S_OK);
                QCOMPARE(callback->output->bytes, QByteArray(512, 'A') + QByteArray(188, 'B'));
            }
            NWindows::NCOM::CPropVariant label;
            QCOMPARE(archive_->GetArchiveProperty(kpidVolumeName, &label), S_OK);
            QCOMPARE(label.vt, VARTYPE(VT_EMPTY));
            QCOMPARE(open(fixture.image), S_OK);
        }
    }
    void malformedVolumeLabel() {
        ExfatFixture fixture;
        setVolumeLabel(fixture, 31, "broken");
        fixture.image[fixture.rootOffset(31) + 1] = 12;
        QCOMPARE(open(fixture.image), S_OK);
        UInt32 count;
        QCOMPARE(archive_->GetNumberOfItems(&count), S_OK);
        QCOMPARE(count, 5u);
        NWindows::NCOM::CPropVariant label;
        QCOMPARE(archive_->GetArchiveProperty(kpidVolumeName, &label), S_OK);
        QCOMPARE(label.vt, VARTYPE(VT_EMPTY));
        QCOMPARE(open(fixture_.image), S_OK);
    }
    void entryTimestamps_data() {
        QTest::addColumn<int>("date");
        QTest::addColumn<int>("time");
        QTest::addColumn<int>("cs");
        QTest::addColumn<int>("tz");
        QTest::addColumn<QDateTime>("expected");
        const int date = ((2026 - 1980) << 9) | (10 << 5) | 17;
        const int time = (12 << 11) | (34 << 5) | 28;
        const QDateTime base(QDate(2026, 10, 17), QTime(12, 34, 56), QTimeZone::UTC);
        QTest::newRow("unknown-offset") << date << time << 123 << 0 << base.addMSecs(1230);
        QTest::newRow("unknown-offset-bits") << date << time << 0 << 0x7f << base;
        QTest::newRow("positive-offset")
            << date << time << 199 << 0xbf << base.addSecs(-63 * 900).addMSecs(1990);
        QTest::newRow("negative-offset")
            << date << time << 1 << 0xc0 << base.addSecs(64 * 900).addMSecs(10);
        QTest::newRow("large-increment") << date << time << 255 << 0x80 << base.addMSecs(2550);
        QTest::newRow("zero-date")
            << 0 << 0 << 0 << 0x80 << QDateTime(QDate(1979, 11, 30), QTime(0, 0), QTimeZone::UTC);
        QTest::newRow("leap-day-overflow")
            << (((2024 - 1980) << 9) | (2 << 5) | 30) << 0 << 0 << 0x80
            << QDateTime(QDate(2024, 3, 1), QTime(0, 0), QTimeZone::UTC);
        QTest::newRow("clock-overflow")
            << date << ((31 << 11) | (63 << 5) | 31) << 0 << 0x80
            << QDateTime(QDate(2026, 10, 18), QTime(8, 4, 2), QTimeZone::UTC);
    }
    void entryTimestamps() {
        QFETCH(int, date);
        QFETCH(int, time);
        QFETCH(int, cs);
        QFETCH(int, tz);
        QFETCH(QDateTime, expected);
        ExfatFixture fixture;
        const int offset = fixture.rootOffset(2);
        for (int field : {8, 12, 16}) {
            qToLittleEndian(quint16(time), fixture.image.data() + offset + field);
            qToLittleEndian(quint16(date), fixture.image.data() + offset + field + 2);
        }
        fixture.image[offset + 20] = fixture.image[offset + 21] = char(cs);
        fixture.image[offset + 22] = fixture.image[offset + 23] = fixture.image[offset + 24] =
            char(tz);
        fixture.refreshEntryChecksum(2);
        QCOMPARE(open(fixture.image), S_OK);
        const UInt32 index = indexOf(fixture.fragmentedName);
        QVERIFY(index != UInt32(-1));
        for (PROPID property : {kpidCTime, kpidMTime, kpidATime}) {
            NWindows::NCOM::CPropVariant timestamp;
            QCOMPARE(archive_->GetProperty(index, property, &timestamp), S_OK);
            QCOMPARE(timestamp.vt, VARTYPE(VT_FILETIME));
            const quint64 ticks = (quint64(timestamp.filetime.dwHighDateTime) << 32) |
                                  timestamp.filetime.dwLowDateTime;
            const auto wanted = property == kpidATime ? expected.addMSecs(-cs * 10) : expected;
            QCOMPARE(ticks, quint64(wanted.toMSecsSinceEpoch() + 11644473600000LL) * 10000);
        }
    }
    void timestampsAcrossHostTimezones() {
        for (const auto *zone : {"UTC0", "HST10"}) {
            QProcess process;
            auto environment = QProcessEnvironment::systemEnvironment();
            environment.insert("TZ", QString::fromLatin1(zone));
            process.setProcessEnvironment(environment);
            process.start(QCoreApplication::applicationFilePath(), {"entryTimestamps", "-silent"});
            QVERIFY(process.waitForFinished(15000));
            QCOMPARE(process.exitStatus(), QProcess::NormalExit);
            QVERIFY2(process.exitCode() == 0, process.readAllStandardOutput().constData());
        }
    }
    void largerLogicalSectors() {
        ExfatFixture fixture(4096);
        QCOMPARE(open(fixture.image), S_OK);
        const UInt32 index = indexOf(fixture.fragmentedName);
        QVERIFY(index != UInt32(-1));
        CMyComPtr2<IArchiveExtractCallback, Extract> callback;
        callback.Create_if_Empty();
        QCOMPARE(archive_->Extract(&index, 1, 0, callback), S_OK);
        QCOMPARE(callback->result, Int32(NArchive::NExtract::NOperationResult::kOK));
        QCOMPARE(callback->output->bytes, QByteArray(4096, 'A') + QByteArray(188, 'B'));
    }
    void invalidVolumeGeometry() {
        ExfatFixture f;
        f.set32(92, 0xffffffff);
        f.refreshBootChecksum();
        QVERIFY(open(f.image) != S_OK);
        UInt32 count = 999;
        QCOMPARE(archive_->GetNumberOfItems(&count), S_OK);
        QCOMPARE(count, 0u);
        QCOMPARE(open(fixture_.image), S_OK); // Failed opens leave a reusable handler.
    }
    void recoveryMetadata_data() {
        QTest::addColumn<QString>("fault");
        for (const auto *fault : {"boot-checksum", "boot-signature", "root-cycle", "missing-bitmap",
                                  "root-unallocated", "bitmap-cycle", "short-source"})
            QTest::newRow(fault) << QString::fromLatin1(fault);
    }
    void recoveryMetadata() {
        QFETCH(QString, fault);
        ExfatFixture fixture;
        fixture.recoveryFault(fault);
        QCOMPARE(open(fixture.image), S_OK);
        const UInt32 index = indexOf(fixture.fragmentedName);
        QVERIFY(index != UInt32(-1));
        CMyComPtr2<IArchiveExtractCallback, Extract> callback;
        callback.Create_if_Empty();
        QCOMPARE(archive_->Extract(&index, 1, 0, callback), S_OK);
        QCOMPARE(callback->result, Int32(NArchive::NExtract::NOperationResult::kOK));
        QCOMPARE(callback->output->bytes, QByteArray(512, 'A') + QByteArray(188, 'B'));
        NWindows::NCOM::CPropVariant free;
        QCOMPARE(archive_->GetArchiveProperty(kpidFreeSpace, &free), S_OK);
        if (fault != "short-source")
            QCOMPARE(free.vt, VARTYPE(VT_EMPTY));
        QCOMPARE(open(fixture_.image), S_OK);
        QCOMPARE(archive_->GetArchiveProperty(kpidFreeSpace, &free), S_OK);
        QCOMPARE(free.vt, VARTYPE(VT_UI8));
    }
    void partialFileData_data() {
        QTest::addColumn<QString>("fault");
        for (const auto *fault :
             {"file-free-link", "file-cycle", "file-eof", "file-short-source", "file-io"})
            QTest::newRow(fault) << QString::fromLatin1(fault);
    }
    void partialFileData() {
        QFETCH(QString, fault);
        ExfatFixture fixture;
        if (fault != "file-io")
            fixture.recoveryFault(fault);
        auto *input = new Input(fixture.image);
        if (fault == "file-io") {
            input->failureOffset = fixture.clusterOffset(9);
            input->failureEnd = fixture.clusterOffset(10);
        }
        CMyComPtr<IInStream> source = input;
        QCOMPARE(archive_->Open(source, nullptr, nullptr), S_OK);
        const UInt32 bad = indexOf(fixture.fragmentedName);
        const UInt32 good = indexOf(fixture.directoryName + '/' + fixture.nestedName);
        QVERIFY(bad != UInt32(-1) && good != UInt32(-1));
        const UInt32 indices[] = {bad, good};
        CMyComPtr2<IArchiveExtractCallback, Extract> callback;
        callback.Create_if_Empty();
        callback->collect = true;
        QCOMPARE(archive_->Extract(indices, 2, 0, callback), S_OK);
        QCOMPARE(callback->results[bad], Int32(NArchive::NExtract::NOperationResult::kDataError));
        QCOMPARE(callback->results[good], Int32(NArchive::NExtract::NOperationResult::kOK));
        QCOMPARE(callback->files[bad],
                 QByteArray(512, 'A') +
                     (fault == "file-short-source" ? QByteArray(80, 'B') : QByteArray()));
        QCOMPARE(callback->files[good], QByteArray("tail") + QByteArray(1020, '\0'));
    }
    void unreadableRootIsNotAnEmptySuccessfulArchive() {
        ExfatFixture fixture;
        fixture.recoveryFault("unreadable-root");
        QCOMPARE(open(fixture.image), S_FALSE);
        UInt32 count;
        QCOMPARE(archive_->GetNumberOfItems(&count), S_OK);
        QCOMPARE(count, 0u);
        NWindows::NCOM::CPropVariant error;
        QCOMPARE(archive_->GetArchiveProperty(kpidError, &error), S_OK);
        QCOMPARE(error.vt, VARTYPE(VT_BSTR));
        QVERIFY(::SysStringLen(error.bstrVal) > 0);
        QCOMPARE(open(fixture_.image), S_OK);
    }
    void progressFailureCannotEnableRecovery() {
        ExfatFixture fixture;
        fixture.recoveryFault("boot-checksum");
        CMyComPtr2<IArchiveOpenCallback, OpenProgress> progress;
        progress.Create_if_Empty();
        for (HRESULT failure : {E_ABORT, E_FAIL, E_OUTOFMEMORY}) {
            progress->failure = failure;
            QCOMPARE(open(fixture.image, progress), failure);
        }
        QCOMPARE(open(fixture_.image), S_OK);
    }
    void partialDirectories_data() {
        QTest::addColumn<QString>("fault");
        for (const auto *fault :
             {"entry-checksum", "extent-range", "name-length", "traversal-name", "directory-cycle",
              "child-checksum", "child-chain", "child-io", "alias"})
            QTest::newRow(fault) << QString::fromLatin1(fault);
    }
    void partialDirectories() {
        QFETCH(QString, fault);
        ExfatFixture fixture;
        if (fault != "child-io")
            fixture.directoryFault(fault);
        auto *input = new Input(fixture.image);
        if (fault == "child-io") {
            input->failureOffset = fixture.clusterOffset(6);
            input->failureEnd = fixture.clusterOffset(7);
        }
        CMyComPtr<IInStream> source = input;
        QCOMPARE(archive_->Open(source, nullptr, nullptr), S_OK);
        const bool root = fault == "entry-checksum" || fault == "extent-range" ||
                          fault == "name-length" || fault == "traversal-name";
        const QString healthyName = root ? fixture.longName : fixture.fragmentedName;
        const QByteArray healthy =
            root ? QByteArray("end") : QByteArray(512, 'A') + QByteArray(188, 'B');
        const UInt32 index = indexOf(healthyName);
        QVERIFY(index != UInt32(-1));
        CMyComPtr2<IArchiveExtractCallback, Extract> callback;
        callback.Create_if_Empty();
        QCOMPARE(archive_->Extract(&index, 1, 0, callback), S_OK);
        QCOMPARE(callback->result, Int32(NArchive::NExtract::NOperationResult::kOK));
        QCOMPARE(callback->output->bytes, healthy);
        if (root) {
            QVERIFY(indexOf(fixture.fragmentedName) == UInt32(-1));
            QCOMPARE(archive_->Extract(nullptr, UInt32(-1), 1, callback), S_FALSE);
        } else if (fault != "alias") {
            const UInt32 directory = indexOf(fixture.directoryName);
            QVERIFY(directory != UInt32(-1));
            NWindows::NCOM::CPropVariant error;
            QCOMPARE(archive_->GetProperty(directory, kpidError, &error), S_OK);
            QCOMPARE(error.vt, VARTYPE(VT_BSTR));
            QCOMPARE(archive_->Extract(&directory, 1, 1, callback), S_OK);
            QCOMPARE(callback->result, Int32(NArchive::NExtract::NOperationResult::kDataError));
            if (fault == "child-chain") {
                const UInt32 prefix = indexOf(fixture.directoryName + '/' + fixture.nestedName);
                QVERIFY(prefix != UInt32(-1));
                callback->output->bytes.clear();
                QCOMPARE(archive_->Extract(&prefix, 1, 0, callback), S_OK);
                QCOMPARE(callback->output->bytes, QByteArray("tail") + QByteArray(1020, '\0'));
            }
        } else {
            for (const QString &directory : {fixture.directoryName, QString("alias")}) {
                const UInt32 child = indexOf(directory + '/' + fixture.nestedName);
                QVERIFY(child != UInt32(-1));
                callback->output->bytes.clear();
                QCOMPARE(archive_->Extract(&child, 1, 0, callback), S_OK);
                QCOMPARE(callback->output->bytes, QByteArray("tail") + QByteArray(1020, '\0'));
            }
        }
        QCOMPARE(open(fixture_.image), S_OK);
        NWindows::NCOM::CPropVariant flags;
        QCOMPARE(archive_->GetArchiveProperty(kpidErrorFlags, &flags), S_OK);
        QCOMPARE(flags.vt, VARTYPE(VT_EMPTY));
    }
    void dataFailureAndCancellation() {
        ExfatFixture broken;
        broken.fat(5, 5);
        QCOMPARE(open(broken.image), S_OK);
        UInt32 index = indexOf(broken.fragmentedName);
        CMyComPtr2<IArchiveExtractCallback, Extract> callback;
        callback.Create_if_Empty();
        QCOMPARE(archive_->Extract(&index, 1, 1, callback), S_OK);
        QCOMPARE(callback->result, Int32(NArchive::NExtract::NOperationResult::kDataError));
        CMyComPtr2<IArchiveOpenCallback, OpenProgress> progress;
        progress.Create_if_Empty();
        QCOMPARE(open(fixture_.image, progress), E_ABORT);
        QCOMPARE(open(fixture_.image), S_OK);
        index = indexOf(fixture_.fragmentedName);
        callback->cancel = true;
        QCOMPARE(archive_->Extract(&index, 1, 0, callback), E_ABORT);
        callback->cancel = false;
        callback->output->fail = true;
        QCOMPARE(archive_->Extract(&index, 1, 0, callback), E_FAIL);
    }
    void cancelDuringCyclicFileChain() {
        ExfatFixture broken;
        broken.fat(9, 5);
        auto *input = new Input(broken.image);
        CMyComPtr<IInStream> stream = input;
        QCOMPARE(archive_->Open(stream, nullptr, nullptr), S_OK);
        const UInt32 index = indexOf(broken.fragmentedName);
        QVERIFY(index != UInt32(-1));
        CMyComPtr2<IArchiveExtractCallback, Extract> callback;
        callback.Create_if_Empty();
        // Request cancellation when the source returns data during extraction.
        input->cancelOnRead = &callback->cancel;
        QCOMPARE(archive_->Extract(&index, 1, 0, callback), E_ABORT);
        QVERIFY((QByteArray(512, 'A') + QByteArray(188, 'B')).startsWith(callback->output->bytes));
        QCOMPARE(callback->result, Int32(-1));
        QCOMPARE(open(fixture_.image), S_OK);
        const UInt32 good = indexOf(fixture_.fragmentedName);
        callback->cancel = false;
        callback->output->bytes.clear();
        QCOMPARE(archive_->Extract(&good, 1, 0, callback), S_OK);
        QCOMPARE(callback->result, Int32(NArchive::NExtract::NOperationResult::kOK));
        QCOMPARE(callback->output->bytes, QByteArray(512, 'A') + QByteArray(188, 'B'));
    }
    void statusFlagsPermitReadOnlyData_data() {
        QTest::addColumn<int>("layout");
        QTest::addColumn<unsigned>("status");
        for (int layout : {-1, 0, 1})
            for (unsigned status : {2u, 4u, 6u})
                QTest::newRow(qPrintable(QString::number(layout) + '-' + QString::number(status)))
                    << layout << status;
    }
    void statusFlagsPermitReadOnlyData() {
        QFETCH(int, layout);
        QFETCH(unsigned, status);
        ExfatFixture fixture;
        if (layout >= 0)
            fixture = TexfatFixture(unsigned(layout));
        fixture.image[106] = char(status | (layout < 0 ? 0 : layout));
        const QByteArray original = fixture.image;
        QCOMPARE(open(fixture.image), S_OK);
        UInt32 properties = 0;
        QCOMPARE(archive_->GetNumberOfArchiveProperties(&properties), S_OK);
        QString limitations;
        for (UInt32 i = 0; i < properties; ++i) {
            CMyComBSTR name;
            PROPID id;
            VARTYPE type;
            QCOMPARE(archive_->GetArchivePropertyInfo(i, &name, &id, &type), S_OK);
            if (!name || QString::fromWCharArray(name) != "Read-only limitations")
                continue;
            NWindows::NCOM::CPropVariant value;
            QCOMPARE(archive_->GetArchiveProperty(id, &value), S_OK);
            QCOMPARE(value.vt, VARTYPE(VT_BSTR));
            limitations = QString::fromWCharArray(value.bstrVal);
        }
        QCOMPARE(limitations.contains("unmounted"), bool(status & 2));
        QCOMPARE(limitations.contains("failures"), bool(status & 4));
        const UInt32 index = indexOf(fixture.fragmentedName);
        QVERIFY(index != UInt32(-1));
        CMyComPtr2<IArchiveExtractCallback, Extract> callback;
        callback.Create_if_Empty();
        QCOMPARE(archive_->Extract(&index, 1, 0, callback), S_OK);
        QCOMPARE(callback->result, Int32(NArchive::NExtract::NOperationResult::kOK));
        QCOMPARE(callback->output->bytes, QByteArray(512, 'A') + QByteArray(188, 'B'));
        QCOMPARE(fixture.image, original);
    }
    void texfatLimitedMetadata_data() {
        QTest::addColumn<unsigned>("active");
        QTest::addColumn<QString>("fault");
        for (unsigned active : {0u, 1u})
            for (const auto *fault : {"missing-bitmap", "duplicate-bitmap", "active-root-cycle",
                                      "active-root-unallocated", "bitmap-cycle"}) {
                const QByteArray row = QByteArray::number(active) + '-' + fault;
                QTest::newRow(row.constData()) << active << QString::fromLatin1(fault);
            }
    }
    void texfatLimitedMetadata() {
        QFETCH(unsigned, active);
        QFETCH(QString, fault);
        TexfatFixture fixture(active);
        if (fault == "missing-bitmap")
            fixture.image[fixture.rootOffset(31)] = 1;
        if (fault == "duplicate-bitmap")
            fixture.image[fixture.rootOffset(31) + 1] = 0;
        if (fault == "active-root-cycle")
            fixture.fatCopy(active, 11, 2);
        if (fault == "active-root-unallocated")
            fixture.allocated(active, 2, false);
        if (fault == "bitmap-cycle") {
            const unsigned start = active ? 13 : 3;
            fixture.fatCopy(active, start, start);
        }
        QCOMPARE(open(fixture.image), S_OK);
        const UInt32 index = indexOf(fixture.fragmentedName);
        QVERIFY(index != UInt32(-1));
        CMyComPtr2<IArchiveExtractCallback, Extract> callback;
        callback.Create_if_Empty();
        QCOMPARE(archive_->Extract(&index, 1, 0, callback), S_OK);
        QCOMPARE(callback->result, Int32(NArchive::NExtract::NOperationResult::kOK));
        QCOMPARE(callback->output->bytes, QByteArray(512, 'A') + QByteArray(188, 'B'));
        NWindows::NCOM::CPropVariant free;
        QCOMPARE(archive_->GetArchiveProperty(kpidFreeSpace, &free), S_OK);
        QCOMPARE(free.vt, VARTYPE(VT_EMPTY));
        QCOMPARE(open(fixture_.image), S_OK);
    }
    void texfatDoesNotRecoverFromInactivePair() {
        TexfatFixture fixture(1);
        fixture.image[106] = char(1 | 2 | 4);
        // The inactive copy still has an acyclic chain, but its payload is stale.
        fixture.fatCopy(1, 5, 0xffffffff);
        QCOMPARE(open(fixture.image), S_OK);
        const UInt32 index = indexOf(fixture.fragmentedName);
        QVERIFY(index != UInt32(-1));
        CMyComPtr2<IArchiveExtractCallback, Extract> callback;
        callback.Create_if_Empty();
        QCOMPARE(archive_->Extract(&index, 1, 0, callback), S_OK);
        QCOMPARE(callback->result, Int32(NArchive::NExtract::NOperationResult::kDataError));
        QCOMPARE(callback->output->bytes, QByteArray(512, 'A'));
    }
    void texfatCancellation() {
        TexfatFixture fixture(1);
        CMyComPtr2<IArchiveOpenCallback, OpenProgress> progress;
        progress.Create_if_Empty();
        QCOMPARE(open(fixture.image, progress), E_ABORT);
        QCOMPARE(open(fixture.image), S_OK);
        const UInt32 index = indexOf(fixture.fragmentedName);
        CMyComPtr2<IArchiveExtractCallback, Extract> callback;
        callback.Create_if_Empty();
        callback->cancel = true;
        QCOMPARE(archive_->Extract(&index, 1, 0, callback), E_ABORT);
        callback->cancel = false;
        QCOMPARE(archive_->Extract(&index, 1, 0, callback), S_OK);
        QCOMPARE(callback->result, Int32(NArchive::NExtract::NOperationResult::kOK));
        QCOMPARE(callback->output->bytes, QByteArray(512, 'A') + QByteArray(188, 'B'));
    }
    void officialEngineListsTestsAndExtracts_data() {
        QTest::addColumn<int>("active");
        QTest::newRow("exfat") << -1;
        QTest::newRow("texfat-first") << 0;
        QTest::newRow("texfat-second") << 1;
        QTest::newRow("zero-size-directories") << 2;
    }
    void officialEngineListsTestsAndExtracts() {
        QFETCH(int, active);
        ExfatFixture fixture;
        if (active == 0 || active == 1)
            fixture = TexfatFixture(unsigned(active));
        if (active == 2) {
            fixture.fat(10, 11);
            fixture.fat(11, 0xffffffff);
            const int bitmap = fixture.clusterOffset(3) + 1;
            fixture.image[bitmap] = char(quint8(fixture.image[bitmap]) | 2);
            fixture.emptyDirectory(31, QStringLiteral("пустой 空"));
            fixture.emptyDirectory(34, QStringLiteral("empty2"));
        }
        const QString label = QStringLiteral("метка 空 😀");
        setVolumeLabel(fixture, active == 2 ? 37 : active >= 0 ? 34 : 31, label);
        QTemporaryDir runtime;
        QVERIFY(runtime.isValid());
        const QDir prepared(QString::fromUtf8(SEVENZIP_EXFAT_RUNTIME));
#ifdef Q_OS_WIN
        const QString engineName = "7z.exe", coreName = "7z.dll", suffix = ".dll";
#else
        const QString engineName = "7z", coreName = "7z.so", suffix = ".so";
#endif
        const QString engine = runtime.filePath(engineName);
        QVERIFY(QFile::copy(prepared.filePath(engineName), engine));
        QVERIFY(QFile::copy(prepared.filePath(coreName), runtime.filePath(coreName)));
        QVERIFY(QDir(runtime.path()).mkdir("Formats"));
        QVERIFY(QFile::copy(module_.fileName(), runtime.filePath("Formats/exfat" + suffix)));
        const QString image = runtime.filePath(QStringLiteral("образ 空.img"));
        QFile file(image);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(fixture.image), qint64(fixture.image.size()));
        file.close();
        const auto hash = QCryptographicHash::hash(read(image), QCryptographicHash::Sha256);
        auto run = [&](const QStringList &arguments) {
            QProcess process;
            process.start(engine, QStringList{"-sccUTF-8"} + arguments);
            if (!process.waitForFinished(10000))
                return QByteArray("PROCESS FAILED");
            const QByteArray output =
                process.readAllStandardOutput() + process.readAllStandardError();
            if (process.exitCode() != 0)
                return QByteArray("EXIT FAILED: ") + output;
            return output;
        };
        const auto listing = run({"l", "-slt", "--", image});
        QVERIFY2(listing.contains("Type = exFAT"), listing.constData());
        QVERIFY2(listing.contains(label.toUtf8()), listing.constData());
        QVERIFY2(listing.contains(fixture.fragmentedName.toUtf8()), listing.constData());
        const auto tested = run({"t", "--", image});
        QVERIFY2(tested.contains("Everything is Ok"), tested.constData());
        const QString output = runtime.filePath("output");
        const auto extracted = run({"x", "-o" + output, "--", image});
        QVERIFY2(extracted.contains("Everything is Ok"), extracted.constData());
        QCOMPARE(read(QDir(output).filePath(fixture.fragmentedName)),
                 QByteArray(512, 'A') + QByteArray(188, 'B'));
        QCOMPARE(read(QDir(output).filePath(fixture.directoryName + '/' + fixture.nestedName)),
                 QByteArray("tail") + QByteArray(1020, '\0'));
        if (active == 2) {
            for (const auto &name : {QStringLiteral("пустой 空"), QStringLiteral("empty2")}) {
                const QDir directory(QDir(output).filePath(name));
                QVERIFY(directory.exists());
                QVERIFY(directory.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
            }
        }
        QCOMPARE(QCryptographicHash::hash(read(image), QCryptographicHash::Sha256), hash);
    }
    void cleanupTestCase() { archive_.Release(); }
};
QTEST_GUILESS_MAIN(ExfatTests)
#include "exfat_tests.moc"
