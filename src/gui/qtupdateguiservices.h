#pragma once
#include "archive/operations/Update.h"
class QWidget;
class CUpdateCallbackGUI;
class CCompressDialog;

// Presentation and retained backend factories for the source UpdateGUI worker.
class IQtUpdateGUIServices {
  public:
    virtual ~IQtUpdateGUIServices() = default;
    virtual HRESULT PresentCompressDialog(CCompressDialog &,
                                          const CObjectVector<NWildcard::CCensorPath> &,
                                          QWidget *) {
        return E_NOTIMPL;
    }
    virtual void GetUserCodecs(CCodecs &codecs, CObjectVector<CCodecInfoUser> &userCodecs) {
        codecs.Get_CodecsInfoUser_Vector(userCodecs);
    }
    virtual QString CompressionSettingsPath() const { return {}; }
    virtual HRESULT UpdateArchive(CCodecs *codecs, const CObjectVector<COpenType> &types,
                                  const UString &path, NWildcard::CCensor &censor,
                                  CUpdateOptions &options, CUpdateErrorInfo &error,
                                  IOpenCallbackUI *openCallback, IUpdateCallbackUI2 *callback,
                                  bool needSetPath) = 0;
};
