#include "platform/process/qtchildprocessreaper.h"
#include "platform/process/qtexternalprocess.h"
#include "settings/ViewSettings.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QVector>
#include <cerrno>
#include <memory>
#include <optional>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef Q_OS_MACOS
#include <crt_externs.h>
#include <libproc.h>
#include <sys/proc.h>
#else
extern char **environ;
#endif

namespace {
QStringList ProcessArguments(const UString &parameters, const UStringVector &paths) {
    QStringList arguments = QProcess::splitCommand(ToQString(parameters));
    FOR_VECTOR(i, paths) arguments.append(ToQString(paths[i]));
    return arguments;
}

QtProcessStatus ReadProcessRecord(UInt32 id, CSnapshotProcess &record, QByteArray &identity) {
#ifdef Q_OS_MACOS
    proc_bsdinfo info{};
    errno = 0;
    if (proc_pidinfo(int(id), PROC_PIDTBSDINFO, 0, &info, sizeof(info)) != sizeof(info))
        return errno == ESRCH || errno == ENOENT ? QtProcessStatus::Exited : QtProcessStatus::Error;
    if (info.pbi_status == SZOMB)
        return QtProcessStatus::Exited;
    record.Id = id;
    record.ParentId = info.pbi_ppid;
    char path[PROC_PIDPATHINFO_MAXSIZE];
    const QString name = proc_pidpath(int(id), path, sizeof(path)) > 0
                             ? QFileInfo(QFile::decodeName(path)).fileName()
                             : QString::fromLocal8Bit(info.pbi_name);
    record.Name = ToUString(name);
    identity =
        QByteArray::number(info.pbi_start_tvsec) + ':' + QByteArray::number(info.pbi_start_tvusec);
#else
    QFile file(QString("/proc/%1/stat").arg(id));
    errno = 0;
    if (!file.open(QIODevice::ReadOnly))
        return errno == ENOENT || errno == ESRCH ? QtProcessStatus::Exited : QtProcessStatus::Error;
    const QByteArray bytes = file.readAll();
    const qsizetype endName = bytes.lastIndexOf(") ");
    const qsizetype startName = bytes.indexOf('(');
    if (endName < 0 || startName < 0)
        return QtProcessStatus::Error;
    const QList<QByteArray> fields = bytes.mid(endName + 2).split(' ');
    if (fields.size() < 20)
        return QtProcessStatus::Error;
    if (fields[0] == "Z" || fields[0] == "X")
        return QtProcessStatus::Exited;
    record.Id = id;
    record.ParentId = fields[1].toUInt();
    const QString executable = QFileInfo(QString("/proc/%1/exe").arg(id)).symLinkTarget();
    record.Name = ToUString(
        executable.isEmpty() ? QFile::decodeName(bytes.mid(startName + 1, endName - startName - 1))
                             : QFileInfo(executable).fileName());
    identity = fields[19];
#endif
    return QtProcessStatus::Running;
}

class CPidProcessHandle final : public IQtProcessHandle {
    UInt32 _id = 0;
    UString _path;
    QByteArray _identity;
    bool _finished = false;
    QtChildProcessReaper _reaper;

  public:
    void PrepareReaper() { _reaper.Prepare(); }
    void Attach(UInt32 id, const UString &path, const QByteArray &identity) {
        _id = id;
        _path = path;
        _identity = identity;
    }
    void Own(UInt32 id) noexcept {
        _id = id;
        _reaper.Adopt(pid_t(id));
    }
    UInt32 Id() const override { return _id; }
    UString FileName() const override { return _path; }
    QtProcessStatus Status() override {
        if (_finished)
            return QtProcessStatus::Exited;
        if (_reaper.Id() > 0) {
            const pid_t result = waitpid(pid_t(_id), nullptr, WNOHANG);
            if (result == 0 || (result < 0 && errno == EINTR))
                return QtProcessStatus::Running;
            if (result == pid_t(_id)) {
                _reaper.Reaped();
                _finished = true;
                return QtProcessStatus::Exited;
            }
            if (errno != ECHILD)
                return QtProcessStatus::Error;
            _reaper.Reaped();
        }
        CSnapshotProcess record;
        QByteArray identity;
        if (_identity.isEmpty())
            return QtProcessStatus::Error;
        const QtProcessStatus status = ReadProcessRecord(_id, record, identity);
        if (status != QtProcessStatus::Running)
            return status;
        return identity == _identity ? QtProcessStatus::Running : QtProcessStatus::Exited;
    }
};
} // namespace

UInt32 QtNativeProcessServices::CurrentProcessId() const { return UInt32(getpid()); }

void QtNativeProcessServices::GetSnapshot(CObjectVector<CSnapshotProcess> &items) {
    const CRecordVector<UInt32> roots;
    GetSnapshot(items, roots);
}

void QtNativeProcessServices::GetSnapshot(CObjectVector<CSnapshotProcess> &items,
                                          const CRecordVector<UInt32> &roots) {
    items.Clear();
    QVector<UInt32> ids;
#ifdef Q_OS_MACOS
    const int count = proc_listallpids(nullptr, 0);
    if (count <= 0)
        return;
    QVector<int> pids(count * 2 + 32);
    const int received = proc_listallpids(pids.data(), int(pids.size() * sizeof(int)));
    for (int i = 0; i < qMin(received, int(pids.size())); ++i)
        if (pids[i] > 0)
            ids.append(UInt32(pids[i]));
#else
    const QStringList names = QDir("/proc").entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &name : names) {
        bool valid = false;
        const UInt32 id = name.toUInt(&valid);
        if (valid && id != 0)
            ids.append(id);
    }
#endif
    for (UInt32 id : ids) {
        CSnapshotProcess item;
        QByteArray identity;
        if (ReadProcessRecord(id, item, identity) == QtProcessStatus::Running) {
            // Win32 keeps an exited parent's PID in the snapshot. POSIX
            // reparents orphans; a fresh launch group retains their lineage.
            const pid_t group = getpgid(pid_t(id));
            if (group > 0 && UInt32(group) != id && roots.FindInSorted(UInt32(group)) >= 0)
                item.ParentId = UInt32(group);
            items.Add(item);
        }
    }
}

IQtProcessHandle *QtNativeProcessServices::OpenProcess(UInt32 id) {
    CSnapshotProcess item;
    QByteArray identity;
    if (ReadProcessRecord(id, item, identity) != QtProcessStatus::Running)
        return nullptr;
    auto handle = std::make_unique<CPidProcessHandle>();
    handle->Attach(id, item.Name, identity);
    return handle.release();
}

HRESULT QtNativeProcessServices::Create(const UString &program, const UString &parameters,
                                        const UStringVector &paths, IQtProcessHandle *&process) {
    process = nullptr;
    if (program.IsEmpty())
        return E_FAIL;
    auto handle = std::make_unique<CPidProcessHandle>();
    handle->Attach(0, program, QByteArray());
    handle->PrepareReaper();
    QList<QByteArray> bytes;
    bytes.append(QFile::encodeName(ToQString(program)));
    for (const QString &arg : ProcessArguments(parameters, paths))
        bytes.append(QFile::encodeName(arg));
    QVector<char *> arguments;
    for (QByteArray &arg : bytes)
        arguments.append(arg.data());
    arguments.append(nullptr);
    pid_t pid = 0;
    posix_spawnattr_t attributes;
    if (posix_spawnattr_init(&attributes) != 0)
        return E_FAIL;
    int setup = posix_spawnattr_setpgroup(&attributes, 0);
    if (setup == 0)
        setup = posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
    if (setup != 0) {
        posix_spawnattr_destroy(&attributes);
        return HRESULT_FROM_WIN32(setup);
    }
#ifdef Q_OS_MACOS
    char **environment = *_NSGetEnviron();
#else
    char **environment = environ;
#endif
    const int result = posix_spawnp(&pid, bytes[0].constData(), nullptr, &attributes,
                                    arguments.data(), environment);
    posix_spawnattr_destroy(&attributes);
    if (result != 0)
        return HRESULT_FROM_WIN32(result);
    // Retain the launched child before reading allocating metadata. A partial
    // failure still returns its owner so the caller preserves the edited file.
    handle->Own(UInt32(pid));
    try {
        CSnapshotProcess item;
        QByteArray identity;
        ReadProcessRecord(UInt32(pid), item, identity);
        handle->Attach(UInt32(pid), program, identity);
    } catch (...) {
        process = handle.release();
        return E_OUTOFMEMORY;
    }
    process = handle.release();
    return S_OK;
}
