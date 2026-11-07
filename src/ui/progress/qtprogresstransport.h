// Narrow capabilities for source dialogs whose worker uses an external engine.
#pragma once
#include "Common/MyWindows.h"
#include <memory>

class CProgressDialog;
class IQtProgressTransport {
  public:
    virtual ~IQtProgressTransport() = default;
    virtual HRESULT SetPaused(bool paused) = 0;
    virtual bool CanPause() const { return true; }
    // An active backend phase can have unknown progress while the surrounding
    // native worker still retains counters from its preceding phase.
    virtual bool UsesPercentProgress() const { return false; }
    virtual void Cancel() {}
};

class IQtProgressServices {
  public:
    virtual ~IQtProgressServices() = default;
    virtual std::unique_ptr<IQtProgressTransport>
    CreateProgressTransport(CProgressDialog &dialog) = 0;
};
