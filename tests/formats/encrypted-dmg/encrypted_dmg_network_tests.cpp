#include "7zip/Common/FileStreams.h"
#include "archive/backend/archivebackend.h"
#include "archive/callbacks/ArchiveOpenCallback.h"
#include "archive/resources/archivenativeopen.h"
#include "archive/resources/qtarchivefileresource.h"
#include "mount/qtarchivemountsession.h"
#include "support/codec_runtime.h"
#include <QCryptographicHash>
#include <QFile>
#include <QProcess>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>
#include <future>
#include <thread>

namespace {
class LatentInput final : public IInStream, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_2(IInStream, ISequentialInStream)
    CMyComPtr<IInStream> input_;

  public:
    explicit LatentInput(IInStream *input) : input_(input) {}
    Z7_COM7F_IMF(Read(void *data, UInt32 size, UInt32 *processed)) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        return input_->Read(data, size, processed);
    }
    Z7_COM7F_IMF(Seek(Int64 offset, UInt32 origin, UInt64 *position)) override {
        return input_->Seek(offset, origin, position);
    }
};
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
bool Run(const QString &program, const QStringList &args, const QByteArray &input = {}) {
    QProcess process;
    process.start(program, args);
    if (!process.waitForStarted())
        return false;
    if (!input.isEmpty())
        process.write(input);
    process.closeWriteChannel();
    if (!process.waitForFinished(60000)) {
        process.kill();
        process.waitForFinished();
        return false;
    }
    if (process.exitCode())
        qWarning().noquote() << process.readAllStandardError();
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}
bool ProduceMedia(const QString &folder) {
    const auto ffmpeg = QStandardPaths::findExecutable("ffmpeg");
    return !ffmpeg.isEmpty() &&
           Run(ffmpeg, {"-f", "lavfi", "-i", "testsrc2=size=2048x1536:rate=1", "-vf",
                        "noise=alls=80:allf=t+u", "-frames:v", "24", "-q:v", "2", "-y",
                        folder + "/photo%02d.jpg"}) &&
           Run(ffmpeg,
               {"-f", "lavfi", "-i", "testsrc2=size=1280x720:rate=24", "-t", "60", "-an", "-c:v",
                "libx264", "-preset", "ultrafast", "-crf", "18", "-y", folder + "/video.mp4"});
}
QString CheckMedia(const QString &mount, const QString &scratch) {
    const auto vlc = qEnvironmentVariable("SEVENZIP_TEST_VLC");
    if (vlc.isEmpty())
        return {};
    QDir(scratch).mkdir("frames");
    QDir(scratch).mkdir("previews");
    QProcess previews, player;
    const auto cleanup = qScopeGuard([&] {
        for (auto *process : {&player, &previews}) {
            if (process->state() != QProcess::NotRunning) {
                process->kill();
                process->waitForFinished(5000);
            }
        }
    });
    QStringList thumbnails{"-t", "-s", "256", "-o", scratch + "/previews"};
    for (const auto &name : QDir(mount).entryList({"photo*.jpg"}, QDir::Files))
        thumbnails.append(QDir(mount).filePath(name));
    previews.start("/usr/bin/qlmanage", thumbnails);
    player.start(vlc, {"--intf=rc", "--rc-fake-tty", "--ignore-config", "--no-media-library",
                       "--no-metadata-network-access", "--vout=dummy", "--no-audio",
                       "--video-filter=scene", "--scene-ratio=24", "--scene-format=png",
                       "--scene-prefix=frame", "--scene-path=" + scratch + "/frames",
                       "--config=" + scratch + "/vlc.ini", mount + "/video.mp4"});
    if (!previews.waitForStarted() || !player.waitForStarted())
        return "Could not start native media clients";
    QElapsedTimer elapsed;
    elapsed.start();
    int seeks = 0, afterPreviews = -1;
    QSet<QByteArray> hashes;
    while (elapsed.elapsed() < 45000) {
        QTest::qWait(10);
        const QDir frames(scratch + "/frames");
        hashes.clear();
        for (const auto &name : frames.entryList({"*.png"}, QDir::Files)) {
            QFile frame(frames.filePath(name));
            if (frame.open(QIODevice::ReadOnly))
                hashes.insert(
                    QCryptographicHash::hash(frame.readAll(), QCryptographicHash::Sha256));
        }
        if (!hashes.isEmpty() && seeks < 8 && elapsed.elapsed() > (seeks + 1) * 400) {
            player.write("seek +5\n");
            ++seeks;
        }
        if (previews.state() == QProcess::NotRunning && afterPreviews < 0) {
            if (previews.exitCode() != 0 ||
                QDir(scratch + "/previews").entryList({"*.png"}, QDir::Files).size() != 24)
                return "Preview generation failed";
            afterPreviews = hashes.size();
            player.write("seek 2\n");
        }
        if (afterPreviews >= 0 && seeks == 8 && hashes.size() >= afterPreviews + 3)
            return {};
    }
    return QString("Video did not advance after previews: %1 distinct frames, baseline %2, "
                   "%3 seeks, previews running %4")
        .arg(hashes.size())
        .arg(afterPreviews)
        .arg(seeks)
        .arg(previews.state() != QProcess::NotRunning);
}
} // namespace

class EncryptedDmgNetworkTests final : public QObject {
    Q_OBJECT
  private slots:
    void randomReadsOverTwoSmbMounts() {
#ifndef Q_OS_MACOS
        QSKIP("Native macOS network-source diagnostic.");
#else
        if (!qEnvironmentVariableIsSet("SEVENZIP_NATIVE_NETWORK_DMG_TEST"))
            QSKIP("Opt-in large synthetic image over two native SMB mounts.");
        QTemporaryDir fixture, runtime;
        QVERIFY(fixture.isValid() && runtime.isValid());
        QVERIFY(
            CopyCodecRuntime(QDir(QString::fromUtf8(SEVENZIP_DMG_RUNTIME)), QDir(runtime.path())));
        QVERIFY(QDir(runtime.path()).mkdir("Formats"));
        for (const auto &path : {qEnvironmentVariable("SEVENZIP_TEST_DMG_MODULE",
                                                      QString::fromUtf8(SEVENZIP_DMG_MODULE)),
                                 QString::fromUtf8(SEVENZIP_DMG_EXFAT_MODULE)})
            QVERIFY(QFile::copy(path, runtime.filePath("Formats/" + QFileInfo(path).fileName())));
        const QString module = runtime.filePath("7z.so"), engine = runtime.filePath("7z");
        ArchiveBackend backend(nullptr, engine);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(module));
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 15000);
        QVERIFY2(ready[0][0].toBool(), qPrintable(ready[0][1].toString()));
        QByteArray unit(1024 * 1024, '\0');
        for (int i = 0; i < unit.size(); ++i)
            unit[i] = char((i * 17 + (i >> 12)) & 255);
        QVERIFY(QDir(fixture.path()).mkdir("files"));
        QFile payload(fixture.filePath("files/large.bin"));
        QVERIFY(payload.open(QIODevice::WriteOnly));
        QByteArray block = unit;
        for (int n = 0; n < 128; ++n) {
            block[0] = char(n); // Distinguish wrong MiB ranges as well as shifted bytes.
            QCOMPARE(payload.write(block), block.size());
        }
        payload.close();
        if (qEnvironmentVariableIsSet("SEVENZIP_TEST_VLC"))
            QVERIFY(ProduceMedia(fixture.filePath("files")));
        const auto image = fixture.filePath("network.dmg");
        QVERIFY(Run("/usr/bin/hdiutil",
                    {"create", "-srcfolder", fixture.filePath("files"), "-fs", "ExFAT", "-format",
                     "UDRO", "-size", "512m", "-encryption", "AES-256", "-stdinpass", image},
                    QByteArray("fixture-password\0", 17)));
        const auto tar = fixture.filePath("source.tar");
        QVERIFY(Run(engine, {"a", "-ttar", "-y", tar, image}));
        QtArchiveMountSession outer("Synthetic SMB image source"), inner("Synthetic mounted DMG");
        const auto cleanup = qScopeGuard([&] {
            for (auto *session : {&inner, &outer}) {
                session->unmount(true);
                for (int n = 0;
                     n < 1000 && session->status() != QtArchiveMountSession::Status::Stopped; ++n)
                    QTest::qWait(10);
            }
        });
        int extractions = 0;
        const auto start = [&](QtArchiveMountSession &session, const QString &path,
                               const QString &type, IOpenCallbackUI *password) {
            std::shared_ptr<ArchiveNativeOpen> reader;
            const HRESULT result = Worker([&] {
                if (type == "tar" && qEnvironmentVariableIsSet("SEVENZIP_TEST_VLC")) {
                    CMyComPtr2_Create<IInStream, CInFileStream> file;
                    if (!file->Open(QFile::encodeName(path).constData()))
                        return E_FAIL;
                    CMyComPtr<IInStream> delayed = new LatentInput(file);
                    CObjectVector<COpenType> types;
                    CIntVector excluded;
                    COpenOptions options;
                    options.props = nullptr;
                    options.types = &types;
                    options.excludedFormats = &excluded;
                    options.stream = delayed;
                    return backend.openNativeArchive(path, type, password, reader, &options);
                }
                return backend.openNativeArchive(path, type, password, reader);
            });
            if (result != S_OK)
                return result;
            auto *owner = new QtArchiveFileResource(path);
            CMyComPtr<IUnknown> retained = owner;
            owner->AttachNativeOpen(reader);
            owner->UseNativeMetadata();
            connect(&session, &QtArchiveMountSession::extractionRequired, this,
                    [&](const QString &, quint64, const ArchiveMountDecisionPtr &decision) {
                        ++extractions;
                        decision->answer(false);
                    });
            session.start(retained, 0, {},
                          QDir(QCoreApplication::applicationDirPath()).filePath("SevenzipSmb"));
            return S_OK;
        };
        QCOMPARE(start(outer, tar, "tar", nullptr), S_OK);
        QTRY_VERIFY_WITH_TIMEOUT(outer.status() == QtArchiveMountSession::Status::Mounted ||
                                     outer.status() == QtArchiveMountSession::Status::Stopped,
                                 15000);
        QVERIFY2(outer.status() == QtArchiveMountSession::Status::Mounted,
                 qPrintable(outer.error()));
        QVERIFY2(Worker([&] {
                     QFile network(QDir(outer.path()).filePath("network.dmg")), local(image);
                     if (!network.open(QIODevice::ReadOnly) || !local.open(QIODevice::ReadOnly))
                         return false;
                     for (int n = 0; n < 4; ++n) {
                         const qint64 offset = n * 5 * 1024 * 1024;
                         if (!network.seek(offset) || !local.seek(offset) ||
                             network.read(1024 * 1024) != local.read(1024 * 1024))
                             return false;
                     }
                     return true;
                 }),
                 "SMB image source bytes differ");
        Password password;
        QCOMPARE(start(inner, QDir(outer.path()).filePath("network.dmg"), {}, &password), S_OK);
        QTRY_VERIFY_WITH_TIMEOUT(inner.status() == QtArchiveMountSession::Status::Mounted ||
                                     inner.status() == QtArchiveMountSession::Status::Stopped,
                                 15000);
        QVERIFY2(inner.status() == QtArchiveMountSession::Status::Mounted,
                 qPrintable(inner.error()));
        const auto mediaError = CheckMedia(inner.path(), fixture.path());
        QVERIFY2(mediaError.isEmpty(), qPrintable(mediaError));
        QElapsedTimer elapsed;
        elapsed.start();
        auto reading = std::async(std::launch::async, [&, path = inner.path()] {
            QFile file(QDir(path).filePath("large.bin"));
            if (!file.open(QIODevice::ReadOnly))
                return file.errorString();
            quint32 random = 0x12345678;
            for (int n = 0; n < 64; ++n) {
                random = random * 1664525 + 1013904223;
                const qint64 offset =
                    n < 20 ? qint64(n) * 5 * 1024 * 1024 : qint64(random % (127 * 1024 * 1024));
                if (!file.seek(offset))
                    return file.errorString();
                const auto bytes = file.read(unit.size());
                if (bytes.size() != unit.size())
                    return QString("Short read at %1: %2 bytes").arg(offset).arg(bytes.size());
                for (int i = 0; i < bytes.size(); ++i) {
                    const auto position = offset + i;
                    const char expected = position % unit.size() == 0
                                              ? char(position / unit.size())
                                              : unit[position % unit.size()];
                    if (bytes[i] != expected)
                        return QString("Incorrect bytes at %1").arg(offset + i);
                }
            }
            return QString();
        });
        bool timedOut = false;
        while (reading.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
            QTest::qWait(1);
            if (!timedOut && elapsed.elapsed() > 60000) {
                timedOut = true;
                inner.unmount(true);
                outer.unmount(true);
            }
        }
        const auto error = reading.get();
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(!timedOut);
        QCOMPARE(extractions, 0);
        qInfo() << "64 random MiB ranges over nested SMB took" << elapsed.elapsed() << "ms";
#endif
    }
};
QTEST_GUILESS_MAIN(EncryptedDmgNetworkTests)
#include "encrypted_dmg_network_tests.moc"
