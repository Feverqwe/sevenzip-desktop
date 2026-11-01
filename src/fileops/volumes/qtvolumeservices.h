#pragma once

#include "Common/MyString.h"
#include <functional>

struct CSplitVolumePlan;
struct CCombineVolumePlan;

// CApp owns the source workflow. Qt launches the joined asynchronous worker
// and presents its progress; plans contain the original native worker fields.
class IQtVolumeServices {
  public:
    virtual ~IQtVolumeServices() = default;
    virtual void StartSplit(const CSplitVolumePlan &plan, const UString &title,
                            std::function<void()> completion) = 0;
    virtual void StartCombine(const CCombineVolumePlan &plan, const UString &title,
                              std::function<void()> completion) = 0;
};
