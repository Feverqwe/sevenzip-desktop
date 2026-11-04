#pragma once
#include <QByteArray>
#include <QList>
#include <QString>
#include <functional>
#include <memory>

struct SmbTreeEntry {
    quint64 Id = 0, Parent = 0, Size = 0, Modified = 0;
    QString Name;
    bool Directory = false;
    bool ChildrenLoaded = true;
    QString SymlinkTarget = {};
};
struct SmbTreeRead {
    quint32 Status = 0;
    QByteArray Data;
};
struct SmbTreeDirectory {
    quint32 Status = 0;
    QList<SmbTreeEntry> Entries;
};

// Filesystem semantics and lifecycle around the bundled protocol library.
// One instance per process: the library's global context registry cannot share
// an event loop with another server/client. requestStop is controller-safe.
class SmbReadOnlyServer final {
  public:
    using Reader = std::function<SmbTreeRead(quint64, quint64, quint32)>;
    using ReadCompletion = std::function<void(SmbTreeRead)>;
    using AsyncReader = std::function<void(quint64, quint64, quint32, ReadCompletion)>;
    using DirectoryCompletion = std::function<void(SmbTreeDirectory)>;
    using AsyncDirectoryReader = std::function<void(quint64, DirectoryCompletion)>;
    SmbReadOnlyServer(QList<SmbTreeEntry> entries, QString user, QString password, Reader reader,
                      QString share = QStringLiteral("Sevenzip"));
    ~SmbReadOnlyServer();
    bool validate(QString &error);
    int serve(quint16 port, std::function<void(quint16)> listening);
    void requestStop();
    void setDiagnosticHandler(std::function<void(QString)> handler);
    void setAsyncReader(AsyncReader reader);
    void setAsyncDirectoryReader(AsyncDirectoryReader reader);

  private:
    struct State;
    std::unique_ptr<State> state_;
};
