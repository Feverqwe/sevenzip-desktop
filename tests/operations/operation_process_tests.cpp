#include "operations/qtoperationprocess.h"
#include <QCoreApplication>
#include <QFile>
#include <QProcess>
#include <QSaveFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif

class OperationProcessTests final : public QObject {
    Q_OBJECT
  private slots:
    void exits_data() {
        QTest::addColumn<QString>("mode");
        QTest::addColumn<int>("code");
        QTest::newRow("success") << QString("success") << 0;
        QTest::newRow("operation failure") << QString("failure") << 2;
        QTest::newRow("cancellation exit") << QString("cancelled") << 255;
    }
    void exits() {
        QFETCH(QString, mode);
        QFETCH(int, code);
        QtOperationProcess process;
        const auto started = process.start(QCoreApplication::applicationFilePath(), mode);
        QVERIFY2(started.has_value(), started ? "" : qPrintable(started.error()));
        QTRY_COMPARE(process.poll().Status, QtOperationProcess::State::Exited);
        QCOMPARE(process.poll().Code, quint32(code));
    }
    void unavailableProgram() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QtOperationProcess process;
        const auto failed = process.start(directory.filePath("missing 7zG"), "channel");
        QVERIFY(!failed);
        QVERIFY(!failed.error().isEmpty());
        QCOMPARE(process.poll().Status, QtOperationProcess::State::Idle);
        QVERIFY(process.start(QCoreApplication::applicationFilePath(), "success"));
        QTRY_COMPARE(process.poll().Status, QtOperationProcess::State::Exited);
        QCOMPARE(process.poll().Code, quint32(0));
    }
    void crashIsNotAnOperationExit() {
        QtOperationProcess process;
        QVERIFY(process.start(QCoreApplication::applicationFilePath(), "crash"));
        QTRY_COMPARE(process.poll().Status, QtOperationProcess::State::Crashed);
        QVERIFY(process.poll().Code != 0);
    }
    void rejectedStartPreservesRunningChild() {
        QtOperationProcess process;
        QVERIFY(process.start(QCoreApplication::applicationFilePath(), "delayed"));
        const auto child = process.processId();
        QVERIFY(child != 0);
        const auto rejected = process.start(QCoreApplication::applicationFilePath(), "success");
        QVERIFY(!rejected);
        QVERIFY(!rejected.error().isEmpty());
        QCOMPARE(process.processId(), child);
        QTRY_COMPARE(process.poll().Status, QtOperationProcess::State::Exited);
        QCOMPARE(process.poll().Code, quint32(2));
    }
    void ownerExitDoesNotTerminateAcceptedWork() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString output = directory.filePath(QStringLiteral("итог 空 with spaces.txt"));
        QProcess parent;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("SEVENZIP_OPERATION_PROBE_OUTPUT", output);
        parent.setProcessEnvironment(environment);
        parent.start(QCoreApplication::applicationFilePath(), {"--probe-parent"});
        QVERIFY(parent.waitForStarted(5000));
        QVERIFY(parent.waitForFinished(5000));
        QCOMPARE(parent.exitStatus(), QProcess::NormalExit);
        QCOMPARE(parent.exitCode(), 0);
        QTRY_VERIFY(QFileInfo::exists(output));
        QFile file(output);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("completed after parent exit"));
    }
#ifndef Q_OS_WIN
    void discardedObserverReapsIndependentChild() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString output = directory.filePath(QStringLiteral("итог 空 with spaces.txt"));
        qputenv("SEVENZIP_OPERATION_PROBE_OUTPUT", output.toUtf8());
        const auto restore = qScopeGuard([] { qunsetenv("SEVENZIP_OPERATION_PROBE_OUTPUT"); });
        auto process = std::make_unique<QtOperationProcess>();
        QVERIFY(process->start(QCoreApplication::applicationFilePath(), "wait-release"));
        const pid_t child = pid_t(process->processId());
        QVERIFY(child > 0);
        const auto cleanup = qScopeGuard([&] { ::waitpid(child, nullptr, WNOHANG); });
        process.reset();
        QVERIFY(!QFileInfo::exists(output));
        QFile release(output + ".release");
        QVERIFY(release.open(QIODevice::WriteOnly));
        release.close();
        QTRY_VERIFY(QFileInfo::exists(output));
        QFile file(output);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("completed after parent exit"));
        // WNOWAIT observes ownership without stealing the exit from a reaper.
        const auto reaped = [child] {
            siginfo_t information{};
            return ::waitid(P_PID, id_t(child), &information, WEXITED | WNOHANG | WNOWAIT) < 0 &&
                   errno == ECHILD;
        };
        QTRY_VERIFY_WITH_TIMEOUT(reaped(), 3000);
    }
#endif
};

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    const auto arguments = application.arguments();
    if (arguments.contains("--probe-parent")) {
        QtOperationProcess operation;
        return operation.start(application.applicationFilePath(), "survive") ? 0 : 1;
    }
    const int option = arguments.indexOf("--operation-channel");
    if (option >= 0) {
        const QString mode = arguments.value(option + 1);
        if (mode == "delayed") {
            QTimer::singleShot(250, &application, [&] { application.exit(2); });
            return application.exec();
        }
        if (mode == "crash") {
#ifdef Q_OS_WIN
            TerminateProcess(GetCurrentProcess(), 0xc0000005u);
#else
            ::kill(::getpid(), SIGKILL);
#endif
            return 1;
        }
        if (mode == "survive" || mode == "wait-release") {
            const auto complete = [&] {
                QSaveFile output(qEnvironmentVariable("SEVENZIP_OPERATION_PROBE_OUTPUT"));
                if (!output.open(QIODevice::WriteOnly) ||
                    output.write("completed after parent exit") != 27 || !output.commit())
                    application.exit(1);
                else
                    application.exit(0);
            };
            QTimer release;
            if (mode == "survive")
                QTimer::singleShot(250, &application, complete);
            else {
                QObject::connect(&release, &QTimer::timeout, &application, [&] {
                    if (QFileInfo::exists(qEnvironmentVariable("SEVENZIP_OPERATION_PROBE_OUTPUT") +
                                          ".release"))
                        complete();
                });
                release.start(10);
                QTimer::singleShot(10000, &application, [&] { application.exit(1); });
            }
            return application.exec();
        }
        return mode == "success" ? 0 : mode == "cancelled" ? 255 : 2;
    }
    OperationProcessTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "operation_process_tests.moc"
