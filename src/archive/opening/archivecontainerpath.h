// Project container-name boundary shared by native and Qt opening callers.
#pragma once
#include "Common/MyString.h"
inline bool IsSparseBundleName(const UString &name) {
    return name.Len() >= 13 && UString(name.RightPtr(13)).IsEqualTo_NoCase(L".sparsebundle");
}
