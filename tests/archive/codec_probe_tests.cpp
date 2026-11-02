#include "archive/codecs/archivecodecprobe.h"
#include "support/slow_codec.h"
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

class CodecProbeTests : public QObject {
    Q_OBJECT

  private slots:
    void nativeDiscoveryDoesNotRequireAConsole() {
#ifndef SEVENZIP_CODEC_MODULE
        QSKIP("Official module unavailable; native discovery remains unverified");
#else
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString source = QString::fromUtf8(SEVENZIP_CODEC_MODULE);
        const QString module = temporary.filePath(QFileInfo(source).fileName());
        QVERIFY(QFile::copy(source, module));
        ArchiveCodecProbe probe;
        int calls = 0;
        ArchiveCodecProbeResult result;
        QVERIFY(probe.Start(module, [&](auto value) {
            QCOMPARE(QThread::currentThread(), thread());
            result = std::move(value);
            ++calls;
        }));
        QTRY_COMPARE(calls, 1);
        QCOMPARE(result.result, S_OK);
        QVERIFY2(result.catalog, qPrintable(result.error));
        // The selected module supplies the native opening interface directly.
        CArchiveLink opened;
        const QString input = temporary.filePath("not an archive.zip");
        QFile file(input);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("ordinary bytes"), qint64(14));
        file.close();
        QCOMPARE(result.catalog->openArchive(opened, input, "zip", nullptr), S_FALSE);
        QVERIFY(!probe.active());
#endif
    }

    void deadlineDoesNotDependOnGuiTimerDelivery() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        SlowCodecModuleFixture module(temporary, 2100);
        QVERIFY(module.Valid);
        ArchiveCodecProbe probe;
        int calls = 0;
        ArchiveCodecProbeResult result;
        QVERIFY(probe.Start(
            module.Path,
            [&](auto value) {
                QCOMPARE(QThread::currentThread(), thread());
                result = std::move(value);
                ++calls;
            },
            1500));
        // Withhold GUI timers/completion while the native query finishes.
        QElapsedTimer wait;
        wait.start();
        while (!QFileInfo::exists(module.Finished) && wait.elapsed() < 5000)
            QThread::msleep(5);
        QVERIFY(QFileInfo::exists(module.Entered));
        QVERIFY(QFileInfo::exists(module.Finished));
        QCOMPARE(calls, 0);
        QTRY_COMPARE(calls, 1);
        QCOMPARE(result.result, E_ABORT);
        QVERIFY(!result.catalog);
        QVERIFY(!result.error.isEmpty());
        QVERIFY(!probe.active());
    }
    void nativeModuleLoadingKeepsEventLoopResponsive_data() {
        QTest::addColumn<QString>("mode");
        for (const QString &mode : {"failure", "cancel", "deadline", "destroy"})
            QTest::newRow(qPrintable(mode)) << mode;
    }
    void nativeModuleLoadingKeepsEventLoopResponsive() {
        QFETCH(QString, mode);
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        SlowCodecModuleFixture module(temporary, mode == "deadline" ? 2100 : 600);
        QVERIFY(module.Valid);
        auto probe = std::make_unique<ArchiveCodecProbe>();
        ArchiveCodecProbeResult result;
        int calls = 0;
        QElapsedTimer elapsed;
        elapsed.start();
        qint64 previous = 0, maximumGap = 0;
        QTimer heartbeat;
        heartbeat.setInterval(10);
        connect(&heartbeat, &QTimer::timeout, this, [&] {
            const auto now = elapsed.elapsed();
            maximumGap = qMax(maximumGap, now - previous);
            previous = now;
        });
        heartbeat.start();
        QVERIFY(probe->Start(
            module.Path,
            [&](auto value) {
                QCOMPARE(QThread::currentThread(), thread());
                result = std::move(value);
                ++calls;
            },
            mode == "deadline" ? 1500 : 5000));
        QTRY_VERIFY(QFileInfo::exists(module.Entered));
        QVERIFY(probe->active());
        QVERIFY(!QFileInfo::exists(module.Finished));
        QVERIFY(!probe->Start("unused", {}));
        if (mode == "destroy") {
            heartbeat.stop();
            probe.reset();
            QVERIFY(QFileInfo::exists(module.Finished));
            QCoreApplication::processEvents();
            QCOMPARE(calls, 0);
            return;
        }
        if (mode == "cancel")
            probe->Cancel();
        QTRY_COMPARE(calls, 1);
        heartbeat.stop();
        QVERIFY2(maximumGap < 250, qPrintable(QString::number(maximumGap)));
        QVERIFY(QFileInfo::exists(module.Finished));
        QVERIFY(!probe->active());
        QCOMPARE(result.result, mode == "failure" ? E_FAIL : E_ABORT);
        QVERIFY(!result.error.isEmpty());
        if (mode != "failure")
            QVERIFY(!result.catalog);
        QVERIFY(probe->Start(temporary.filePath("missing module"), [&](auto) { ++calls; }));
        QTRY_COMPARE(calls, 2);
        QVERIFY(!probe->active());
    }
    void completionCanRestartOrDestroyProbe_data() {
        QTest::addColumn<bool>("restart");
        QTest::newRow("restart") << true;
        QTest::newRow("destroy") << false;
    }
    void completionCanRestartOrDestroyProbe() {
        QFETCH(bool, restart);
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        auto probe = std::make_unique<ArchiveCodecProbe>();
        const QString missing = temporary.filePath(QStringLiteral("нет модуля 空"));
        int calls = 0;
        QVERIFY(probe->Start(missing, [&](auto result) {
            ++calls;
            QVERIFY(result.result != S_OK);
            QVERIFY(!probe->active());
            if (restart) {
                QVERIFY(probe->Start(missing, [&](auto next) {
                    ++calls;
                    QVERIFY(next.result != S_OK);
                    QVERIFY(!probe->active());
                }));
            } else
                probe.reset();
        }));
        QTRY_COMPARE(calls, restart ? 2 : 1);
        if (restart)
            QVERIFY(!probe->active());
        else
            QVERIFY(!probe);
        QCoreApplication::processEvents();
        QCOMPARE(calls, restart ? 2 : 1);
    }

    void cancellationBeforeLoadingPublishesNoCatalog() {
        QTemporaryDir temporary;
        ArchiveCodecProbe probe;
        int calls = 0;
        ArchiveCodecProbeResult result;
        QVERIFY(probe.Start("unused", [&](auto value) {
            result = std::move(value);
            ++calls;
        }));
        QVERIFY(!probe.Start("unused", {}));
        probe.Cancel();
        QTRY_COMPARE(calls, 1);
        QCOMPARE(result.result, E_ABORT);
        QVERIFY(!result.catalog);
        QVERIFY(!probe.active());
    }
};

QTEST_GUILESS_MAIN(CodecProbeTests)
#include "codec_probe_tests.moc"
