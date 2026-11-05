// Adapted from 7-Zip Panel.h (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#pragma once

#include "C/Threads.h"
#include "Common/MyWindows.h"
#include "Windows/Synchronization.h"
#include "common/collections/MyVector.h"
#include <atomic>

// Native launcher owner, scoped to this panel rather than a process-wide global.
struct CExitEventLauncher {
    std::atomic_bool _exiting{false};
    NWindows::NSynchronization::CManualResetEvent _exitEvent;
    unsigned _numActiveThreads;
    CRecordVector<::CThread> _threads;
    CExitEventLauncher() {
        if (_exitEvent.Create(false) != S_OK)
            throw 9387173;
        _numActiveThreads = 0;
    }
    ~CExitEventLauncher() { Exit(true); }
    void Exit(bool hardExit);
    bool Exiting() const { return _exiting.load(); }
};
