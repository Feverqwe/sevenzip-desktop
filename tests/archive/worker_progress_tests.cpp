#include "archive/backend/archivebackend.h"
#include <QElapsedTimer>
#include <QFile>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

class WorkerProgressTests final : public QObject {
    Q_OBJECT
  private slots:
    void destructionDoesNotStartPipelineContinuation() {
        QTemporaryDir fixture;
        const QString path = fixture.filePath(QStringLiteral("источник 空.zip"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("123456789"), qint64(9));
        file.close();
        ArchiveBackend next;
        auto backend = std::make_unique<ArchiveBackend>();
        int continuations = 0;
        connect(backend.get(), &ArchiveBackend::finished, this, [&](bool, const QString &) {
            ++continuations;
            next.hashMethods({path}, {"CRC32"});
            next.setPaused(true);
        });
        const QString output = fixture.filePath("cancelled.zip");
        backend->hashMethods({path}, {"SHA256"});
        QCOMPARE(backend->setPaused(true), HRESULT(S_OK));
        backend.reset();
        QCOMPARE(continuations, 0);
        QVERIFY(!next.busy());
        QVERIFY(!QFileInfo::exists(output));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("123456789"));
    }
    void joinedHashCancellationDoesNotJoinReplacementHash() {
        QTemporaryDir fixture;
        const QString source = fixture.filePath("source.zip");
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("123456789"), qint64(9));
        file.close();
        ArchiveBackend backend;
        QSignalSpy completed(&backend, &ArchiveBackend::finished);
        connect(&backend, &ArchiveBackend::finished, this, [&](bool, const QString &) {
            if (completed.size() == 1)
                backend.hashMethods({source}, {"CRC32"});
        });
        backend.hashMethods({source}, {"SHA256"});
        backend.cancelAndWait();
        QCOMPARE(completed.size(), 1);
        QVERIFY(!completed.first().first().toBool());
        QVERIFY(backend.busy());
        QTRY_COMPARE(completed.size(), 2);
        QVERIFY(completed.last().first().toBool());
        QVERIFY(!backend.busy());
        QCOMPARE(backend.lastResult(), HRESULT(S_OK));
    }
    void joinedCancellationMayReleaseBackend() {
        QTemporaryDir fixture;
        const QString source = fixture.filePath("source.zip");
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("original"), qint64(8));
        file.close();
        auto backend = std::make_unique<ArchiveBackend>();
        int completions = 0;
        connect(backend.get(), &ArchiveBackend::finished, this, [&](bool ok, const QString &) {
            QVERIFY(!ok);
            ++completions;
            backend.reset();
        });
        connect(backend.get(), &ArchiveBackend::phaseStarted, this,
                [&] { backend->cancelAndWait(); });
        const QString output = fixture.filePath("cancelled.zip");
        backend->hashMethods({source}, {"CRC32"});
        QVERIFY(!backend);
        QCOMPARE(completions, 1);
        QVERIFY(!QFileInfo::exists(output));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("original"));
    }
    void hashOutputMayReleaseBackend() {
        QTemporaryDir fixture;
        const QString path = fixture.filePath(QStringLiteral("имя с пробелом.txt"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("123456789"), qint64(9));
        file.close();
        auto backend = std::make_unique<ArchiveBackend>();
        QPointer<ArchiveBackend> retained(backend.get());
        int outputs = 0;
        connect(backend.get(), &ArchiveBackend::outputReceived, this, [&](const QString &line) {
            QVERIFY(line.contains("CBF43926"));
            ++outputs;
            backend.reset();
        });
        backend->hashMethods({path}, {"CRC32", "SHA256"});
        QTRY_VERIFY(retained.isNull());
        QCOMPARE(outputs, 1);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("123456789"));
    }
    void reentrantFailureDoesNotRemoveCompletedHashSummary() {
        QTemporaryDir fixture;
        const QString path = fixture.filePath("input.txt");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("123456789"), qint64(9));
        file.close();
        ArchiveBackend backend;
        QSignalSpy completed(&backend, &ArchiveBackend::finished);
        QStringList output;
        connect(&backend, &ArchiveBackend::outputReceived, this, [&](const QString &line) {
            output.append(line);
            if (output.size() == 1)
                backend.hashMethods({path}, {"unsupported-test-method"});
        });
        backend.hashMethods({path}, {"CRC32"});
        QTRY_COMPARE(completed.size(), 2);
        QCOMPARE(output.size(), 2);
        QVERIFY(output.last().contains("CBF43926"));
        QVERIFY(!completed.first().first().toBool());
        QVERIFY(completed.last().first().toBool());
        QVERIFY(!backend.busy());
        QVERIFY(backend.lastResult() != S_OK);
    }
    void hashBurstKeepsResultsAndBoundsGuiDelivery() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        constexpr int files = 2048;
        for (int i = 0; i < files; ++i) {
            QFile file(fixture.filePath(QStringLiteral("имя с пробелом %1.txt").arg(i)));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write("123456789"), qint64(9));
        }
        ArchiveBackend backend;
        QSignalSpy completed(&backend, &ArchiveBackend::finished);
        QSignalSpy progress(&backend, &ArchiveBackend::progress);
        QSignalSpy output(&backend, &ArchiveBackend::outputReceived);
        QElapsedTimer elapsed;
        elapsed.start();
        backend.hashMethods({fixture.path()}, {"CRC32"});
        // Simulate a temporarily occupied GUI. Native callbacks must not leave
        // one queued event per file waiting to be delivered afterwards.
        QTest::qSleep(200);
        QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 15000);
        QVERIFY(completed.first().first().toBool());
        QCOMPARE(backend.lastResult(), HRESULT(S_OK));
        int digests = 0;
        for (const auto &line : output)
            digests += line.first().toString().startsWith("CRC32: CBF43926  ");
        QCOMPARE(digests, files);
        const qint64 deliveryLimit = elapsed.elapsed() / 20 + 4;
        QVERIFY2(progress.count() <= deliveryLimit,
                 qPrintable(QString("%1 deliveries in %2 ms; limit %3")
                                .arg(progress.count())
                                .arg(elapsed.elapsed())
                                .arg(deliveryLimit)));
        const int delivered = progress.count();
        QTest::qWait(50);
        QCOMPARE(progress.count(), delivered);
    }
    void joinedHashCancelThenRestartDropsOldProgressAndCompletion() {
        QTemporaryDir fixture;
        const QString path = fixture.filePath("input.txt");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("123456789"), qint64(9));
        file.close();
        ArchiveBackend backend;
        QSignalSpy completed(&backend, &ArchiveBackend::finished);
        backend.hashMethods({path}, {"SHA256"});
        QCOMPARE(backend.setPaused(true), HRESULT(S_OK));
        backend.cancelAndWait();
        QCOMPARE(completed.count(), 1);
        QCOMPARE(backend.lastResult(), HRESULT(E_ABORT));
        const auto pause = connect(&backend, &ArchiveBackend::phaseStarted, this,
                                   [&] { QCOMPARE(backend.setPaused(true), HRESULT(S_OK)); });
        backend.hashMethods({path}, {"CRC32"});
        disconnect(pause);
        // Deliver the joined worker's queued completion while its replacement
        // is paused. It must neither finish nor synchronously join the new job.
        QTest::qWait(50);
        QCOMPARE(completed.count(), 1);
        QVERIFY(backend.busy());
        QCOMPARE(backend.setPaused(false), HRESULT(S_OK));
        QTRY_COMPARE(completed.count(), 2);
        QVERIFY(completed.last().first().toBool());
        QTest::qWait(50);
        QCOMPARE(completed.count(), 2);
        QVERIFY(!backend.busy());
    }
};
QTEST_GUILESS_MAIN(WorkerProgressTests)
#include "worker_progress_tests.moc"
