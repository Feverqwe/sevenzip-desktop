#include "support/private_bus.h"
#include "ui/progress/qtlauncherprogress.h"
#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QScopeGuard>
#include <QThread>
#include <QtTest>

namespace {
const QString kInterface = QStringLiteral("com.canonical.Unity.LauncherEntry");
struct CUpdate {
    QString Uri;
    QVariantMap Properties;
    QString Path;
};
} // namespace

class CLauncherObserver : public QObject {
    Q_OBJECT
  public:
    QList<CUpdate> Updates;
  public slots:
    void Receive(const QString &uri, const QVariantMap &properties, const QDBusMessage &message) {
        Updates.append({uri, properties, message.path()});
    }
};

class LauncherProgressTests : public QObject {
    Q_OBJECT
    static QDBusMessage Query(const QDBusConnection &observer, const QString &source,
                              const QString &path) {
        QDBusMessage request = QDBusMessage::createMethodCall(source, path, kInterface, "Query");
        QDBusPendingCallWatcher watcher(observer.asyncCall(request));
        QSignalSpy finished(&watcher, &QDBusPendingCallWatcher::finished);
        if (!watcher.isFinished())
            finished.wait(3000);
        if (!watcher.isFinished())
            return QDBusMessage::createError(QDBusError::Timeout, "Private bus Query timed out");
        return watcher.reply();
    }
    static bool Subscribe(QDBusConnection connection, const QString &source,
                          CLauncherObserver &observer) {
        return connection.connect(source, QString(), kInterface, "Update", &observer,
                                  SLOT(Receive(QString, QVariantMap, QDBusMessage)));
    }

  private slots:
#if defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
    void privateBusesRemainIsolatedWithLongTemporaryPath() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString path = fixture.filePath(QString(120, 't'));
        QVERIFY(QDir().mkpath(path));
        const QByteArray previous = qgetenv("TMPDIR");
        const bool defined = qEnvironmentVariableIsSet("TMPDIR");
        const auto restore = qScopeGuard([&] {
            if (defined)
                qputenv("TMPDIR", previous);
            else
                qunsetenv("TMPDIR");
        });
        qputenv("TMPDIR", path.toUtf8());
        CPrivateBus first, second;
        QVERIFY(first.Start());
        QVERIFY(second.Start());
        QVERIFY(first.Address != second.Address);
        auto a = first.Connect();
        auto b = second.Connect();
        QVERIFY(a.isConnected() && b.isConnected());
        QVERIFY(a.registerService("org.sevenzip.PrivateFixture"));
        QVERIFY(b.registerService("org.sevenzip.PrivateFixture"));
    }
#endif
    void nativeUpdateQueryReplayAndDestructorClear() {
        CPrivateBus bus;
        QVERIFY(bus.Start());
        const QDBusConnection source = bus.Connect();
        QDBusConnection listener = bus.Connect();
        QVERIFY(source.isConnected());
        QVERIFY(listener.isConnected());
        CLauncherObserver observer;
        QVERIFY(Subscribe(listener, source.baseService(), observer));
        QString path;
        {
            CQtLauncherProgress progress(source, QStringLiteral("семь пробел.desktop"));
            QCOMPARE(progress.HrInit(), S_OK);
            QTRY_COMPARE(observer.Updates.size(), 1);
            path = observer.Updates.last().Path;
            QCOMPARE(observer.Updates.last().Uri,
                     QStringLiteral("application://семь пробел.desktop"));
            QCOMPARE(observer.Updates.last().Properties.value("progress").toDouble(), 0.0);
            QVERIFY(!observer.Updates.last().Properties.value("progress-visible").toBool());
            QCOMPARE(progress.HrInit(), S_OK);
            QCOMPARE(progress.SetProgressValue(1, 4), S_OK);
            QTRY_COMPARE(observer.Updates.size(), 2);
            QCOMPARE(observer.Updates.last().Properties.value("progress").toDouble(), 0.25);
            QCOMPARE(observer.Updates.last().Properties.value("progress").metaType(),
                     QMetaType::fromType<double>());
            QCOMPARE(progress.SetProgressState(TBPF_ERROR), S_OK);
            QTRY_COMPARE(observer.Updates.size(), 3);
            QVERIFY(observer.Updates.last().Properties.value("urgent").toBool());
            // State Update carries only changed properties, Query the complete entry.
            QVERIFY(!observer.Updates.last().Properties.contains("progress"));
            const QDBusMessage reply = Query(listener, source.baseService(), path);
            QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
            QCOMPARE(reply.arguments().size(), 2);
            QCOMPARE(reply.arguments()[0].toString(),
                     QStringLiteral("application://семь пробел.desktop"));
            const auto properties = qdbus_cast<QVariantMap>(reply.arguments()[1]);
            QCOMPARE(properties.value("progress").toDouble(), 0.25);
            QVERIFY(properties.value("urgent").toBool());
            QVERIFY(properties.value("progress-visible").toBool());
            QVERIFY(listener.registerService("com.canonical.Unity"));
            QTRY_COMPARE(observer.Updates.size(), 4);
            QCOMPARE(observer.Updates.last().Properties, properties);
            QVERIFY(listener.unregisterService("com.canonical.Unity"));
            QVERIFY(listener.registerService("com.canonical.Unity"));
            QTRY_COMPARE(observer.Updates.size(), 5);
            QCOMPARE(observer.Updates.last().Properties, properties);
            QCOMPARE(progress.SetProgressValue(~UInt64(0), ~UInt64(0)), S_OK);
            QTRY_COMPARE(observer.Updates.size(), 6);
            QCOMPARE(observer.Updates.last().Properties.value("progress").toDouble(), 1.0);
            QVERIFY(!observer.Updates.last().Properties.contains("urgent"));
            const auto completed = Query(listener, source.baseService(), path);
            QCOMPARE(completed.type(), QDBusMessage::ReplyMessage);
            QVERIFY(qdbus_cast<QVariantMap>(completed.arguments()[1]).value("urgent").toBool());
            QCOMPARE(progress.SetProgressState(TBPF_PAUSED), S_OK);
            QTRY_COMPARE(observer.Updates.size(), 7);
            QVERIFY(!observer.Updates.last().Properties.value("urgent").toBool());
            QVERIFY(!observer.Updates.last().Properties.contains("progress-visible"));
            QCOMPARE(progress.SetProgressValue(100, 4), S_OK);
            const auto clamped = Query(listener, source.baseService(), path);
            QCOMPARE(clamped.type(), QDBusMessage::ReplyMessage);
            QCOMPARE(qdbus_cast<QVariantMap>(clamped.arguments()[1]).value("progress").toDouble(),
                     1.0);
            QCOMPARE(observer.Updates.size(), 7);
        }
        QTRY_COMPARE(observer.Updates.size(), 8);
        QVERIFY(!observer.Updates.last().Properties.value("progress-visible").toBool());
        QVERIFY(!observer.Updates.last().Properties.value("urgent").toBool());
        QCOMPARE(Query(listener, source.baseService(), path).type(), QDBusMessage::ErrorMessage);
    }
    void invalidUnavailableAndDisconnectedPreservePublishedState() {
        CQtLauncherProgress absent(QDBusConnection("not-connected"), "sevenzip.desktop");
        QVERIFY(absent.HrInit() != S_OK);
        QVERIFY(absent.SetProgressValue(1, 2) != S_OK);
        CPrivateBus bus;
        QVERIFY(bus.Start());
        const QDBusConnection source = bus.Connect();
        const QDBusConnection listener = bus.Connect();
        QVERIFY(source.isConnected());
        CLauncherObserver observer;
        QVERIFY(Subscribe(listener, source.baseService(), observer));
        for (const auto &invalid :
             {"", "not-a-desktop-id", "../evil.desktop", "/evil.desktop", "folder\\evil.desktop"}) {
            CQtLauncherProgress progress(source, QString::fromLatin1(invalid));
            QCOMPARE(progress.HrInit(), E_INVALIDARG);
        }
        CQtLauncherProgress progress(source, "sevenzip.desktop");
        QVERIFY(progress.SetProgressState(TBPF_NORMAL) != S_OK);
        QCOMPARE(progress.HrInit(), S_OK);
        QTRY_COMPARE(observer.Updates.size(), 1);
        QCOMPARE(progress.SetProgressState(TBPF_NORMAL), S_OK);
        QTRY_COMPARE(observer.Updates.size(), 2);
        const QString path = observer.Updates.last().Path;
        QCOMPARE(progress.SetProgressValue(0, 4), S_OK);
        QCOMPARE(progress.SetProgressValue(1, 0), E_INVALIDARG);
        QVERIFY(progress.SetProgressState(TBPF_INDETERMINATE) != S_OK);
        QCOMPARE(progress.SetProgressState(TBPFLAG(TBPF_INDETERMINATE | TBPF_NORMAL)),
                 E_INVALIDARG);
        const QDBusMessage reply = Query(listener, source.baseService(), path);
        QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
        const auto properties = qdbus_cast<QVariantMap>(reply.arguments()[1]);
        QCOMPARE(properties.value("progress").toDouble(), 0.0);
        QVERIFY(properties.value("progress-visible").toBool());
        QVERIFY(!properties.value("urgent").toBool());
        QCOMPARE(observer.Updates.size(), 2);
        HRESULT initialize = S_OK, state = S_OK, value = S_OK;
        QThread *worker = QThread::create([&] {
            initialize = progress.HrInit();
            state = progress.SetProgressState(TBPF_ERROR);
            value = progress.SetProgressValue(1, 2);
        });
        worker->start();
        worker->wait();
        delete worker;
        QVERIFY(initialize != S_OK);
        QVERIFY(state != S_OK);
        QVERIFY(value != S_OK);
        const auto preserved = Query(listener, source.baseService(), path);
        QCOMPARE(preserved.type(), QDBusMessage::ReplyMessage);
        QCOMPARE(qdbus_cast<QVariantMap>(preserved.arguments()[1]), properties);
        QCOMPARE(observer.Updates.size(), 2);
        QCOMPARE(progress.SetProgressState(TBPF_NOPROGRESS), S_OK);
        QTRY_COMPARE(observer.Updates.size(), 3);
        QVERIFY(!observer.Updates.last().Properties.value("progress-visible").toBool());
        bus.Stop();
        QTRY_VERIFY(!source.isConnected());
        QVERIFY(progress.HrInit() != S_OK);
        QVERIFY(progress.SetProgressState(TBPF_NORMAL) != S_OK);
        QVERIFY(progress.SetProgressValue(1, 2) != S_OK);
    }
};

QTEST_GUILESS_MAIN(LauncherProgressTests)
#include "launcher_progress_tests.moc"
