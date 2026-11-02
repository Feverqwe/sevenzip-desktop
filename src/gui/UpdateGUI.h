// Adapted from 7-Zip CPP/7zip/UI/GUI/UpdateGUI.cpp at 0766b73.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#pragma once

#include "7zip/UI/Common/Property.h"
#include "7zip/UI/Common/UpdateAction.h"
#include "dialogs/compress/CompressInfo.h"
#include "fileops/patterns/Wildcard.h"
#include <functional>
class CUpdateCallbackGUI;
class CCodecs;
class CCompressDialog;
struct CUpdateOptions;
struct COpenType;
class IQtUpdateGUIServices;
class QWidget;

HRESULT ShowDialog(CCodecs *codecs, const CObjectVector<NWildcard::CCensorPath> &censor,
                   CUpdateOptions &options, CUpdateCallbackGUI *callback, QWidget *hwndParent,
                   IQtUpdateGUIServices &services);

HRESULT UpdateGUI(CCodecs *codecs, const CObjectVector<COpenType> &formatIndices,
                  const UString &cmdArcPath, NWildcard::CCensor &censor, CUpdateOptions &options,
                  bool showDialog, bool &messageWasDisplayed, CUpdateCallbackGUI *callback,
                  QWidget *hwndParent, IQtUpdateGUIServices &services,
                  const std::function<void()> &outputReady = {});

namespace NCompressDialog {
// Source ShowDialog format/name preparation; Qt supplies the selected paths
// and owns modal presentation after this source decision block.
HRESULT PrepareFormatSelection(CCompressDialog &dialog, const CCodecs &codecs,
                               const CObjectVector<NWildcard::CCensorPath> &censor);
FString GetCompressionWorkingDir(const UString &arcPath, const QString &settingsPath);
const NUpdateArchive::CActionSet *FindUpdateActionSet(NUpdateMode::EEnum mode);
CObjectVector<CProperty> GetCompressionProperties(const CInfo &info, bool is7z);
} // namespace NCompressDialog
