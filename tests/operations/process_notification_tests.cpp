#include "archive/backend/archivebackend.h"
#include "archive/support/native_archive_fixture.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

namespace {
bool write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
} // namespace

class ProcessNotificationTests final : public QObject {
    Q_OBJECT
  private slots:
    void receiversMayReleaseBackend_data() {
        QTest::addColumn<QString>("signal");
        for (const char *name : {"output", "info", "entries", "password", "started", "finished",
                                 "start-failure", "overwrite"})
            QTest::newRow(name) << QString::fromLatin1(name);
    }
    void receiversMayReleaseBackend() {
        QFETCH(QString, signal);
        QTemporaryDir fixture;
        const QString archive = fixture.filePath(QStringLiteral("архив 空 with spaces.zip"));
        QVERIFY(NativeArchiveFixture::Write(archive, {{"first.txt", "original"}}, "7z",
                                            signal == "password" ? QString("fixture password")
                                                                 : QString()));
        QFile snapshot(archive);
        QVERIFY(snapshot.open(QIODevice::ReadOnly));
        const QByteArray originalBytes = snapshot.readAll();
        snapshot.close();
        auto backend = std::make_unique<ArchiveBackend>(
            nullptr, signal == "start-failure" ? fixture.filePath("missing-module") : QString());
        QPointer<ArchiveBackend> owner(backend.get());
        const auto release = [&] { backend.reset(); };
        if (signal == "overwrite") {
            QSignalSpy listed(backend.get(), &ArchiveBackend::finished);
            backend->list(archive);
            QTRY_COMPARE(listed.size(), 1);
            QVERIFY(listed.first().first().toBool());
            const QString output = fixture.filePath("output");
            QVERIFY(QDir().mkpath(output));
            QVERIFY(write(output + "/first.txt", "unchanged"));
            connect(backend.get(), &ArchiveBackend::overwriteRequired, this, release);
            ExtractionOptions options;
            options.overwriteMode = ExtractionOptions::OverwriteMode::Ask;
            backend->extract(archive, output, {}, {}, options);
            QTRY_VERIFY(owner.isNull());
            QFile original(output + "/first.txt");
            QVERIFY(original.open(QIODevice::ReadOnly));
            QCOMPARE(original.readAll(), QByteArray("unchanged"));
        } else {
            if (signal == "output")
                connect(backend.get(), &ArchiveBackend::outputReceived, this, release);
            else if (signal == "info")
                connect(backend.get(), &ArchiveBackend::infoReady, this, release);
            else if (signal == "entries")
                connect(backend.get(), &ArchiveBackend::entriesReady, this, release);
            else if (signal == "password")
                connect(backend.get(), &ArchiveBackend::passwordRequired, this, release);
            else if (signal == "started")
                connect(backend.get(), &ArchiveBackend::phaseStarted, this, release);
            else
                connect(backend.get(), &ArchiveBackend::finished, this, release);
            if (signal == "output")
                backend->hashMethods({archive}, {"CRC32"});
            else
                backend->list(archive);
            QTRY_VERIFY_WITH_TIMEOUT(owner.isNull(), 5000);
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QFile original(archive);
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), originalBytes);
    }
    void listingPayloadSurvivesRestart_data() {
        QTest::addColumn<bool>("fromInfo");
        QTest::newRow("info-receiver") << true;
        QTest::newRow("first-entries-receiver") << false;
    }
    void listingPayloadSurvivesRestart() {
        QFETCH(bool, fromInfo);
        QTemporaryDir fixture;
        const QString first = fixture.filePath("first.zip"),
                      second = fixture.filePath("second.zip");
        QVERIFY(NativeArchiveFixture::Write(first, {{"first.txt"}}, "zip"));
        QVERIFY(NativeArchiveFixture::Write(second, {{"second.txt"}}, "zip"));
        ArchiveBackend backend;
        bool restarted = false;
        const auto restart = [&] {
            if (!restarted) {
                restarted = true;
                backend.list(second);
            }
        };
        if (fromInfo)
            connect(&backend, &ArchiveBackend::infoReady, this, restart);
        else
            connect(&backend, &ArchiveBackend::entriesReady, this, restart);
        QSignalSpy entries(&backend, &ArchiveBackend::entriesReady);
        QSignalSpy completed(&backend, &ArchiveBackend::finished);
        backend.list(first);
        QTRY_COMPARE(completed.size(), 2);
        QCOMPARE(entries.size(), 2);
        const auto original = qvariant_cast<QList<ArchiveEntry>>(entries[0][0]);
        const auto replacement = qvariant_cast<QList<ArchiveEntry>>(entries[1][0]);
        QCOMPARE(original.size(), 1);
        QCOMPARE(original[0].path, QString("first.txt"));
        QCOMPARE(replacement.size(), 1);
        QCOMPARE(replacement[0].path, QString("second.txt"));
        QVERIFY(completed[0][0].toBool());
        QVERIFY(completed[1][0].toBool());
        QVERIFY(!backend.busy());
    }
    void listingDoesNotOverwriteReentrantRequestResult() {
        QTemporaryDir fixture;
        const QString archive = fixture.filePath("first.zip");
        QVERIFY(NativeArchiveFixture::Write(archive, {{"first.txt"}}, "zip"));
        ArchiveBackend backend;
        connect(&backend, &ArchiveBackend::infoReady, this,
                [&] { backend.hashMethods({}, {"CRC32"}); });
        QSignalSpy completed(&backend, &ArchiveBackend::finished);
        backend.list(archive);
        QTRY_COMPARE(completed.size(), 2);
        QVERIFY(!completed[0][0].toBool());
        QVERIFY(completed[1][0].toBool());
        QCOMPARE(backend.lastResult(), HRESULT(E_INVALIDARG));
    }
    void listingDiagnosticReceiverCanExtractCompletedArchive() {
        QTemporaryDir fixture;
        const QString archive = fixture.filePath(QStringLiteral("архив 空.7z"));
        const QByteArray payload("retained bytes\0binary", 21);
        QVERIFY(NativeArchiveFixture::Write(archive, {{"first.txt", payload}}, "7z"));
        QFile file(archive);
        QVERIFY(file.open(QIODevice::Append));
        const QByteArray trailing("nonzero trailing data");
        QCOMPARE(file.write(trailing), qint64(trailing.size()));
        file.close();
        const QString destination = fixture.filePath("extracted");
        ArchiveBackend backend;
        bool requested = false;
        bool listed = false;
        connect(&backend, &ArchiveBackend::outputReceived, this, [&] {
            if (requested)
                return;
            requested = true;
            listed = backend.hasListedSourceArchive(archive);
            backend.extract(archive, destination, {"first.txt"});
        });
        QSignalSpy completed(&backend, &ArchiveBackend::finished);
        QSignalSpy entries(&backend, &ArchiveBackend::entriesReady);
        backend.list(archive);
        QTRY_COMPARE(completed.size(), 2);
        QVERIFY(requested);
        QVERIFY(listed);
        QVERIFY(completed[0][0].toBool());  // The completed listing retains its own result.
        QVERIFY(!completed[1][0].toBool()); // Trailing data still makes extraction unverified.
        QCOMPARE(entries.size(), 1);
        const auto listing = qvariant_cast<QList<ArchiveEntry>>(entries[0][0]);
        QCOMPARE(listing.size(), 1);
        QCOMPARE(listing[0].path, QString("first.txt"));
        QFile extracted(QDir(destination).filePath("first.txt"));
        QVERIFY(extracted.open(QIODevice::ReadOnly));
        QCOMPARE(extracted.readAll(), payload);
        QVERIFY(!backend.busy());
    }
};

QTEST_GUILESS_MAIN(ProcessNotificationTests)
#include "process_notification_tests.moc"
