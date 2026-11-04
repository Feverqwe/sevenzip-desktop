#include "Windows/ProcessUtils.h"
#include "platform/process/qtexternalprocess.h"
#include "settings/ViewSettings.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QVector>
#include <TlHelp32.h>
#include <memory>
#include <optional>

namespace {
UString NativeParameters(const UString &parameters, const UStringVector &paths) {
    UString result = parameters;
    FOR_VECTOR(i, paths) {
        if (!result.IsEmpty() && result.Back() != ' ')
            result.Add_Space();
        result.Add_Char('"');
        result += paths[i];
        result.Add_Char('"');
    }
    return result;
}

std::optional<FILETIME> ProcessCreationTime(UInt32 id) {
    NWindows::CProcess process;
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!process.Open(PROCESS_QUERY_LIMITED_INFORMATION, false, id) ||
        !process.GetTimes(&created, &exited, &kernel, &user))
        return std::nullopt;
    return created;
}

class CWinProcessHandle final : public IQtProcessHandle {
    NWindows::CProcess _process;

  public:
    explicit CWinProcessHandle(HANDLE handle) { _process.Attach(handle); }
    void Attach(HANDLE handle) { _process.Attach(handle); }
    UInt32 Id() const override { return GetProcessId(const_cast<NWindows::CProcess &>(_process)); }
    UString FileName() const override {
        // Resolve the API without changing the SDK target inherited by source
        // modules. This service needs the DOS path for retained-file comparison.
        using QueryName = BOOL(WINAPI *)(HANDLE, DWORD, LPWSTR, PDWORD);
        const auto queryName = Z7_GET_PROC_ADDRESS(QueryName, ::GetModuleHandleA("kernel32.dll"),
                                                   "QueryFullProcessImageNameW");
        wchar_t name[32768];
        DWORD size = 32768;
        if (!queryName || !queryName(const_cast<NWindows::CProcess &>(_process), 0, name, &size))
            return UString();
        UString result;
        result.SetFrom(name, size);
        return result;
    }
    QtProcessStatus Status() override {
        const DWORD result = WaitForSingleObject(_process, 0);
        if (result == WAIT_TIMEOUT)
            return QtProcessStatus::Running;
        return result == WAIT_OBJECT_0 ? QtProcessStatus::Exited : QtProcessStatus::Error;
    }
};
} // namespace

UInt32 QtNativeProcessServices::CurrentProcessId() const { return GetCurrentProcessId(); }

void QtNativeProcessServices::GetSnapshot(CObjectVector<CSnapshotProcess> &items) {
    const CRecordVector<UInt32> roots;
    GetSnapshot(items, roots);
}

void QtNativeProcessServices::GetSnapshot(CObjectVector<CSnapshotProcess> &items,
                                          const CRecordVector<UInt32> &roots) {
    items.Clear();
    const HANDLE snapshotHandle = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshotHandle == INVALID_HANDLE_VALUE)
        return;
    NWindows::CHandle snapshot;
    snapshot.Attach(snapshotHandle);
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL found = Process32FirstW(snapshot, &entry); found;
         found = Process32NextW(snapshot, &entry)) {
        CSnapshotProcess item;
        item.Id = entry.th32ProcessID;
        item.ParentId = entry.th32ParentProcessID;
        item.Name = entry.szExeFile;
        items.Add(item);
    }
    snapshot.Close();
    // Toolhelp retains the creator's PID after it exits. A recycled PID can
    // otherwise make an unrelated older process look like an editor child.
    QHash<UInt32, std::optional<FILETIME>> creationTimes;
    const auto creationTime = [&creationTimes](UInt32 id) {
        auto found = creationTimes.constFind(id);
        if (found == creationTimes.cend())
            found = creationTimes.insert(id, ProcessCreationTime(id));
        return found.value();
    };
    FOR_VECTOR(i, items) {
        CSnapshotProcess &item = items[i];
        if (!item.ParentId)
            continue;
        const auto child = creationTime(item.Id);
        const auto parent = creationTime(item.ParentId);
        if (child && parent && CompareFileTime(&*child, &*parent) < 0)
            item.ParentId = 0;
    }
}

IQtProcessHandle *QtNativeProcessServices::OpenProcess(UInt32 id) {
    NWindows::CProcess process;
    if (!process.Open(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, false, id))
        return nullptr;
    return Adopt(process);
}

IQtProcessHandle *QtNativeProcessServices::Adopt(NWindows::CProcess &process) {
    return (HANDLE)process ? new CWinProcessHandle(process.Detach()) : nullptr;
}

HRESULT
QtNativeProcessServices::StartNative(const std::function<HRESULT(NWindows::CProcess &)> &start,
                                     IQtProcessHandle *&process) {
    process = nullptr;
    auto owner = std::make_unique<CWinProcessHandle>(nullptr);
    NWindows::CProcess native;
    HRESULT result;
    try {
        result = start(native);
    } catch (...) {
        result = E_OUTOFMEMORY;
    }
    // Preserve a launched process even if its source launcher reported failure.
    if ((HANDLE)native) {
        owner->Attach(native.Detach());
        process = owner.release();
    }
    return result;
}

HRESULT QtNativeProcessServices::Create(const UString &program, const UString &parameters,
                                        const UStringVector &paths, IQtProcessHandle *&process) {
    process = nullptr;
    if (program.IsEmpty())
        return E_FAIL;
    const UString param = NativeParameters(parameters, paths);
    return StartNative(
        [&](NWindows::CProcess &native) {
            const WRes result = native.Create(program, param, nullptr);
            return result == 0 ? S_OK : HRESULT_FROM_WIN32(result);
        },
        process);
    return S_OK;
}

HRESULT QtNativeProcessServices::CreateDetached(const UString &program, const UString &parameters,
                                                const UStringVector &paths) {
    QProcess process;
    process.setProgram(ToQString(program));
    // Preserve original CreateProcess parameter-string interpretation on Windows.
    process.setNativeArguments(ToQString(NativeParameters(parameters, paths)));
    return process.startDetached() ? S_OK : E_FAIL;
}
