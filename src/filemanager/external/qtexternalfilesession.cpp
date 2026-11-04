// Adapted from 7-Zip CPP/7zip/UI/FileManager/PanelItemOpen.cpp (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Joined external-file launch, process discovery, changed-file update and shutdown.
#include "filemanager/external/qtexternalfilesession.h"
#include "7zip/UI/FileManager/PropertyNameRes.h"
#include "7zip/UI/FileManager/resource.h"
#include "Common/AutoPtr.h"
#include "Common/StringConvert.h"
#include "Windows/FileFind.h"
#include "archive/password/archivepassword.h"
#include "filemanager/FormatUtils.h"
#include "filemanager/external/qtexternalapplications.h"
#include "filemanager/panels/Panel.h"
#include "fileops/native/FileDir.h"
#include "localization/LangUtils.h"
#include "operations/qtexternalguicall.h"
#include "platform/process/qtexternalprocess.h"
#include "platform/temp/qtownedtempregistry.h"
#include <QElapsedTimer>
#include <QMessageBox>
#include <QWidget>
#include <chrono>
#include <memory>
#include <thread>
#include <utility>

using namespace NWindows;
using namespace NFile;
using namespace NDir;

bool IQtExternalFileServices::RunArchiveUpdate(CPanel &panel, const QtArchiveUpdateBody &body) {
    std::optional<CSelectedState> selection;
    const bool result = body(selection);
    if (selection)
        panel.RefreshListCtrl(*selection);
    return result;
}

void CExitEventLauncher::Exit(bool hardExit) {
    // Native event/owned-thread lifecycle. Always join on POSIX/Qt: detaching
    // after the source Windows 100 ms hard-exit timeout could outlive this owner.
    (void)hardExit;
    if (!_exiting.exchange(true))
        _exitEvent.Set();
    if (_numActiveThreads == 0)
        return;
    FOR_VECTOR(i, _threads) {
        ::CThread &th = _threads[i];
        if (Thread_WasCreated(&th)) {
            if (Thread_Wait_Close(&th) != 0)
                continue;
            _numActiveThreads--;
        }
    }
}

// Source child discovery and wait policy with scoped Qt process owners.
class CChildProcesses {
    struct CTrackedProcess {
        std::unique_ptr<IQtProcessHandle> Handle;
        bool NeedWait = true;
    };
    CRecordVector<UInt32> _ids;
    CObjectVector<CTrackedProcess> Entries;
    UString Path;

  public:
    enum class WaitStatus { Empty, ProcessExited, Stopped, Error };
    struct WaitResult {
        WaitStatus State;
        unsigned Count;
    };
    IQtProcessServices *Services = nullptr;
    ~CChildProcesses() { CloseAll(); }
    bool HasProcesses() const { return !Entries.IsEmpty(); }
    WaitResult WaitForExit(CExitEventLauncher &launcher);
    void CloseAll() {
        Entries.Clear();
        _ids.Clear();
    }
    void SetMainProcess(IQtProcessHandle *h) {
        CMyUniquePtr<IQtProcessHandle> owner(h);
        const UInt32 id = h->Id();
        if (id != 0)
            _ids.AddToUniqueSorted(id);
        Path = h->FileName();
        auto &entry = Entries.AddNew();
        entry.Handle.reset(owner.release());
    }
    void Update(bool needFindProcessByPath) {
        if (!Services)
            return;
        CObjectVector<CSnapshotProcess> sps;
        Services->GetSnapshot(sps, _ids);
        const int separ = Path.ReverseFind_PathSepar();
        const UString mainName = Path.Ptr((unsigned)(separ + 1));
        if (mainName.IsEmpty())
            needFindProcessByPath = false;
        const UInt32 currentProcessId = Services->CurrentProcessId();
        for (;;) {
            bool wasAdded = false;
            FOR_VECTOR(i, sps) {
                const CSnapshotProcess &sp = sps[i];
                const UInt32 id = sp.Id;
                if (id == currentProcessId)
                    continue;
                if (_ids.FindInSorted(id) >= 0)
                    continue;
                bool isSameName = false;
                const UString &name = sp.Name;
                if (needFindProcessByPath)
                    isSameName = mainName.IsEqualTo_NoCase(name);
                bool needAdd = false;
                if (isSameName || _ids.FindInSorted(sp.ParentId) >= 0)
                    needAdd = true;
                if (needAdd) {
                    CMyUniquePtr<IQtProcessHandle> owner(Services->OpenProcess(id));
                    IQtProcessHandle *hProcess = owner.get();
                    if (hProcess) {
                        _ids.AddToUniqueSorted(id);
                        auto &entry = Entries.AddNew();
                        entry.Handle.reset(owner.release());
                        wasAdded = true;
                    }
                }
            }
            if (!wasAdded)
                break;
        }
    }
};

CChildProcesses::WaitResult CChildProcesses::WaitForExit(CExitEventLauncher &launcher) {
    CUIntVector waiting;
    FOR_VECTOR(i, Entries) {
        if (waiting.Size() > 60)
            break;
        if (Entries[i].NeedWait)
            waiting.Add(i);
    }
    if (waiting.IsEmpty())
        return {WaitStatus::Empty, 0};
    for (;;) {
        if (launcher.Exiting())
            return {WaitStatus::Stopped, waiting.Size()};
        FOR_VECTOR(i, waiting) {
            auto &process = Entries[waiting[i]];
            const QtProcessStatus status = process.Handle->Status();
            if (status == QtProcessStatus::Error)
                return {WaitStatus::Error, waiting.Size()};
            if (status == QtProcessStatus::Exited) {
                process.NeedWait = false;
                return {WaitStatus::ProcessExited, waiting.Size()};
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
}

struct CTmpProcessInfo : public CTempFileInfo {
    CChildProcesses Processes;
    CExitEventLauncher *Launcher;
    CPanel *Window;
    QWidget *Parent;
    IQtExternalFileServices *ExternalServices;
    UString FullPathFolderPrefix;
    bool UsePassword;
    ArchivePassword Password;
    bool ReadOnly;
    bool Registered = false;
    CTmpProcessInfo() : UsePassword(false), ReadOnly(false) {}
    ~CTmpProcessInfo() {
        if (Registered)
            --Launcher->PendingSessions;
    }
};

static int CallExternalUpdateGui(CTmpProcessInfo &tpi, const std::function<int()> &body) {
    return QtExternalGuiCall(tpi.Parent, *tpi.Launcher, body,
                             [&] { return tpi.ExternalServices->CanHandleExternalUpdate(); });
}

static int ShowExternalUpdateMessage(CTmpProcessInfo &tpi, const UString &text,
                                     bool question = false) {
    return CallExternalUpdateGui(tpi, [&] {
        QMessageBox box(question ? QMessageBox::Question : QMessageBox::Critical,
                        QStringLiteral("7-Zip"), ToQString(text),
                        question ? QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel
                                 : QMessageBox::Ok,
                        tpi.Parent);
        box.setObjectName(question ? "externalUpdatePrompt" : "externalUpdateError");
        box.setTextFormat(Qt::PlainText);
        return box.exec();
    });
}

static bool UpdateChangedExternalFile(CTmpProcessInfo &tpi) {
    const UString error = MyFormatNew(IDS_CANNOT_UPDATE_FILE, fs2us(tpi.FilePath));
    if (tpi.ReadOnly) {
        UString message = error;
        message.Add_LF();
        AddLangString(message, IDS_PROP_READ_ONLY);
        message.Add_LF();
        message += tpi.FullPathFolderPrefix;
        ShowExternalUpdateMessage(tpi, message);
        return false;
    }
    const int answer = ShowExternalUpdateMessage(
        tpi, MyFormatNew(IDS_WANT_UPDATE_MODIFIED_FILE, tpi.RelPath), true);
    if (tpi.Launcher->Exiting())
        return false;
    if (answer != QMessageBox::Yes)
        return true;
    const int updated = CallExternalUpdateGui(tpi, [&] {
        return tpi.ExternalServices->RunArchiveUpdate(
                   *tpi.Window,
                   [&](std::optional<CSelectedState> &selection) {
                       return tpi.Window->OnOpenItemChanged(tpi, tpi.FullPathFolderPrefix,
                                                            tpi.UsePassword, tpi.Password.native(),
                                                            &selection);
                   })
                   ? 1
                   : 0;
    });
    if (updated == 1)
        return true;
    ShowExternalUpdateMessage(tpi, error);
    return false;
}

static THREAD_FUNC_DECL MyThreadFunction(void *param) try {
    CMyUniquePtr<CTmpProcessInfo> tpi((CTmpProcessInfo *)param);
    CChildProcesses &processes = tpi->Processes;
    const bool mainProcessWasSet = processes.HasProcesses();
    bool isComplexMode = true;
    if (mainProcessWasSet) {
        QElapsedTimer timer;
        timer.start();
        bool firstPass = true;
        for (;;) {
            const auto wait = processes.WaitForExit(*tpi->Launcher);
            bool needFindProcessByPath = false;
            if (wait.State == CChildProcesses::WaitStatus::Empty) {
                if (!firstPass)
                    break;
            } else {
                if (wait.State != CChildProcesses::WaitStatus::ProcessExited) {
                    const bool failed = wait.State == CChildProcesses::WaitStatus::Error;
                    processes.CloseAll();
                    return (THREAD_FUNC_RET_TYPE)(uintptr_t)failed;
                }
                if (firstPass && wait.Count == 1) {
                    const UInt32 curTime = UInt32(timer.elapsed());
                    needFindProcessByPath = (curTime < 2 * 1000);
                    if (needFindProcessByPath) {
                        NFind::CFileInfo newFileInfo;
                        if (newFileInfo.Find(tpi->FilePath))
                            if (tpi->WasChanged_from_TempFileInfo(newFileInfo))
                                needFindProcessByPath = false;
                    }
                }
            }
            firstPass = false;
            processes.Update(needFindProcessByPath);
        }
        const UInt32 curTime = UInt32(timer.elapsed());
        processes.CloseAll();
        // The native wait/snapshot loop has proved all known children finished.
        if (tpi->OwnedTempLease)
            tpi->OwnedTempLease->ExternalFinished();
        isComplexMode = (curTime < 2 * 1000);
    }
    {
        NFind::CFileInfo newFileInfo;
        if (newFileInfo.Find(tpi->FilePath)) {
            if (mainProcessWasSet && tpi->WasChanged_from_TempFileInfo(newFileInfo)) {
                if (!UpdateChangedExternalFile(*tpi))
                    return 0;
                tpi->DeleteDirAndFile();
                return 0;
            }
            if ((!mainProcessWasSet || isComplexMode) && tpi->Launcher->_exitEvent.Lock() != 0)
                return (THREAD_FUNC_RET_TYPE)(uintptr_t)1;
        }
    }
    {
        NFind::CFileInfo newFileInfo;
        const bool finded = newFileInfo.Find(tpi->FilePath);
        if (!finded || !tpi->WasChanged_from_TempFileInfo(newFileInfo))
            tpi->DeleteDirAndFile();
    }
    return 0;
} catch (...) {
    // A failed snapshot/registration must not escape the native thread entry.
    // The record's owners unwind; an unproven external lifetime keeps its copy.
    return (THREAD_FUNC_RET_TYPE)(uintptr_t)1;
}

namespace {
// CTempDir owns cleanup until launch establishes an external lifetime. A
// returned handle can establish that lifetime even when the service throws.
class CExternalFileLaunch MY_UNCOPYABLE {
    CTempDir &_directory;
    CMyUniquePtr<CTmpProcessInfo> &_info;
    IQtProcessHandle *_process = nullptr;

  public:
    CExternalFileLaunch(CTempDir &directory, CMyUniquePtr<CTmpProcessInfo> &info)
        : _directory(directory), _info(info) {}
    ~CExternalFileLaunch() {
        if (_process) {
            _directory.DisableDeleting();
            delete _process;
        }
    }
    void Start(const std::function<HRESULT(IQtProcessHandle *&)> &start) {
        const HRESULT result = start(_process);
        // The native caller accepts a valid handle even on launch error.
        if (!_process && result != S_OK)
            return;
        // Both known processes and successful no-handle associations retain
        // the copy if registration or thread creation subsequently fails.
        _directory.DisableDeleting();
        _info->Processes.Services = _info->ExternalServices->ProcessServices();
        if (_process)
            _info->Processes.SetMainProcess(std::exchange(_process, nullptr));
        // The worker can finish and delete its record immediately after Create.
        auto &launcher = *_info->Launcher;
        ::CThread thread;
        Thread_CONSTRUCT(&thread);
        _info->Registered = true;
        ++launcher.PendingSessions;
        if (Thread_Create(&thread, MyThreadFunction, _info.get()) != 0)
            throw 271824;
        launcher._threads.AddInReserved(thread);
        launcher._numActiveThreads++;
        _info.release();
    }
};
} // namespace

void CPanel::StartExternalFileSession(CTempDir &tempDirectory, const CTempFileInfo &tempFileInfo,
                                      const FString &tempDirNorm, bool usePassword,
                                      const UString &password, bool editMode, bool useEditor) {
    const FString &tempFilePath = tempFileInfo.FilePath;
    CMyUniquePtr<CTmpProcessInfo> tpi(new CTmpProcessInfo());
    static_cast<CTempFileInfo &>(*tpi) = tempFileInfo;
    tpi->UsePassword = usePassword;
    tpi->Password = ArchivePassword(password);
    tpi->ReadOnly = IsThereReadOnlyFolder();
    if (IsHashFolder())
        tpi->ReadOnly = true;
    if (!tpi->FileInfo.Find(tempFilePath))
        return;
    if (!_exitEventLauncher)
        _exitEventLauncher = std::make_unique<CExitEventLauncher>();
    // Allocate tracking before launch/thread creation; Add must not throw while
    // the worker already owns this temp record.
    _exitEventLauncher->_threads.ReserveOnePosition();
    // A no-handle association or shutdown during a known process must never
    // turn an unproven external lifetime into an inactive cleanup candidate.
    if (tpi->OwnedTempLease && !tpi->OwnedTempLease->ProtectExternal()) {
        MessageBox_LastError();
        return;
    }
    // Prepare allocating tracking metadata before an external program can own
    // the copy. The process output remains guarded even inside launch errors.
    tpi->FullPathFolderPrefix = _currentFolderPrefix;
    tpi->Window = this;
    tpi->Parent = GetParent();
    tpi->ExternalServices = _externalFileServices;
    tpi->Launcher = _exitEventLauncher.get();
    CExternalFileLaunch(tempDirectory, tpi).Start([&](IQtProcessHandle *&process) {
        return editMode ? StartEditApplication(fs2us(tempFilePath), useEditor, GetParent(),
                                               *_externalFileServices, _settingsPath, &process)
                        : _externalFileServices->StartApplicationKnown(
                              fs2us(tempDirNorm), fs2us(tempFilePath), GetParent(), process);
    });
}
