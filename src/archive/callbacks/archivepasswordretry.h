// Local UI capability. Native handlers report ERROR_INVALID_PASSWORD; an
// interactive owner can discard that opening's cached value before retrying.
#pragma once
#include "Common/MyWindows.h"
// Actual Win32 HRESULT. On Unix, SDK HRESULT_FROM_WIN32 encodes errno instead.
inline constexpr HRESULT kArchiveWrongPassword = static_cast<HRESULT>(0x80070056L);
class IArchivePasswordRetryUI {
  public:
    virtual ~IArchivePasswordRetryUI() = default;
    virtual HRESULT ResetArchivePassword() = 0;
};
