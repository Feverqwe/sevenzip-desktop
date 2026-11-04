// Adapted from 7-Zip HashGUI.h (0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#pragma once
#include "7zip/UI/Common/Property.h"
#include "archive/hashing/HashCalc.h"
class QWidget;
typedef CObjectVector<CProperty> CPropNameValPairs;
HRESULT HashCalcGUI(DECL_EXTERNAL_CODECS_LOC_VARS const NWildcard::CCensor &censor,
                    const CHashOptions &options, bool &messageWasDisplayed);
void AddValuePair(CPropNameValPairs &pairs, UINT resourceID, UInt64 value);
void AddSizeValuePair(CPropNameValPairs &pairs, UINT resourceID, UInt64 value);
void AddHashBundleRes(CPropNameValPairs &s, const CHashBundle &hb);
void AddHashBundleRes(UString &s, const CHashBundle &hb);
void ShowHashResults(const CPropNameValPairs &propPairs, QWidget *hwnd);
void ShowHashResults(const CHashBundle &hb, QWidget *hwnd);
