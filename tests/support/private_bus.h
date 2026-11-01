#pragma once
#include <QDBusConnection>
#include <QProcess>
#include <QTemporaryDir>

// Owns a bounded local bus fixture; never connects to the user's session bus.
class CPrivateBus {
    QProcess _daemon;
    QStringList _names;
#ifdef Q_OS_MACOS
    // A branch's TMPDIR can exceed the Unix socket limit and contain D-Bus
    // address metacharacters. Keep this private fixture's socket path short.
    QTemporaryDir _directory{QStringLiteral("/tmp/sevenzip-bus-XXXXXX")};
#else
    QTemporaryDir _directory;
#endif

  public:
    QString Address;
    bool Start();
    QDBusConnection Connect();
    void Stop();
    ~CPrivateBus();
};
