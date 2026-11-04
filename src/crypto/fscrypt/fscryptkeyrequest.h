// Optional local extension to the SDK's generic password callback.
#pragma once
#include "7zip/IDecl.h"

inline constexpr GUID IID_IFscryptKeyRequest = {
    0x55f77674, 0xe678, 0x438d, {0xa4, 0x96, 0x9c, 0x53, 0x67, 0xa6, 0x47, 0x71}};
struct Z7_DECLSPEC_NOVTABLE IFscryptKeyRequest : public IUnknown {
    Z7_COM7F_PURE(GetFscryptKeys(BSTR *keys))
};

// Only the opening worker may use this interface; it does not retain its UI.
struct IFscryptKeyUI {
    virtual HRESULT Open_FscryptKeys(BSTR *keys) = 0;
    virtual ~IFscryptKeyUI() = default;
};
