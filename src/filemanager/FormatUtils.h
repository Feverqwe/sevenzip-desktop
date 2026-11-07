// Adapted from 7-Zip CPP/7zip/UI/FileManager/FormatUtils.h (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#ifndef ZIP7_INC_FORMAT_UTILS_H
#define ZIP7_INC_FORMAT_UTILS_H

#include "Common/MyString.h"
#include "Common/MyTypes.h"
#include <QString>

QString ConvertSizeToString(UInt64 value);
bool IsSizeProp(UINT propID) noexcept;
bool IsByteSizeProp(UINT propID) noexcept;
bool IsTimestampProp(UINT propID) noexcept;
void AddSizeValue(UString &s, UInt64 value);
wchar_t *Browse_ConvertSizeToString(UInt64 v, wchar_t *s Z7_lifetimebound);

UString NumberToString(UInt64 number);

UString MyFormatNew(const UString &format, const UString &argument);
UString MyFormatNew(UINT resourceID, const UString &argument);
UString MyFormatNew(UINT resourceID, UInt64 count);

#endif
