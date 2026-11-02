#include "mount/smb/smbreadonlyserver.h"
#include <QProcess>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtEndian>
#include <QtTest>
#include <array>
#include <fcntl.h>
#include <future>
#include <stdexcept>
#include <utility>
#ifdef Q_OS_UNIX
#include <poll.h>
#endif
// clang-format off
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
#include <smb2/libsmb2-raw.h>
// clang-format on

#ifdef SEVENZIP_USE_SMB2_MODULE
import sevenzip.dependencies.smb2;
#else
#include "dependencies/smb2/facade.h"
#endif

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
        smb2_set_version(Context, SMB2_VERSION_0210);
        smb2_set_sign(Context, 1);
        smb2_set_timeout(Context, 3);
        smb2_set_user(Context, "szprobe");
        smb2_set_password(Context, "LocalProbeOnly427");
    }
    ~Client() { smb2_destroy_context(Context); }
    int connect(quint16 port) {
        const auto address = QByteArray("127.0.0.1:") + QByteArray::number(port);
        return ClientWork([&] {
            return smb2_connect_share(Context, address.constData(), "Sevenzip", "szprobe");
        });
    }
};
class ReadTamperProxy final : public QObject {
    QTcpServer listener_;
    QTcpSocket peer_;
    QByteArray bytes_, responses_, held_;
    QTcpSocket *incoming_ = nullptr;
    bool strip_, hold_;

    static QByteArray takeFrame(QByteArray &bytes) {
        if (bytes.size() < 4)
            return {};
        const quint32 length =
            (quint32(quint8(bytes[1])) << 16) | (quint32(quint8(bytes[2])) << 8) | quint8(bytes[3]);
        if (length > 1024 * 1024 || bytes.size() < qsizetype(length) + 4)
            return {};
        auto frame = bytes.left(length + 4);
        bytes.remove(0, length + 4);
        return frame;
    }

  public:
    quint64 ReadMessage = 0;
    int CancelReplies = 0;
    explicit ReadTamperProxy(quint16 destination, bool strip, bool hold = false)
        : strip_(strip), hold_(hold) {
        connect(&listener_, &QTcpServer::newConnection, this, [this, destination] {
            auto *incoming = listener_.nextPendingConnection();
            incoming_ = incoming;
            connect(&peer_, &QTcpSocket::readyRead, incoming, [this, incoming] {
                responses_ += peer_.readAll();
                for (auto frame = takeFrame(responses_); !frame.isEmpty();
                     frame = takeFrame(responses_)) {
                    const bool header = frame.size() >= 68;
                    const auto command =
                        header ? qFromLittleEndian<quint16>(frame.constData() + 16) : 0;
                    if (header && command == SMB2_CANCEL)
                        ++CancelReplies;
                    if (hold_ && header && command == SMB2_READ)
                        held_ += frame;
                    else
                        incoming->write(frame);
                }
            });
            connect(&peer_, &QTcpSocket::disconnected, incoming, &QTcpSocket::disconnectFromHost);
            connect(incoming, &QTcpSocket::disconnected, &peer_, &QTcpSocket::disconnectFromHost);
            connect(incoming, &QTcpSocket::readyRead, this, [this, incoming] {
                bytes_ += incoming->readAll();
                for (auto frame = takeFrame(bytes_); !frame.isEmpty(); frame = takeFrame(bytes_)) {
                    if (frame.size() >= 68 && quint8(frame[16]) == SMB2_READ && frame[17] == 0) {
                        ReadMessage = qFromLittleEndian<quint64>(frame.constData() + 28);
                        if (hold_) {
                            // Preserve the signed request and delay only its
                            // reply so the client can cancel an outstanding MID.
                        } else if (strip_)
                            frame[20] = char(quint8(frame[20]) & ~SMB2_FLAGS_SIGNED);
                        else
                            frame[52] = char(quint8(frame[52]) ^ 1);
                    }
                    peer_.write(frame);
                }
            });
            peer_.connectToHost(QHostAddress::LocalHost, destination);
        });
        listener_.listen(QHostAddress::LocalHost, 0);
    }
    quint16 port() const { return listener_.serverPort(); }
    void releaseRead() {
        hold_ = false;
        if (incoming_)
            incoming_->write(std::exchange(held_, {}));
    }
};
} // namespace

class SmbReadOnlyServerTests final : public QObject {
    Q_OBJECT
  private slots:
    void wireNamesPreserveUnicode() {
        // SMB names are little-endian UTF-16, including non-BMP pairs.
        const std::array wire{qToLittleEndian<quint16>(0x0416), qToLittleEndian<quint16>(0xD83D),
                              qToLittleEndian<quint16>(0xDE00)};
        const auto decoded = sevenzip::smb2::decode_name(wire);
        QVERIFY(decoded);
        QCOMPARE(QString::fromUtf8(decoded->get()), QString::fromUtf8("Ж😀"));
        const auto empty = sevenzip::smb2::decode_name({});
        QVERIFY(empty);
        QCOMPARE(QByteArray(empty->get()), QByteArray());
    }

    void listeningExceptionClosesServerSocket() {
        SmbReadOnlyServer server({{1, 0, 0, 0, {}, true}}, "probe", "password",
                                 [](quint64, quint64, quint32) { return SmbTreeRead{}; });
        QString error;
        QVERIFY(server.validate(error));
        quint16 port = 0;
        QVERIFY(server.serve(0, [&](quint16 listening) {
            port = listening;
            throw std::runtime_error("fixture listening failure");
        }) != 0);
        QVERIFY(port);
        QTcpServer replacement;
        QVERIFY(replacement.listen(QHostAddress::LocalHost, port));
    }
    void callbackFailureRejectsOnlyCurrentConnection_data() {
        QTest::addColumn<QString>("failure");
        QTest::newRow("authorize") << QStringLiteral("--throw-authorize");
        QTest::newRow("tree connect") << QStringLiteral("--throw-tree");
    }
    void callbackFailureRejectsOnlyCurrentConnection() {
        QFETCH(QString, failure);
        QProcess server;
        server.start(QStringLiteral(SEVENZIP_SMB_PROBE), {"0", failure});
        QVERIFY(server.waitForStarted(10000));
        const auto cleanup = qScopeGuard([&] {
            server.terminate();
            if (!server.waitForFinished(5000)) {
                server.kill();
                server.waitForFinished(5000);
            }
        });
        QByteArray diagnostics;
        const QRegularExpression ready("loopback SMB fixture ready on port ([0-9]+)");
        QRegularExpressionMatch match;
        QTRY_VERIFY_WITH_TIMEOUT((diagnostics += server.readAllStandardError(),
                                  match = ready.match(QString::fromUtf8(diagnostics)),
                                  match.hasMatch()),
                                 10000);
        const quint16 port = match.captured(1).toUShort();
        {
            Client rejected;
            QVERIFY(rejected.connect(port) < 0);
        }
        QCOMPARE(server.state(), QProcess::Running);
        Client accepted;
        QCOMPARE(accepted.connect(port), 0);
        auto *file = ClientWork([&] { return smb2_open(accepted.Context, "hello.txt", O_RDONLY); });
        QVERIFY(file);
        QCOMPARE(ClientWork([&] { return smb2_close(accepted.Context, file); }), 0);
    }
    void asynchronousReadsKeepFirstResultAndRecoverAfterError() {
        QProcess server;
        server.start(QStringLiteral(SEVENZIP_SMB_PROBE), {"0", "--async"});
        QVERIFY(server.waitForStarted(10000));
        const auto cleanup = qScopeGuard([&] {
            server.terminate();
            if (!server.waitForFinished(5000)) {
                server.kill();
                server.waitForFinished(5000);
            }
        });
        QByteArray diagnostics;
        const QRegularExpression ready("loopback SMB fixture ready on port ([0-9]+)");
        QRegularExpressionMatch match;
        QTRY_VERIFY_WITH_TIMEOUT((diagnostics += server.readAllStandardError(),
                                  match = ready.match(QString::fromUtf8(diagnostics)),
                                  match.hasMatch()),
                                 10000);
        Client client;
        QCOMPARE(client.connect(match.captured(1).toUShort()), 0);
        // Each asynchronous reader responds from another thread twice, with
        // deliberately conflicting bytes/status in the second completion.
        for (const char *name : {"hello.txt", "large.bin", "Файл с пробелом.bin"}) {
            auto *file = ClientWork([&] { return smb2_open(client.Context, name, O_RDONLY); });
            QVERIFY2(file, smb2_get_error(client.Context));
            const auto close =
                qScopeGuard([&] { ClientWork([&] { return smb2_close(client.Context, file); }); });
            const bool failed = QByteArray(name) == "large.bin";
            const quint64 offset = QByteArray(name) == "hello.txt" ? 1003 : 262140;
            QByteArray bytes(128, '\0');
            const int count = ClientWork([&] {
                return smb2_pread(client.Context, file, reinterpret_cast<uint8_t *>(bytes.data()),
                                  bytes.size(), offset);
            });
            if (failed) {
                QVERIFY(count < 0);
                QCOMPARE(bytes, QByteArray(128, '\0'));
            } else {
                QCOMPARE(count, offset == 1003 ? 128 : 4);
                QByteArray expected(count, Qt::Uninitialized);
                for (int i = 0; i < count; ++i)
                    expected[i] = char((offset + i) % 251);
                QCOMPARE(bytes.left(count), expected);
                QCOMPARE(bytes.mid(count), QByteArray(bytes.size() - count, '\0'));
            }
        }
        QCOMPARE(server.state(), QProcess::Running);
    }
    void cancelHasNoReplyAndPreservesOutstandingRead() {
#ifndef Q_OS_UNIX
        QSKIP("This wire-client driver uses POSIX poll; native Windows lifecycle is checked "
              "separately.");
#else
        QProcess server;
        server.start(QStringLiteral(SEVENZIP_SMB_PROBE), {"0"});
        QVERIFY(server.waitForStarted(10000));
        const auto cleanup = qScopeGuard([&] {
            server.terminate();
            if (!server.waitForFinished(5000)) {
                server.kill();
                server.waitForFinished(5000);
            }
        });
        QByteArray diagnostics;
        const QRegularExpression ready("loopback SMB fixture ready on port ([0-9]+)");
        QRegularExpressionMatch match;
        QTRY_VERIFY((diagnostics += server.readAllStandardError(),
                     match = ready.match(QString::fromUtf8(diagnostics)), match.hasMatch()));
        ReadTamperProxy proxy(match.captured(1).toUShort(), false, true);
        struct Result {
            bool Done = false;
            int Status = -1;
        } read, echo;
        QByteArray data(128, '\0');
        Client client;
        QCOMPARE(client.connect(proxy.port()), 0);
        auto *file = ClientWork([&] { return smb2_open(client.Context, "hello.txt", O_RDONLY); });
        QVERIFY(file);
        const auto completed = [](smb2_context *, int status, void *, void *opaque) {
            auto *result = static_cast<Result *>(opaque);
            result->Status = status;
            result->Done = true;
        };
        QTimer transport;
        connect(&transport, &QTimer::timeout, this, [&] {
            pollfd socket{smb2_get_fd(client.Context), short(smb2_which_events(client.Context)), 0};
            if (::poll(&socket, 1, 0) > 0)
                smb2_service(client.Context, socket.revents);
        });
        transport.start(1);
        QCOMPARE(smb2_pread_async(client.Context, file, reinterpret_cast<uint8_t *>(data.data()),
                                  data.size(), 0, completed, &read),
                 0);
        QTRY_VERIFY(proxy.ReadMessage);
        auto *cancel = smb2_cmd_cancel_async(client.Context, proxy.ReadMessage, nullptr, nullptr);
        QVERIFY(cancel);
        smb2_queue_pdu(client.Context, cancel);
        QCOMPARE(smb2_echo_async(client.Context, completed, &echo), 0);
        QTRY_VERIFY(echo.Done);
        QCOMPARE(echo.Status, 0);
        QCOMPARE(proxy.CancelReplies, 0);
        QVERIFY(!read.Done);
        proxy.releaseRead();
        QTRY_VERIFY(read.Done);
        QCOMPARE(read.Status, 128);
        for (int index = 0; index < data.size(); ++index)
            QCOMPARE(quint8(data[index]), quint8(index % 251));
        transport.stop();
        QCOMPARE(ClientWork([&] { return smb2_close(client.Context, file); }), 0);
#endif
    }
    void forgedReadNeverReachesReader_data() {
        QTest::addColumn<bool>("stripSignatureFlag");
        QTest::newRow("cleared signed flag") << true;
        QTest::newRow("invalid signature") << false;
    }
    void forgedReadNeverReachesReader() {
        QFETCH(bool, stripSignatureFlag);
        // The production server is a separate process. libsmb2's server loop
        // services its global context list; client contexts must not share it.
        QProcess server;
        server.start(QStringLiteral(SEVENZIP_SMB_PROBE), {"0", "--trace"});
        QVERIFY(server.waitForStarted(10000));
        const auto cleanup = qScopeGuard([&] {
            server.terminate();
            if (!server.waitForFinished(5000)) {
                server.kill();
                server.waitForFinished(5000);
            }
        });
        QByteArray diagnostics;
        const QRegularExpression ready("loopback SMB fixture ready on port ([0-9]+)");
        QRegularExpressionMatch match;
        QTRY_VERIFY_WITH_TIMEOUT((diagnostics += server.readAllStandardError(),
                                  match = ready.match(QString::fromUtf8(diagnostics)),
                                  match.hasMatch()),
                                 10000);
        const quint16 port = match.captured(1).toUShort();
        ReadTamperProxy proxy(port, stripSignatureFlag);
        QVERIFY(proxy.port());
        Client attacker;
        QCOMPARE(attacker.connect(proxy.port()), 0);
        auto *file = ClientWork([&] { return smb2_open(attacker.Context, "hello.txt", O_RDONLY); });
        QVERIFY(file);
        const auto closeFile = qScopeGuard([&] {
            // A rejected signature disconnects this client. Queue CLOSE so
            // context shutdown releases the handle without awaiting a reply.
            smb2_close_async(
                attacker.Context, file, [](smb2_context *, int, void *, void *) {}, nullptr);
        });
        QByteArray data(128, '\0');
        QVERIFY(ClientWork([&] {
                    return smb2_pread(attacker.Context, file,
                                      reinterpret_cast<uint8_t *>(data.data()), data.size(), 0);
                }) < 0);
        diagnostics += server.readAllStandardError();
        QVERIFY(!diagnostics.contains("\nfixture read\n"));
        QCOMPARE(data, QByteArray(128, '\0'));
        Client valid;
        QCOMPARE(valid.connect(port), 0);
        auto *validFile =
            ClientWork([&] { return smb2_open(valid.Context, "hello.txt", O_RDONLY); });
        QVERIFY(validFile);
        QCOMPARE(ClientWork([&] {
                     return smb2_pread(valid.Context, validFile,
                                       reinterpret_cast<uint8_t *>(data.data()), data.size(), 0);
                 }),
                 128);
        QByteArray expected(128, Qt::Uninitialized);
        for (int i = 0; i < expected.size(); ++i)
            expected[i] = char(i % 251);
        QCOMPARE(data, expected);
        QCOMPARE(ClientWork([&] { return smb2_close(valid.Context, validFile); }), 0);
    }
};
QTEST_GUILESS_MAIN(SmbReadOnlyServerTests)
#include "smb_readonly_server_tests.moc"
