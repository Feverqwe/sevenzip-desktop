// Adapted from 7-Zip 0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17.
// Native Windows launch entries and typed in-process overloads; see audit.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#pragma once
#include "7zip/Common/CreateCoder.h"
#include "Common/MyString.h"
UString GetQuotedString(const UString &s);
class QString;
#ifdef _WIN32
HRESULT CompressFiles(const UString &arcPathPrefix, const UString &arcName, const UString &arcType,
                      bool addExtension, const UStringVector &names, bool email, bool showDialog,
                      bool waitFinish);
void ExtractArchives(const UStringVector &arcPaths, const UString &outFolder, bool showDialog,
                     bool elimDup, UInt32 writeZone);
void TestArchives(const UStringVector &arcPaths, bool hashMode = false);
void CalcChecksum(const UStringVector &paths, const UString &methodName,
                  const UString &arcPathPrefix, const UString &arcFileName);
#endif
class QWidget;
class IQtCompressCallServices;
HRESULT CompressFiles(const UString &arcPathPrefix, const UString &arcName, const UString &arcType,
                      bool addExtension, const UStringVector &names, bool email, bool showDialog,
                      bool waitFinish, IQtCompressCallServices &services);
HRESULT ExtractArchives(const UStringVector &paths, const UString &outFolder, bool showDialog,
                        bool elimDup, UInt32 writeZone, IQtCompressCallServices &services);
HRESULT TestArchives(const UStringVector &paths, bool hashMode, IQtCompressCallServices &services);
HRESULT CalcChecksum(const UStringVector &paths, const UString &methodName,
                     const UString &arcPathPrefix, const UString &arcFileName,
                     IQtCompressCallServices &services);
