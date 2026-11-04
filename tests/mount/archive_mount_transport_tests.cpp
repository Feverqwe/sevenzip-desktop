#include "archive/resources/archivenativeopen.h"
#include "archive/resources/qtarchivefileresource.h"
#include "archive/support/native_archive_fixture.h"
#include "formats/cpio/support/dot_path_cpio_fixture.h"
#include "mount/qtarchivemountsource.h"
#include "mount/qtsmbmountplatform.h"
#include "support/widget_dialogs.h"
#include "ui/support/layout_inspection.h"
#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
#include "mount/qtarchivemountmanager.h"
#include "mount/qtarchivemountsession.h"
#include "mount/qtmountextractionprompt.h"
#include <csignal>
#include <unistd.h>
#endif
#include <QAction>
#include <QClipboard>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QTranslator>
#include <QTreeWidget>
#include <QtTest>
#include <fcntl.h>
#include <future>
// External headers are order dependent.
// clang-format off
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
// clang-format on

namespace {
template <typename Work> auto ClientWork(Work work) {
    auto pending = std::async(std::launch::async, std::move(work));
    while (pending.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
        QTest::qWait(1);
    return pending.get();
}
struct Client {
    smb2_context *Context = smb2_init_context();
    Client() {
        smb2_set_timeout(Context, 3);
        smb2_set_version(Context, SMB2_VERSION_0210);
        smb2_set_sign(Context, 1);
        smb2_set_user(Context, "sztransport");
        smb2_set_password(Context, "SyntheticTransport427");
    }
    ~Client() { smb2_destroy_context(Context); }
    int connect(quint16 port) {
        const auto address = QByteArray("127.0.0.1:") + QByteArray::number(port);
        return smb2_connect_share(Context, address.constData(), "Sevenzip", "sztransport");
    }
};
class Host final : public QObject {
  public:
    QTemporaryDir Socket{QStringLiteral("/tmp/7zs-XXXXXX")};
    QThread Worker;
    QProcess Child;
    QtArchiveMountSource *Source = nullptr;
    quint16 Port = 0;
    QString Error;
    bool Allow = false;
    bool Defer = false;
    ArchiveMountDecisionPtr Pending;
    int Confirmations = 0;
    quint64 CacheQuota = 4 * 1024 * 1024;
    QStringList ReadIssues;
    ~Host() override {
        if (Source)
            Source->requestCancel();
        if (Child.state() != QProcess::NotRunning) {
            Child.terminate();
            if (!Child.waitForFinished(3000)) {
                Child.kill();
                Child.waitForFinished(3000);
            }
        }
        if (Source && Worker.isRunning())
            QMetaObject::invokeMethod(Source, &QtArchiveMountSource::close,
                                      Qt::BlockingQueuedConnection);
        Worker.quit();
        Worker.wait();
    }
    bool start(const QString &path, const QString &format) {
        if (!Socket.isValid())
            return false;
        const auto catalog = NativeArchiveFixture::Catalog();
        std::shared_ptr<ArchiveNativeOpen> reader;
        if (ArchiveBackend::openNativeArchive(catalog, path, format, nullptr, reader, nullptr) !=
                S_OK ||
            !reader)
            return false;
        auto *resource = new QtArchiveFileResource(path);
        CMyComPtr<IUnknown> retained = resource;
        resource->AttachNativeOpen(reader);
        resource->UseNativeMetadata();
        const QUrl endpoint(QStringLiteral("local:") + Socket.filePath("source"));
        Source = new QtArchiveMountSource(resource, CacheQuota, {}, "sztransport",
                                          "SyntheticTransport427", endpoint);
        Source->moveToThread(&Worker);
        QObject::connect(&Worker, &QThread::finished, Source, &QObject::deleteLater);
        QObject::connect(&Worker, &QThread::started, Source, &QtArchiveMountSource::initialize);
        QObject::connect(Source, &QtArchiveMountSource::failure, this,
                         [this](const QString &error) { Error = error; });
        QObject::connect(Source, &QtArchiveMountSource::serverListening, this,
                         [this](quint16 port) { Port = port; });
        QObject::connect(
            Source, &QtArchiveMountSource::entryIssue, this,
            [this](quint64, quint32, const QString &error, qint32) { ReadIssues.append(error); });
        QObject::connect(Source, &QtArchiveMountSource::extractionRequired, this,
                         [this](const QString &, quint64, const ArchiveMountDecisionPtr &decision) {
                             ++Confirmations;
                             if (Defer)
                                 Pending = decision;
                             else
                                 decision->answer(Allow);
                         });
        QObject::connect(Source, &QtArchiveMountSource::prepared, this, [this, endpoint] {
            Child.start(QStringLiteral(SEVENZIP_SMB_SERVER), {"--source", endpoint.toString()});
        });
        Worker.start();
        return true;
    }
};
QByteArray Payload() {
    QByteArray bytes(180000, '\0');
    for (qsizetype i = 0; i < bytes.size(); ++i)
        bytes[i] = char(i * 71 + i / 127);
    return bytes;
}
} // namespace

class ArchiveMountTransportTests final : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        const auto catalog = NativeArchiveFixture::Catalog();
        QVERIFY2(catalog->result() == S_OK, qPrintable(catalog->error()));
    }
    void nativeReaderAcrossChild_data() {
        QTest::addColumn<QString>("format");
        QTest::newRow("seekable tar") << QStringLiteral("tar");
        QTest::newRow("zip with explicit consent") << QStringLiteral("zip");
    }
    void nativeReaderAcrossChild() {
        QFETCH(QString, format);
        QTemporaryDir fixture;
        const auto payload = Payload();
        const QString name = QStringLiteral("folder/данные 空 with spaces.bin");
        const QString path = fixture.filePath("source." + format);
        QVERIFY(NativeArchiveFixture::Write(path, {{name, payload}}, format));
        Host host;
        host.Allow = true;
        QVERIFY(host.start(path, format));
        QTRY_VERIFY_WITH_TIMEOUT(host.Port || !host.Error.isEmpty(), 15000);
        QVERIFY2(host.Port, qPrintable(host.Error));
        Client client;
        QCOMPARE(ClientWork([&] { return client.connect(host.Port); }), 0);
        const auto encoded = name.toUtf8();
        smb2fh *file =
            ClientWork([&] { return smb2_open(client.Context, encoded.constData(), O_RDONLY); });
        QVERIFY2(file, smb2_get_error(client.Context));
        QByteArray bytes(4096, '\0');
        for (quint64 offset : {quint64(100003), quint64(17), quint64(175000)}) {
            const int count = ClientWork([&] {
                return smb2_pread(client.Context, file, reinterpret_cast<uint8_t *>(bytes.data()),
                                  4096, offset);
            });
            QCOMPARE(count, 4096);
            QCOMPARE(bytes, payload.mid(qsizetype(offset), 4096));
        }
        QCOMPARE(host.Confirmations, format == "zip" ? 1 : 0);
        QVERIFY(ClientWork([&] { return smb2_unlink(client.Context, encoded.constData()); }) < 0);
        QVERIFY(ClientWork([&] {
                    return smb2_rename(client.Context, encoded.constData(), "renamed.bin");
                }) < 0);
        QVERIFY(ClientWork([&] { return smb2_mkdir(client.Context, "new directory"); }) < 0);
        auto *write =
            ClientWork([&] { return smb2_open(client.Context, encoded.constData(), O_RDWR); });
        QVERIFY(!write);
        auto *created =
            ClientWork([&] { return smb2_open(client.Context, "new.bin", O_WRONLY | O_CREAT); });
        QVERIFY(!created);
        QCOMPARE(ClientWork([&] {
                     return smb2_pread(client.Context, file,
                                       reinterpret_cast<uint8_t *>(bytes.data()), 4096, 20000);
                 }),
                 4096);
        QCOMPARE(bytes, payload.mid(20000, 4096));
        QCOMPARE(ClientWork([&] { return smb2_close(client.Context, file); }), 0);
        QVERIFY(host.Error.isEmpty());
    }
    void wrongSessionPasswordIsRejected() {
        QTemporaryDir fixture;
        const QString path = fixture.filePath("source.tar");
        QVERIFY(NativeArchiveFixture::Write(path, {{"data.bin", "secret fixture"}}, "tar"));
        Host host;
        QVERIFY(host.start(path, "tar"));
        QTRY_VERIFY_WITH_TIMEOUT(host.Port || !host.Error.isEmpty(), 15000);
        QVERIFY2(host.Port, qPrintable(host.Error));
        Client client;
        smb2_set_password(client.Context, "wrong fixture password");
        QVERIFY(ClientWork([&] { return client.connect(host.Port); }) < 0);
        QCOMPARE(host.Confirmations, 0);
    }
    void cpioCurrentDirectoryAcrossChild() {
        QTemporaryDir fixture;
        const QString path = fixture.filePath("Payload.cpio");
        QFile archive(path);
        QVERIFY(archive.open(QIODevice::WriteOnly));
        const auto payload = DotPathCpioFixture();
        QCOMPARE(archive.write(payload), qint64(payload.size()));
        archive.close();
        Host host;
        QVERIFY(host.start(path, "cpio"));
        QTRY_VERIFY_WITH_TIMEOUT(host.Port || !host.Error.isEmpty(), 15000);
        QVERIFY2(host.Port, qPrintable(host.Error));
        Client client;
        QCOMPARE(ClientWork([&] { return client.connect(host.Port); }), 0);
        const auto name = QStringLiteral("ProxyBridge.app/имя 空.txt").toUtf8();
        auto *file =
            ClientWork([&] { return smb2_open(client.Context, name.constData(), O_RDONLY); });
        QVERIFY2(file, smb2_get_error(client.Context));
        QByteArray bytes(64, '\0');
        const int count = ClientWork([&] {
            return smb2_pread(client.Context, file, reinterpret_cast<uint8_t *>(bytes.data()),
                              bytes.size(), 0);
        });
        QCOMPARE(count, 13);
        QCOMPARE(bytes.left(count), QByteArray("payload bytes"));
        QCOMPARE(host.Confirmations, 0);
        QCOMPARE(ClientWork([&] { return smb2_close(client.Context, file); }), 0);
    }
    void cancellingWhileWaitingForExtractionConsent() {
        QTemporaryDir fixture;
        const QString path = fixture.filePath("source.zip");
        QVERIFY(NativeArchiveFixture::Write(path, {{"data.bin", Payload()}}, "zip"));
        Host host;
        host.Defer = true;
        QVERIFY(host.start(path, "zip"));
        QTRY_VERIFY_WITH_TIMEOUT(host.Port || !host.Error.isEmpty(), 15000);
        QVERIFY2(host.Port, qPrintable(host.Error));
        Client client;
        QCOMPARE(ClientWork([&] { return client.connect(host.Port); }), 0);
        auto *file = ClientWork([&] { return smb2_open(client.Context, "data.bin", O_RDONLY); });
        QVERIFY(file);
        QByteArray bytes(1024, '\0');
        auto read = std::async(std::launch::async, [&] {
            return smb2_pread(client.Context, file, reinterpret_cast<uint8_t *>(bytes.data()),
                              bytes.size(), 1000);
        });
        QElapsedTimer timer;
        timer.start();
        while (!host.Pending && timer.elapsed() < 5000)
            QTest::qWait(1);
        const bool requested = bool(host.Pending);
        Client metadataClient;
        const int metadataConnected = ClientWork([&] { return metadataClient.connect(host.Port); });
        smb2_stat_64 metadata{};
        const int metadataRead = metadataConnected ? metadataConnected : ClientWork([&] {
            return smb2_stat(metadataClient.Context, "data.bin", &metadata);
        });
        host.Source->requestCancel();
        while (read.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
            QTest::qWait(1);
        QVERIFY(read.get() < 0);
        QVERIFY(requested);
        // A consent wait must not freeze directory/metadata traffic from
        // another application while the user considers the question.
        QCOMPARE(metadataConnected, 0);
        QCOMPARE(metadataRead, 0);
        QCOMPARE(metadata.smb2_size, quint64(Payload().size()));
        QCOMPARE(bytes, QByteArray(1024, '\0'));
        QCOMPARE(host.Confirmations, 1);
        QCOMPARE(ClientWork([&] { return smb2_close(client.Context, file); }), 0);
    }
    void allowedExtractionReportsReadFailure() {
        QTemporaryDir fixture;
        const QString path = fixture.filePath("source.zip");
        QVERIFY(NativeArchiveFixture::Write(path, {{"data.bin", Payload()}}, "zip"));
        Host host;
        host.Allow = true;
        QVERIFY(host.start(path, "zip"));
        QTRY_VERIFY_WITH_TIMEOUT(host.Port || !host.Error.isEmpty(), 15000);
        QVERIFY2(host.Port, qPrintable(host.Error));
        Client client;
        QCOMPARE(ClientWork([&] { return client.connect(host.Port); }), 0);
        auto *file = ClientWork([&] { return smb2_open(client.Context, "data.bin", O_RDONLY); });
        QVERIFY(file);
        QByteArray bytes(4096, '\0');
        const auto read = [&] {
            return ClientWork([&] {
                return smb2_pread(client.Context, file, reinterpret_cast<uint8_t *>(bytes.data()),
                                  bytes.size(), 10000);
            });
        };
        QFile source(path);
        QVERIFY(source.open(QIODevice::WriteOnly | QIODevice::Truncate));
        source.close();
        QVERIFY(read() < 0);
        QTRY_COMPARE(host.ReadIssues.size(), 1);
        QVERIFY(host.ReadIssues.front().contains("data.bin"));
        QCOMPARE(bytes, QByteArray(4096, '\0'));
        QCOMPARE(ClientWork([&] { return smb2_close(client.Context, file); }), 0);
    }
    void oneExtractionDecisionForMount_data() {
        QTest::addColumn<bool>("allow");
        QTest::newRow("allow multiple files") << true;
        QTest::newRow("decline without repeated prompts") << false;
    }
    void oneExtractionDecisionForMount() {
        QFETCH(bool, allow);
        QTemporaryDir fixture;
        const QString path = fixture.filePath("source.zip");
        const auto payload = Payload();
        QVERIFY(NativeArchiveFixture::Write(path, {{"first.bin", payload}, {"second.bin", payload}},
                                            "zip"));
        Host host;
        host.Allow = allow;
        // Only one file fits: reading first/second/first also exercises eviction.
        host.CacheQuota = quint64(payload.size());
        QVERIFY(host.start(path, "zip"));
        QTRY_VERIFY_WITH_TIMEOUT(host.Port || !host.Error.isEmpty(), 15000);
        QVERIFY2(host.Port, qPrintable(host.Error));
        Client client;
        QCOMPARE(ClientWork([&] { return client.connect(host.Port); }), 0);
        for (const char *name : {"first.bin", "second.bin", "first.bin"}) {
            auto *file = ClientWork([&] { return smb2_open(client.Context, name, O_RDONLY); });
            QVERIFY(file);
            QByteArray bytes(91, '\0');
            const int count = ClientWork([&] {
                return smb2_pread(client.Context, file, reinterpret_cast<uint8_t *>(bytes.data()),
                                  bytes.size(), 37);
            });
            if (allow) {
                QCOMPARE(count, int(bytes.size()));
                QCOMPARE(bytes, payload.mid(37, bytes.size()));
            } else {
                QVERIFY(count < 0);
                QCOMPARE(bytes, QByteArray(91, '\0'));
            }
            QCOMPARE(ClientWork([&] { return smb2_close(client.Context, file); }), 0);
        }
        QCOMPARE(host.Confirmations, 1);
    }
#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
    void cancellingMountSetupStartsNoSession() {
        const auto unavailable = QtSmbMountPlatform::unavailableReason();
        if (!unavailable.isEmpty())
            QSKIP(qPrintable(unavailable));
        QWidget owner;
        QtArchiveMountManager manager(&owner);
        QTimer answer;
        connect(&answer, &QTimer::timeout, this, [] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                dialog && dialog->objectName() == "archiveMountSettings")
                dialog->reject();
        });
        answer.start(5);
        manager.mount(nullptr, "Synthetic cancelled mount", {});
        answer.stop();
        QCOMPARE(manager.findChild<QTreeWidget *>("mountList")->topLevelItemCount(), 0);
        QVERIFY(manager.canCloseOwner());
    }
    void nativeMountThroughManager_data() {
        QTest::addColumn<QString>("language");
        QTest::newRow("EN") << QStringLiteral("en");
        QTest::newRow("RU") << QStringLiteral("ru");
    }
    void nativeMountThroughManager() {
        if (!qEnvironmentVariableIsSet("SEVENZIP_NATIVE_SMB_TEST"))
            QSKIP("Opt-in native OS mount; uses only an isolated synthetic fixture.");
        const auto unavailable = QtSmbMountPlatform::unavailableReason();
        if (!unavailable.isEmpty())
            QSKIP(qPrintable(unavailable));
        WidgetDialogFixture dialogs;
        QFETCH(QString, language);
        QTranslator catalog;
        if (language == "ru") {
            QVERIFY(catalog.load(":/translations/sevenzip_ru.qm"));
            qApp->installTranslator(&catalog);
        }
        QTemporaryDir fixture;
        const auto payload = Payload();
        const QString path = fixture.filePath("managed.zip");
        const QString name = QStringLiteral("данные 空 with spaces.bin");
        QVERIFY(NativeArchiveFixture::Write(path,
                                            {{"chosen/" + name, payload},
                                             {"chosen/unsupported:entry", "skip this"},
                                             {"outside.bin", "not mounted"}},
                                            "zip"));
        std::shared_ptr<ArchiveNativeOpen> reader;
        QCOMPARE(ArchiveBackend::openNativeArchive(NativeArchiveFixture::Catalog(), path, "zip",
                                                   nullptr, reader, nullptr),
                 S_OK);
        auto *resource = new QtArchiveFileResource(path);
        CMyComPtr<IUnknown> retained = resource;
        resource->AttachNativeOpen(reader);
        resource->UseNativeMetadata();
        QWidget owner;
        QtArchiveMountManager manager(&owner);
        int held = -1;
        const auto cleanup = qScopeGuard([&] {
            if (held >= 0)
                ::close(held);
            const auto sessions = manager.findChildren<QtArchiveMountSession *>();
            for (auto *session : sessions)
                session->unmount(true);
            for (int attempt = 0; attempt < 300; ++attempt) {
                bool stopped = true;
                for (auto *session : sessions)
                    stopped =
                        stopped && session->status() == QtArchiveMountSession::Status::Stopped;
                if (stopped)
                    break;
                QTest::qWait(100);
            }
        });
        int confirmations = 0;
        QTimer answer;
        connect(&answer, &QTimer::timeout, this, [&] {
            if (auto *prompt =
                    qobject_cast<QtMountExtractionPrompt *>(QApplication::activeModalWidget())) {
                ++confirmations;
                prompt->findChild<QPushButton *>("extractForMountButton")->click();
            } else if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                       dialog && dialog->objectName() == "archiveMountSettings") {
                dialog->accept();
            }
        });
        answer.start(5);
        manager.mount(resource, "Synthetic managed ZIP", {}, "chosen/");
        retained.Release();
        reader.reset();
        auto *copy = manager.findChild<QPushButton *>("copyMountPathButton");
        QTRY_VERIFY_WITH_TIMEOUT(copy->isEnabled(), 15000);
        copy->click();
        auto *remove = manager.findChild<QAction *>("removeMountAction");
        QVERIFY(!remove->isEnabled());
        remove->trigger();
        QCOMPARE(manager.findChild<QTreeWidget *>("mountList")->topLevelItemCount(), 1);
        const QString mounted = QApplication::clipboard()->text();
        QCOMPARE(QDir(mounted).entryList(QDir::Files), QStringList{name});
        QVERIFY(manager.findChild<QPlainTextEdit *>("mountDetails")
                    ->toPlainText()
                    .contains("chosen/unsupported:entry"));
        const auto bytes = ClientWork([&] {
            QFile file(QDir(mounted).filePath(name));
            return file.open(QIODevice::ReadOnly) && file.seek(17000) ? file.read(4096)
                                                                      : QByteArray();
        });
        answer.stop();
        const auto encodedPath = QFile::encodeName(QDir(mounted).filePath(name));
        QTimer::singleShot(0, this, [] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
                dialog->reject();
        });
        QVERIFY(!manager.canCloseOwner());
        held = ClientWork([&] { return ::open(encodedPath.constData(), O_RDONLY); });
        QVERIFY(held >= 0);
        manager.findChild<QPushButton *>("unmountButton")->click();
        QTRY_VERIFY_WITH_TIMEOUT(copy->isEnabled(), 15000);
        QVERIFY(manager.findChild<QPushButton *>("forceUnmountButton")->isVisible());
        QVERIFY(!remove->isEnabled());
        manager.resize(640, 400);
        QTest::qWait(10);
        bool geometryFits = true;
        for (auto *button : manager.findChildren<QPushButton *>())
            if (button->isVisible())
                geometryFits = geometryFits && UiInspection::fullyVisible(*button, manager);
        const bool reportWritten = UiInspection::writeReport(
            manager, QStringLiteral("mount-manager-busy-%1-640").arg(language));
        QByteArray afterBusy(1024, '\0');
        const auto count = ClientWork([&] { return ::pread(held, afterBusy.data(), 1024, 7000); });
        ::close(held);
        held = -1;
        manager.findChild<QPushButton *>("unmountButton")->click();
        auto *list = manager.findChild<QTreeWidget *>("mountList");
        QTRY_VERIFY_WITH_TIMEOUT(list->topLevelItem(0)->text(1).isEmpty(), 15000);
        auto *session = manager.findChild<QtArchiveMountSession *>();
        QVERIFY(session);
        QTRY_COMPARE_WITH_TIMEOUT(session->status(), QtArchiveMountSession::Status::Stopped, 15000);
        QVERIFY(remove->isEnabled());
        remove->trigger();
        QCOMPARE(list->topLevelItemCount(), 0);
        QVERIFY(manager.canCloseOwner());
        QCOMPARE(confirmations, 1);
        QVERIFY(geometryFits);
        QVERIFY(reportWritten);
        QCOMPARE(count, 1024);
        QCOMPARE(afterBusy, payload.mid(7000, 1024));
        QCOMPARE(bytes, payload.mid(17000, 4096));
        QVERIFY(!QFileInfo::exists(mounted));
    }
    void nativeMountLifecycle_data() {
        QTest::addColumn<QString>("format");
        QTest::addColumn<bool>("serverCrash");
        QTest::newRow("tar") << QStringLiteral("tar") << false;
        QTest::newRow("zip consent") << QStringLiteral("zip") << false;
        QTest::newRow("server crash") << QStringLiteral("tar") << true;
    }
    void nativeMountLifecycle() {
        if (!qEnvironmentVariableIsSet("SEVENZIP_NATIVE_SMB_TEST"))
            QSKIP("Opt-in native OS mount; uses only an isolated synthetic fixture.");
        QFETCH(QString, format);
        QFETCH(bool, serverCrash);
        QTemporaryDir fixture;
        const auto payload = Payload();
        const QString path = fixture.filePath("native." + format);
        const QString name = QStringLiteral("данные 空 with spaces.bin");
        QVERIFY(NativeArchiveFixture::Write(path, {{name, payload}}, format));
        auto catalog = NativeArchiveFixture::Catalog();
        std::shared_ptr<ArchiveNativeOpen> reader;
        QCOMPARE(ArchiveBackend::openNativeArchive(catalog, path, format, nullptr, reader, nullptr),
                 S_OK);
        auto *resource = new QtArchiveFileResource(path);
        CMyComPtr<IUnknown> retained = resource;
        resource->AttachNativeOpen(reader);
        resource->UseNativeMetadata();
        QtArchiveMountSession session("Synthetic native mount");
        const auto cleanup = qScopeGuard([&] {
            if (session.status() == QtArchiveMountSession::Status::Stopped)
                return;
            session.unmount(true);
            for (int attempt = 0;
                 attempt < 300 && session.status() != QtArchiveMountSession::Status::Stopped;
                 ++attempt)
                QTest::qWait(100);
        });
        int confirmations = 0;
        connect(&session, &QtArchiveMountSession::extractionRequired, this,
                [&](const QString &entry, quint64 size, const ArchiveMountDecisionPtr &decision) {
                    ++confirmations;
                    QCOMPARE(entry, name);
                    QCOMPARE(size, quint64(payload.size()));
                    decision->answer(true);
                });
        session.start(resource, 4 * 1024 * 1024, {}, QStringLiteral(SEVENZIP_SMB_SERVER));
        retained.Release();
        reader.reset();
        QTRY_VERIFY_WITH_TIMEOUT(session.status() == QtArchiveMountSession::Status::Mounted ||
                                     session.status() == QtArchiveMountSession::Status::Stopped,
                                 30000);
        const bool mounted = session.status() == QtArchiveMountSession::Status::Mounted;
        if (!mounted) {
            QVERIFY2(mounted, qPrintable(session.error()));
            return;
        }
        const QString mountedPath = session.path();
        const auto bytes = ClientWork([&] {
            QFile file(QDir(mountedPath).filePath(name));
            return file.open(QIODevice::ReadOnly) && file.seek(100003) ? file.read(4096)
                                                                       : QByteArray();
        });
        const bool contentsMatch = bytes == payload.mid(100003, 4096);
        if (serverCrash) {
            QProcess identify;
            identify.start(
                "/usr/bin/pgrep",
                {"-P", QString::number(QCoreApplication::applicationPid()), "-x", "SevenzipSmb"});
            QVERIFY(identify.waitForFinished(10000));
            const auto candidates = identify.readAllStandardOutput().trimmed().split('\n');
            QCOMPARE(candidates.size(), 1);
            bool valid = false;
            const auto server = candidates.front().toLongLong(&valid);
            QVERIFY(valid && server > 0);
            QCOMPARE(::kill(pid_t(server), SIGKILL), 0);
            QTRY_VERIFY_WITH_TIMEOUT(!session.error().isEmpty(), 10000);
            QCOMPARE(session.status(), QtArchiveMountSession::Status::Mounted);
        }
        session.unmount(serverCrash);
        QTRY_VERIFY_WITH_TIMEOUT(session.status() == QtArchiveMountSession::Status::Stopped ||
                                     session.status() == QtArchiveMountSession::Status::Mounted,
                                 15000);
        if (session.status() == QtArchiveMountSession::Status::Mounted) {
            session.unmount(true);
            QTRY_COMPARE_WITH_TIMEOUT(session.status(), QtArchiveMountSession::Status::Stopped,
                                      15000);
            QFAIL("Normal unmount failed; the isolated fixture was force-unmounted for cleanup.");
        }
        QVERIFY2(contentsMatch, qPrintable(session.error()));
        QCOMPARE(confirmations, format == "zip" ? 1 : 0);
        QVERIFY(!QFileInfo::exists(mountedPath));
    }
#endif
};

QTEST_MAIN(ArchiveMountTransportTests)
#include "archive_mount_transport_tests.moc"
