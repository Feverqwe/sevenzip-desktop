#pragma once
#include "dialogs/compress/qtcompresscallservices.h"
#include "gui/qtguicommand.h"
#include "gui/qtupdateguiservices.h"
#include <functional>
#include <memory>

class ArchiveBackend;
class ArchiveCodecCatalog;
class QWidget;

// Concrete retained-backend/GUI transport for source CompressCall2 workflows.
// Opening and Add presentation are supplied by the window/native launch owner.
class QtShellArchiveServices final : public IQtCompressCallServices, public IQtUpdateGUIServices {
  public:
    using ErrorSink = std::function<void(HRESULT, const UString &)>;
    using OpenSink = std::function<HRESULT(const UString &, const UString &)>;
    using CompressSink = std::function<HRESULT(const UString &, const UString &, const UString &,
                                               bool, const UStringVector &, bool, bool, bool)>;
    using ModalSink = std::function<HRESULT(
        CCompressDialog &, const CObjectVector<NWildcard::CCensorPath> &, QWidget *)>;
    using TestSink = std::function<HRESULT(const UStringVector &, bool)>;
    using ExtractSink =
        std::function<HRESULT(const UStringVector &, const UString &, bool, bool, UInt32)>;
    using OutputSink = QtGUIOutputSink;
    using HashSink = std::function<HRESULT(const UStringVector &, const UString &)>;
    struct Routes {
        OpenSink Open{};
        CompressSink Compress{};
        ErrorSink Error{};
        ModalSink Modal{};
        TestSink Test{};
        ExtractSink Extract{};
        OutputSink OutputReady{};
        HashSink Hash{};
    };
    QtShellArchiveServices(ArchiveBackend &backend, QWidget *parent);
    QtShellArchiveServices(ArchiveBackend &backend, QWidget *parent, Routes routes,
                           QString settingsPath = {});
    QString CompressionSettingsPath() const override { return _settingsPath; }
    FString GetSfxModulePath() const override;
    HRESULT PresentCompressDialog(CCompressDialog &source,
                                  const CObjectVector<NWildcard::CCensorPath> &paths,
                                  QWidget *parent) override;
    void GetUserCodecs(CCodecs &codecs, CObjectVector<CCodecInfoUser> &userCodecs) override;
    ~QtShellArchiveServices() override;
    HRESULT Result() const { return _result; }
    std::shared_ptr<const ArchiveCodecCatalog> Catalog() const { return _catalog; }
    void ResetResult() { _result = S_OK; }
    CCodecs &Codecs() override;
    CCodecs &GUICodecs(bool hashOnly);
    HRESULT LoadHashProviders(CExternalCodecs &providers) override;
    HRESULT LoadExternalCodecs(CExternalCodecs &providers);
    HRESULT ExtractGUI(CCodecs &codecs, const CObjectVector<COpenType> &types, UStringVector &paths,
                       UStringVector &fullPaths, const NWildcard::CCensorNode &censor,
                       CExtractOptions &options, bool showDialog, bool &messageWasDisplayed,
                       CExtractCallbackImp &callback) override;
    HRESULT ExtractGUI(CCodecs &codecs, const CObjectVector<COpenType> &types,
                       const CIntVector &excludedFormats, UStringVector &paths,
                       UStringVector &fullPaths, const NWildcard::CCensorNode &censor,
                       CExtractOptions &options, CHashBundle *hashBundle, bool showDialog,
                       bool &messageWasDisplayed, CExtractCallbackImp &callback,
                       const UString &openType = UString());
    HRESULT HashCalcGUI(const CExternalCodecs *providers, const NWildcard::CCensor &censor,
                        const CHashOptions &options, bool &messageWasDisplayed) override;
    HRESULT UpdateGUI(CCodecs &codecs, const CObjectVector<COpenType> &types, const UString &path,
                      NWildcard::CCensor &censor, CUpdateOptions &options, bool showDialog,
                      bool &messageWasDisplayed, CUpdateCallbackGUI &callback) override;
    HRESULT UpdateArchive(CCodecs *codecs, const CObjectVector<COpenType> &types,
                          const UString &path, NWildcard::CCensor &censor, CUpdateOptions &options,
                          CUpdateErrorInfo &error, IOpenCallbackUI *openCallback,
                          IUpdateCallbackUI2 *callback, bool needSetPath) override;
    void Open(const UString &path, const UString &type) override;
    void ExtractArchives(const UStringVector &paths, const UString &folder, bool showDialog,
                         bool elimDup, UInt32 writeZone) override;
    void TestArchives(const UStringVector &paths, bool hashMode) override;
    void CompressFiles(const UString &folder, const UString &name, const UString &type,
                       bool addExtension, const UStringVector &paths, bool email, bool showDialog,
                       bool waitFinish) override;
    HRESULT CompressSelection(const UString &folder, const UString &name, const UString &type,
                              bool addExtension, const UStringVector &paths, bool email,
                              bool showDialog, bool waitFinish) override;
    void CalcChecksum(const UStringVector &paths, const UString &method, const UString &folder,
                      const UString &generateName) override;
    void ShowErrorMessage(const UString &message) override;
    void ShowErrorMessageRes(UInt32 resourceID) override;
    void ErrorMessageHRESULT(HRESULT result) override;

  private:
    ArchiveBackend &_backend;
    QWidget *_parent;
    std::shared_ptr<const ArchiveCodecCatalog> _catalog;
    CCodecs _metadata;
    Routes _routes;
    QString _settingsPath;
    HRESULT _result = S_OK;
};
