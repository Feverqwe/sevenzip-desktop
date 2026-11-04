#include "Common/StringConvert.h"
#include "archive/agent/Agent.h"
#include "archive/backend/archivebackend.h"
#include "archive/resources/archivedevice.h"
#include "archive/resources/archivereadstream.h"
#include "archive/resources/qtarchivefoldermanager.h"
#include "archive/resources/qtarchiveproperties.h"
#include "archive/support/archive_copy_callback.h"
#include "devices/deviceaccess.h"
#include "formats/exfat/support/exfat_fixture.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>
#include <cstring>
#include <future>

namespace {
class CMemoryDevice final : public DeviceReadHandle {
  public:
    QByteArray Bytes;
    bool Disconnected = false;
    quint64 TotalRead = 0;
    explicit CMemoryDevice(QByteArray bytes) : Bytes(std::move(bytes)) {}
    qint32 readAt(quint64 offset, void *data, quint32 size, quint32 &read) override {
        read = 0;
        if (Disconnected)
            return E_FAIL;
        if (offset > quint64(Bytes.size()) || size > quint64(Bytes.size()) - offset ||
            offset % 512 || size % 512 || quintptr(data) % 512)
            return E_INVALIDARG;
        memcpy(data, Bytes.constData() + offset, size);
        TotalRead += size;
        read = size;
        return S_OK;
    }
};
class CObservedBackend final : public ArchiveBackend {
  public:
    unsigned Listings = 0;
    using ArchiveBackend::ArchiveBackend;
    void openFolder(const QString &path, const QString &type, const QString &password) override {
        ++Listings;
        ArchiveBackend::openFolder(path, type, password);
    }
};
using CCopyCallback = CArchiveCopyCallback;
} // namespace

class DeviceFolderTests final : public QObject {
    Q_OBJECT
    QString engine_;
    QString module_;

    QByteArray fixture() const {
        QProcess process;
        process.start(engine_,
                      {"x", "-so", "--", QStringLiteral(DEVICE_FIXTURE_DIR "/gpt.img.gz")});
        if (!process.waitForFinished(10000) || process.exitCode())
            return {};
        return process.readAllStandardOutput();
    }
    static HRESULT worker(const std::function<HRESULT()> &action) {
        auto result = std::async(std::launch::async, action);
        while (result.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
            QTest::qWait(1);
        return result.get();
    }
  private slots:
    void initTestCase() {
#ifndef SEVENZIP_CODEC_MODULE
        QSKIP("Official module unavailable; direct-device folder integration unverified");
#else
        module_ = QStringLiteral(SEVENZIP_CODEC_MODULE);
        engine_ = QFileInfo(module_).dir().filePath(
#ifdef Q_OS_WIN
            "7z.exe"
#else
            "7z"
#endif
        );
#endif
    }
    void sourceFolderCopiesSelectedUnicodeFile_data() {
        QTest::addColumn<QString>("mode");
        QTest::addColumn<bool>("allowIncompleteRead");
        QTest::addColumn<bool>("exfat");
        for (bool allowIncompleteRead : {false, true})
            for (const QString &mode :
                 {"success", "refresh", "cancel", "disconnect", "cancel-open", "disconnect-open"})
                QTest::newRow(qPrintable(mode + (allowIncompleteRead ? "-journal" : "")))
                    << mode << allowIncompleteRead << false;
        for (bool limited : {false, true})
            QTest::newRow(limited ? "exfat-dirty-refresh" : "exfat-refresh")
                << QString("refresh") << limited << true;
    }
    void sourceFolderCopiesSelectedUnicodeFile() {
        QFETCH(QString, mode);
        QFETCH(bool, allowIncompleteRead);
        QFETCH(bool, exfat);
        CObservedBackend backend(nullptr, engine_);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(module_));
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 15000);
        QVERIFY(ready[0][0].toBool());
        const ExfatFixture exfatFixture;
        QByteArray bytes = exfat ? exfatFixture.image : fixture();
        QVERIFY(!bytes.isEmpty());
        if (allowIncompleteRead && exfat)
            bytes[106] = char(2);
        else if (allowIncompleteRead) {
            const int superblock = 1024 * 1024 + 1024;
            bytes[superblock + 0x65] = char(quint8(bytes[superblock + 0x65]) & ~4u);
            bytes[superblock + 0x60] = char(quint8(bytes[superblock + 0x60]) | 4u);
        }
        auto handle = std::make_shared<CMemoryDevice>(bytes);
        ExternalDevice description;
        description.size = quint64(bytes.size());
        // The shared GPT fixture was generated with 512-byte LBAs.
        description.sectorSize = 512;
        CMyComPtr<IInStream> input, selected;
        QCOMPARE(CreateDeviceInput(description, handle, &input), S_OK);
        ArchiveDevice device(backend.codecCatalog());
        QCOMPARE(device.inspect(input, description.size, QStringLiteral("USB 空"), nullptr), S_OK);
        QCOMPARE(device.partitionInput(0, &selected), S_OK);
        if (allowIncompleteRead) {
            CMyComPtr<IInStream> withoutReplay;
            QCOMPARE(CreateDeviceInputAllowingIncompleteRead(selected, &withoutReplay), S_OK);
            selected = withoutReplay;
        }
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        CMyComPtr<IFolderFolder> root;
        auto *opening = new CCopyCallback;
        CMyComPtr<IFolderOperationsExtractCallback> progress = opening;
        const auto open = [&] {
            return manager->OpenFolderFile(selected, L"USB 空/data", L"", &root, progress);
        };
        if (mode == "cancel-open" || mode == "disconnect-open") {
            opening->Cancel = mode == "cancel-open";
            handle->Disconnected = mode == "disconnect-open";
            const HRESULT rejected = worker(open);
            QVERIFY(rejected != S_OK);
            if (opening->Cancel)
                QCOMPARE(rejected, E_ABORT);
            QVERIFY(!root);
            opening->Cancel = false;
            handle->Disconnected = false;
        }
        QCOMPARE(worker(open), S_OK);
        QVERIFY(root);
        auto *agent = dynamic_cast<CAgentFolder *>(root.Interface());
        QVERIFY(agent);
        QVERIFY(!ArchiveUpdateEnabled(agent->GetArchiveProperties()));
        NWindows::NCOM::CPropVariant type;
        QCOMPARE(root->GetFolderProperty(kpidType, &type), S_OK);
        QCOMPARE(type.vt, VARTYPE(VT_BSTR));
        QCOMPARE(QString::fromWCharArray(type.bstrVal),
                 exfat ? QString("7-Zip.exFAT") : QString("7-Zip.Ext"));
        CMyComPtr<IFolderFolder> folder;
        QCOMPARE(root->BindToFolder(L"папка 空", &folder), S_OK);
        QVERIFY(folder);
        QCOMPARE(folder->LoadItems(), S_OK);
        UInt32 count = 0;
        QCOMPARE(folder->GetNumberOfItems(&count), S_OK);
        QCOMPARE(count, UInt32(1));
        if (mode == "refresh") {
            auto *child = dynamic_cast<CAgentFolder *>(folder.Interface());
            QVERIFY(child);
            CMyComPtr<IFolderFolder> refreshed;
            QCOMPARE(worker([&] { return child->ReOpen(nullptr, &refreshed); }), S_OK);
            QVERIFY(folder->GetNumberOfItems(&count) != S_OK);
            folder = refreshed;
            QCOMPARE(folder->LoadItems(), S_OK);
            QCOMPARE(folder->GetNumberOfItems(&count), S_OK);
            QCOMPARE(count, UInt32(1));
            CMyComPtr<IInArchiveGetStream> streams;
            QCOMPARE(folder.QueryInterface(IID_IInArchiveGetStream, &streams), S_OK);
            CMyComPtr<ISequentialInStream> stream;
            QCOMPARE(streams->GetStream(0, &stream), S_OK);
            if (!exfat)
                QVERIFY(stream);
            // Some native readers expose only Extract. Their ordinary CopyTo
            // path below must remain available without a synthetic seek stream.
            if (stream) {
                CMyComPtr<IArchiveReadStream> requested;
                QCOMPARE(stream.QueryInterface(IID_IArchiveReadStream, &requested), S_OK);
                auto *probe = new CArchiveCopyCallback;
                CMyComPtr<IProgress> request =
                    static_cast<IFolderOperationsExtractCallback *>(probe);
                probe->Cancel = true;
                UInt64 position = 999;
                QCOMPARE(worker([&] {
                             return requested->SeekWithRequest(0, STREAM_SEEK_END, &position,
                                                               request);
                         }),
                         E_ABORT);
                QCOMPARE(position, UInt64(999));
                probe->Cancel = false;
                QCOMPARE(worker([&] {
                             return requested->SeekWithRequest(1, STREAM_SEEK_SET, &position,
                                                               request);
                         }),
                         S_OK);
                QByteArray range(2048, '\0');
                UInt32 read = 0;
                QCOMPARE(worker([&] {
                             // Native streams may return a partial read before EOF.
                             while (read < UInt32(range.size())) {
                                 UInt32 partial = 0;
                                 RINOK(requested->ReadWithRequest(range.data() + read,
                                                                  UInt32(range.size()) - read,
                                                                  &partial, request))
                                 if (!partial)
                                     break;
                                 read += partial;
                             }
                             return S_OK;
                         }),
                         S_OK);
                range.resize(int(read));
                const QByteArray expected = exfat ? QByteArray("tail") + QByteArray(1020, '\0')
                                                  : QByteArray("device ext4 payload\n");
                QCOMPARE(range, expected.mid(1));
            }
        }
        CMyComPtr<IFolderOperations> operations;
        QCOMPARE(folder.QueryInterface(IID_IFolderOperations, &operations), S_OK);
        QTemporaryDir destination;
        QVERIFY(destination.isValid());
        const auto prefix = (destination.path() + '/').toStdWString();
        auto *observer = new CCopyCallback;
        CMyComPtr<IFolderOperationsExtractCallback> callback = observer;
        observer->Cancel = mode == "cancel";
        handle->Disconnected = mode == "disconnect";
        const UInt32 index = 0;
        const auto result = worker(
            [&] { return operations->CopyTo(0, &index, 1, 0, 0, prefix.c_str(), callback); });
        if (mode == "cancel")
            QCOMPARE(result, E_ABORT);
        else if (mode == "disconnect")
            QVERIFY(result != S_OK || observer->Errors);
        else {
            QCOMPARE(result, S_OK);
            QCOMPARE(observer->Errors, 0u);
            QFile copied(destination.filePath(exfat ? exfatFixture.nestedName
                                                    : QStringLiteral("данные с пробелом.txt")));
            QVERIFY(copied.open(QIODevice::ReadOnly));
            QCOMPARE(copied.readAll(), exfat ? QByteArray("tail") + QByteArray(1020, '\0')
                                             : QByteArray("device ext4 payload\n"));
        }
        QCOMPARE(operations->Delete(&index, 1, nullptr), E_NOTIMPL);
        QCOMPARE(backend.Listings, 0u);
        QCOMPARE(handle->Bytes, bytes);
        if (!exfat)
            QVERIFY(handle->TotalRead < device.partitions()[0].size);
        std::weak_ptr<DeviceReadHandle> lease = handle;
        handle.reset();
        operations.Release();
        folder.Release();
        root.Release();
        selected.Release();
        input.Release();
        device = ArchiveDevice(backend.codecCatalog());
        QVERIFY(lease.expired());
    }
};
QTEST_GUILESS_MAIN(DeviceFolderTests)
#include "device_folder_tests.moc"
