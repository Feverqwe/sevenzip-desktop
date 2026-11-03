#pragma once
#include "7zip/IProgress.h"
#include "Common/MyCom.h"
#include <functional>

// Test-side adapter for the native deletion progress contract.
class FileDeleteProgress final : public IProgress, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_1(IProgress)
  public:
    std::function<bool(UInt64)> Total;
    std::function<bool(UInt64)> Completed;
    Z7_COM7F_IMF(SetTotal(UInt64 total)) override {
        return !Total || Total(total) ? S_OK : E_ABORT;
    }
    Z7_COM7F_IMF(SetCompleted(const UInt64 *completed)) override {
        return !Completed || !completed || Completed(*completed) ? S_OK : E_ABORT;
    }
};
