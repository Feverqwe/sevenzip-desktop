#pragma once
#include "Common/MyString.h"
#include "common/collections/MyVector.h"
#include <functional>
#ifdef _WIN32
namespace NWindows {
class CProcess;
}
#endif

// CSnapshotProcess copied from 7-Zip PanelItemOpen.cpp, revision
// 0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17; Igor Pavlov, LGPL-2.1-or-later.
// The remaining interfaces are local platform services.
struct CSnapshotProcess {
    UInt32 Id;
    UInt32 ParentId;
    UString Name;
};

enum class QtProcessStatus { Running, Exited, Error };

class IQtProcessHandle {
  public:
    virtual ~IQtProcessHandle() = default;
    virtual UInt32 Id() const = 0;
    virtual UString FileName() const = 0;
    virtual QtProcessStatus Status() = 0;
};

class IQtProcessServices {
  public:
    virtual ~IQtProcessServices() = default;
    virtual void GetSnapshot(CObjectVector<CSnapshotProcess> &items) = 0;
    virtual void GetSnapshot(CObjectVector<CSnapshotProcess> &items,
                             const CRecordVector<UInt32> &) {
        GetSnapshot(items);
    }
    virtual IQtProcessHandle *OpenProcess(UInt32 id) = 0;
    virtual UInt32 CurrentProcessId() const = 0;
};

// Only OS process operations live here; child discovery/wait policy stays in
// the copied CChildProcesses and MyThreadFunction bodies.
class QtNativeProcessServices final : public IQtProcessServices {
  public:
    void GetSnapshot(CObjectVector<CSnapshotProcess> &items) override;
    void GetSnapshot(CObjectVector<CSnapshotProcess> &items,
                     const CRecordVector<UInt32> &roots) override;
    IQtProcessHandle *OpenProcess(UInt32 id) override;
    UInt32 CurrentProcessId() const override;
#ifdef _WIN32
    IQtProcessHandle *Adopt(NWindows::CProcess &process);
    HRESULT StartNative(const std::function<HRESULT(NWindows::CProcess &)> &start,
                        IQtProcessHandle *&process);
#endif
    HRESULT Create(const UString &program, const UString &parameters, const UStringVector &paths,
                   IQtProcessHandle *&process);
};
