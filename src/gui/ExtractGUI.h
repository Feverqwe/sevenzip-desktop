// GUI/ExtractGUI.h
// Adapted from 7-Zip 0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.

#ifndef ZIP7_INC_EXTRACT_GUI_H
#define ZIP7_INC_EXTRACT_GUI_H

#include "archive/operations/Extract.h"

#include "filemanager/ExtractCallback.h"
#include <functional>

/*
  RESULT can be S_OK, even if there are errors!!!
  if RESULT == S_OK, check extractCallback->IsOK() after ExtractGUI().

  RESULT = E_ABORT - user break.
  RESULT != E_ABORT:
  {
   messageWasDisplayed = true  - message was displayed already.
   messageWasDisplayed = false - there was some internal error, so you must show error message.
  }
*/

HRESULT ExtractGUI(
    // DECL_EXTERNAL_CODECS_LOC_VARS
    CCodecs *codecs, const CObjectVector<COpenType> &formatIndices,
    const CIntVector &excludedFormatIndices, UStringVector &archivePaths,
    UStringVector &archivePathsFull, const NWildcard::CCensorNode &wildcardCensor,
    CExtractOptions &options,
#ifndef Z7_SFX
    CHashBundle *hb,
#endif
    bool showDialog, bool &messageWasDisplayed, CExtractCallbackImp *extractCallback,
    IQtArchiveOpenService *qtOpenService, IQtProgressServices *progressServices,
    QWidget *hwndParent = nullptr, const std::function<void()> &outputReady = {},
    const std::function<QString(const QString &)> &destinationError = {});

#endif
