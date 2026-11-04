// Adapted from 7-Zip ExtractCallback.cpp/.h (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#pragma once

#include "Common/MyString.h"
#include "Common/MyWindows.h"

class CCodecs;
struct CArchiveLink;

UString GetOpenArcErrorMessage(UInt32 errorFlags);
void SetExtractErrorMessage(Int32 opRes, Int32 encrypted, const wchar_t *fileName, UString &s);
void OpenResult_GUI(UString &s, const CCodecs *codecs, const CArchiveLink &arcLink,
                    const wchar_t *name, HRESULT result);
