#pragma once
#include "Common/MyString.h"
#include "Common/MyWindows.h"
#include "common/collections/MyVector.h"

class CPanel;
class IQtPanelPluginMenuServices {
  public:
    virtual ~IQtPanelPluginMenuServices() = default;
    virtual void ReleasePluginMenus() = 0;
    virtual bool IsShiftDown() const = 0;
    virtual void CreateSevenZipMenu(CPanel &panel, bool extended,
                                    const CRecordVector<UInt32> &indices, int firstDirIndex) = 0;
    virtual void CreateSystemMenu(CPanel &panel, bool extended,
                                  const CRecordVector<UInt32> &indices) = 0;
};

// Native IContextMenu numeric invocation and error presentation boundary.
class IQtPanelPluginServices {
  public:
    virtual ~IQtPanelPluginServices() = default;
    virtual bool HasContextMenu(bool system) const = 0;
    virtual HRESULT InvokeCommand(unsigned offset, bool system, const UString &directory) = 0;
    virtual void ShowInvokeError(HRESULT result) = 0;
};
