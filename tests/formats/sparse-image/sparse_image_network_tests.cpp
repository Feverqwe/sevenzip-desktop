#include "7zip/Common/StreamUtils.h"
#include "Windows/PropVariant.h"
#include "archive/backend/archivebackend.h"
#include "archive/callbacks/ArchiveOpenCallback.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/opening/archivedirectoryaccess.h"
#include "archive/resources/archivenativeopen.h"
#include "archive/resources/archivereadonlyview.h"
#include "archive/resources/qtarchivefileresource.h"
#include "mount/qtarchivemountsession.h"
#include "sparsefixtures.h"
#include "support/codec_runtime.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QProcess>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>
#include <copyfile.h>
#include <future>
#include <sys/stat.h>
#include <unistd.h>

namespace {
template <typename Work> auto Worker(Work work) {
    auto pending = std::async(std::launch::async, std::move(work));
    while (pending.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
        QTest::qWait(1);
    return pending.get();
}
class Password final : public IOpenCallbackUI {
    HRESULT Open_CheckBreak() override { return S_OK; }
    HRESULT Open_SetTotal(const UInt64 *, const UInt64 *) override { return S_OK; }
    HRESULT Open_SetCompleted(const UInt64 *, const UInt64 *) override { return S_OK; }
    HRESULT Open_Finished() override { return S_OK; }
    HRESULT Open_CryptoGetTextPassword(BSTR *value) override {
        return StringToBstr(L"fixture-password", value);
    }
};
} // namespace

class SparseImageNetworkTests final : public QObject {
    Q_OBJECT
  private slots:
    void bundleOnSmb() {
        QTemporaryDir scratch, runtime;
        QVERIFY(scratch.isValid() && runtime.isValid());
        QVERIFY(
            CopyCodecRuntime(QDir(QString::fromUtf8(SEVENZIP_DMG_RUNTIME)), QDir(runtime.path())));
        QVERIFY(QDir(runtime.path()).mkdir("Formats"));
        for (const auto &path :
             {QString::fromUtf8(SEVENZIP_DMG_MODULE), QString::fromUtf8(SEVENZIP_DMG_HFS_MODULE)})
            QVERIFY(QFile::copy(path, runtime.filePath("Formats/" + QFileInfo(path).fileName())));
        const auto engine = runtime.filePath("7z");
        ArchiveBackend backend(nullptr, engine);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(runtime.filePath("7z.so")));
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 15000);
        QVERIFY2(ready[0][0].toBool(), qPrintable(ready[0][1].toString()));
        auto fixtures = SparseFixtureManifest()["fixtures"].toArray();
        fixtures.append(
            QJsonObject{{"name", "apfs-directories"}, {"type", "SPARSEBUNDLE"}, {"bits", 256}});
        for (const auto &value : fixtures) {
            const auto fixture = value.toObject();
            if (fixture["type"].toString() != "SPARSEBUNDLE" || fixture["bits"].toInt() == 128)
                continue;
            QTemporaryDir source;
            const bool apfs = fixture["name"].toString() == "apfs-directories";
            const auto local =
                apfs ? DirectorySparseBundle(source) : UnpackSparseFixture(fixture, source);
            QVERIFY(!local.isEmpty());
            const auto tar = scratch.filePath("source.tar");
            QFile::remove(tar);
            QProcess pack;
            pack.start(engine, {"a", "-ttar", "-y", tar, local});
            QVERIFY(pack.waitForFinished(30000));
            QCOMPARE(pack.exitCode(), 0);
            std::shared_ptr<ArchiveNativeOpen> outerReader;
            QCOMPARE(backend.openNativeArchive(tar, "tar", nullptr, outerReader), S_OK);
            auto *owner = new QtArchiveFileResource(tar);
            CMyComPtr<IUnknown> retained = owner;
            owner->AttachNativeOpen(outerReader);
            owner->UseNativeMetadata();
            QtArchiveMountSession mount("Synthetic SMB sparsebundle source");
            const auto cleanup = qScopeGuard([&] {
                mount.unmount(true);
                for (int n = 0;
                     n < 1000 && mount.status() != QtArchiveMountSession::Status::Stopped; ++n)
                    QTest::qWait(10);
            });
            int extractions = 0;
            connect(&mount, &QtArchiveMountSession::extractionRequired, this,
                    [&](const QString &, quint64, const ArchiveMountDecisionPtr &decision) {
                        ++extractions;
                        decision->answer(false);
                    });
            mount.start(retained, 0, {},
                        QDir(QCoreApplication::applicationDirPath()).filePath("SevenzipSmb"));
            QTRY_VERIFY_WITH_TIMEOUT(mount.status() == QtArchiveMountSession::Status::Mounted ||
                                         mount.status() == QtArchiveMountSession::Status::Stopped,
                                     15000);
            QVERIFY2(mount.status() == QtArchiveMountSession::Status::Mounted,
                     qPrintable(mount.error()));
            const auto network = QDir(mount.path()).filePath(QFileInfo(local).fileName());
            // Each opening owns its cursor, password callback and bounded band
            // handles. Pump the main loop while the native SMB server serves I/O.
            const auto check = [&]() -> QString {
                Password password;
                std::shared_ptr<ArchiveNativeOpen> reader;
                COpenOptions options;
                CObjectVector<COpenType> types;
                CIntVector excluded;
                options.props = nullptr;
                options.types = &types;
                options.excludedFormats = &excluded;
                options.PreferDirectoryRead = apfs;
                if (backend.openNativeArchive(network, {}, &password, reader, &options) != S_OK)
                    return "Could not open sparsebundle from SMB";
                if (apfs) {
                    UInt32 count = 0;
                    if (!reader->resolvedOpenType().contains("APFS") ||
                        reader->arc().Archive->GetNumberOfItems(&count) != S_OK || count >= 100)
                        return "SMB APFS opening enumerated the whole tree";
                }
                CMyComPtr<IInArchiveGetStream> access;
                reader->archiveLink().Arcs[0].Archive.QueryInterface(IID_IInArchiveGetStream,
                                                                     &access);
                if (!access)
                    return "Missing image stream";
                CMyComPtr<ISequentialInStream> sequential;
                if (access->GetStream(0, &sequential) != S_OK)
                    return "Could not get image stream";
                CMyComPtr<IInStream> stream;
                sequential.QueryInterface(IID_IInStream, &stream);
                for (int round = 0; round < 3; ++round)
                    for (const auto &rangeValue : fixture["ranges"].toArray()) {
                        const auto range = rangeValue.toObject();
                        QByteArray bytes(range["size"].toInt(), '\0');
                        if (!stream ||
                            stream->Seek(range["offset"].toInteger(), STREAM_SEEK_SET, nullptr) !=
                                S_OK ||
                            ReadStream_FALSE(stream, bytes.data(), size_t(bytes.size())) != S_OK ||
                            QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() !=
                                range["sha256"].toString().toLatin1())
                            return "Incorrect random image range over SMB";
                    }
                auto *resource = new QtArchiveFileResource(network);
                CMyComPtr<IUnknown> held = resource;
                resource->AttachNativeOpen(reader);
                resource->UseNativeMetadata();
                std::shared_ptr<ArchiveReadOnlyView> view;
                QString error;
                if (ArchiveBackend::createReadOnlyView(held, 0, {}, view, error) != S_OK)
                    return "Could not create SMB image view: " + error;
                auto entries = SparseFixtureManifest()["entries"].toObject();
                if (apfs) {
                    entries = {};
                    entries["root.txt"] = QString::fromLatin1(
                        QCryptographicHash::hash("APFS root payload\n", QCryptographicHash::Sha256)
                            .toHex());
                    entries[QString::fromUtf8("данные 空.txt")] =
                        QString::fromLatin1(QCryptographicHash::hash("Nested APFS payload\n",
                                                                     QCryptographicHash::Sha256)
                                                .toHex());
                }
                for (auto expected = entries.begin(); expected != entries.end(); ++expected) {
                    const auto found = std::find_if(
                        view->entries().begin(), view->entries().end(),
                        [&](const auto &entry) { return entry.Name == expected.key(); });
                    if (found == view->entries().end())
                        return "Missing file on SMB image";
                    QByteArray bytes;
                    for (quint64 offset = 0; offset < found->Size; offset += 1u << 20) {
                        const auto data = view->read(found->Id, offset, 1u << 20);
                        if (data.Result != S_OK)
                            return "Could not read file over SMB: " + data.Error;
                        bytes += data.Data;
                    }
                    if (QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() !=
                        expected.value().toString().toLatin1())
                        return "Incorrect extracted file bytes over SMB";
                }
                return {};
            };
            const auto error = Worker([&] {
                auto second = std::async(std::launch::async, check);
                const auto first = check();
                const auto other = second.get();
                return first.isEmpty() ? other : first;
            });
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QCOMPARE(extractions, 0);
            if (apfs) {
                std::shared_ptr<ArchiveNativeOpen> lazy;
                QCOMPARE(Worker([&] {
                             Password password;
                             COpenOptions options;
                             CObjectVector<COpenType> types;
                             CIntVector excluded;
                             options.props = nullptr;
                             options.types = &types;
                             options.excludedFormats = &excluded;
                             options.PreferDirectoryRead = true;
                             return backend.openNativeArchive(network, {}, &password, lazy,
                                                              &options);
                         }),
                         S_OK);
                auto *resource = new QtArchiveFileResource(network);
                CMyComPtr<IUnknown> retainedApfs = resource;
                resource->AttachNativeOpen(lazy);
                resource->UseNativeMetadata();
                QtArchiveMountSession exported("Lazy synthetic APFS over SMB");
                QSignalSpy issues(&exported, &QtArchiveMountSession::entryIssue);
                const auto closeExport = qScopeGuard([&] {
                    exported.unmount(true);
                    for (int n = 0;
                         n < 1000 && exported.status() != QtArchiveMountSession::Status::Stopped;
                         ++n)
                        QTest::qWait(10);
                });
                int confirmations = 0;
                connect(&exported, &QtArchiveMountSession::extractionRequired, this,
                        [&](const QString &, quint64, const ArchiveMountDecisionPtr &decision) {
                            ++confirmations;
                            decision->answer(false);
                        });
                exported.start(
                    retainedApfs, 16 * 1024 * 1024, {},
                    QDir(QCoreApplication::applicationDirPath()).filePath("SevenzipSmb"));
                QTRY_VERIFY_WITH_TIMEOUT(
                    exported.status() == QtArchiveMountSession::Status::Mounted ||
                        exported.status() == QtArchiveMountSession::Status::Stopped,
                    15000);
                QVERIFY2(exported.status() == QtArchiveMountSession::Status::Mounted,
                         qPrintable(exported.error()));
                const auto deepPath =
                    QDir(exported.path())
                        .filePath(QString::fromUtf8("directory-00/nested/данные 空.txt"));
                const auto payload = Worker([&] {
                    QFile file(deepPath);
                    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
                });
                QCOMPARE(payload, QByteArray("Nested APFS payload\n"));
                const auto linkPath = QDir(exported.path()).filePath("directory-02/root-link");
                const auto missingLink = Worker([&] {
                    const auto encoded = QFile::encodeName(linkPath);
                    struct stat metadata{};
                    return ::lstat(encoded, &metadata) == -1 && errno == ENOENT;
                });
                QVERIFY(missingLink);
                QVERIFY(std::any_of(issues.cbegin(), issues.cend(), [](const auto &issue) {
                    return issue.at(1).toUInt() == quint32(S_FALSE) &&
                           issue.at(2).toString().contains("directory-02/root-link");
                }));
                QTemporaryDir copied;
                QVERIFY(copied.isValid());
                const auto sourceFolder = QDir(exported.path()).filePath("directory-02");
                const auto destination = copied.filePath("directory-02");
                const auto copyResult = Worker([&] {
                    const int result = ::copyfile(
                        QFile::encodeName(sourceFolder), QFile::encodeName(destination), nullptr,
                        COPYFILE_ALL | COPYFILE_RECURSIVE | COPYFILE_NOFOLLOW_SRC);
                    return std::pair(result, errno);
                });
                QVERIFY2(copyResult.first == 0, strerror(copyResult.second));
                QVERIFY(!QFileInfo::exists(QDir(destination).filePath("root-link")));
                QVERIFY(Worker([&] {
                    const auto names = QDir(sourceFolder).entryList(QDir::Files);
                    if (names.isEmpty() || names != QDir(destination).entryList(QDir::Files))
                        return false;
                    for (const auto &name : names) {
                        QFile source(QDir(sourceFolder).filePath(name));
                        QFile target(QDir(destination).filePath(name));
                        if (!source.open(QIODevice::ReadOnly) ||
                            !target.open(QIODevice::ReadOnly) ||
                            source.readAll() != target.readAll())
                            return false;
                    }
                    return true;
                }));
                struct stat metadata{};
                QVERIFY(::lstat(QFile::encodeName(QDir(destination).filePath("file-000.txt")),
                                &metadata) == 0);
                QCOMPARE(metadata.st_flags & UF_IMMUTABLE, 0u);
                const auto siblingUnread = Worker([&] {
                    ArchiveNativeReadScope scope(lazy, nullptr);
                    CMyComPtr<IArchiveDirectoryAccess> directories;
                    lazy->arc().Archive.QueryInterface(IID_IArchiveDirectoryAccess,
                                                       (void **)&directories);
                    UInt32 count = 0;
                    lazy->arc().Archive->GetNumberOfItems(&count);
                    for (UInt32 i = 0; i < count; ++i) {
                        NWindows::NCOM::CPropVariant path;
                        if (lazy->arc().Archive->GetProperty(i, kpidPath, &path) == S_OK &&
                            path.vt == VT_BSTR &&
                            QString::fromWCharArray(path.bstrVal) == "directory-11") {
                            BoolInt loaded = true;
                            return directories &&
                                   directories->IsDirectoryLoaded(i, false, &loaded) == S_OK &&
                                   !loaded;
                        }
                    }
                    return false;
                });
                QVERIFY(siblingUnread);
                QCOMPARE(confirmations, 0);
            }
        }
    }
};
QTEST_MAIN(SparseImageNetworkTests)
#include "sparse_image_network_tests.moc"
