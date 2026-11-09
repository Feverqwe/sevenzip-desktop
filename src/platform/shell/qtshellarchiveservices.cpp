#include "platform/shell/qtshellarchiveservices.h"
#include "Common/MyException.h"
#include "archive/backend/archivebackend.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/resources/qtarchivebatchopen.h"
#include "archive/resources/qtextractpathpolicy.h"
#include "gui/ExtractGUI.h"
#include "gui/HashGUI.h"
#include "gui/UpdateGUI.h"
#include "localization/LangUtils.h"
#include "operations/CompressCall.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QScopeGuard>

namespace {
// The source owns wildcard substitution and dialog edits. This policy reads
// its current output root when the source callback validates a final path.
class COutputPolicy final : public IQtExtractPathPolicy {
    const CExtractOptions &_options;
    CQtExtractPathPolicy _protectedInputs;

  public:
    QString Root() const {
        const QString output =
            QDir::fromNativeSeparators(QString::fromWCharArray(fs2us(_options.OutputDir).Ptr()));
        const int wildcard = output.indexOf('*');
        return wildcard < 0 ? output : output.left(output.lastIndexOf('/', wildcard) + 1);
    }

    COutputPolicy(const CExtractOptions &options, const QStringList &inputs)
        : _options(options), _protectedInputs({}, inputs) {}
    HRESULT CheckReplace(const FString &path, UString &message) override {
        return _protectedInputs.CheckReplace(path, message);
    }
    QString DestinationError(const QString &destination) const {
        QString error;
        if (!ArchiveBackend::validateExtractionOutput(destination, destination, true, &error))
            return error;
        // The source engine picks the actual directory for these templates.
        if (_options.OutDirMode == NExtractOutDirMode::k_AddArcName ||
            (_options.OutDirMode == NExtractOutDirMode::k_ReplaceAsterisk &&
             destination.contains('*')))
            return {};
        QString path = destination;
        for (;;) {
            const QFileInfo entry(path);
            if (entry.exists())
                return entry.isWritable() ? QString()
                                          : QCoreApplication::translate(
                                                "CopyRedesign", "The folder is read-only: %1")
                                                .arg(path);
            const QString parent = entry.absolutePath();
            if (parent == path)
                return {};
            path = parent;
        }
    }
    HRESULT CheckPath(const FString &path, bool directory, bool stream, UString &message) override {
        return CQtExtractPathPolicy(Root()).CheckPath(path, directory, stream, message);
    }
    HRESULT CheckLink(const FString &path, const FString &target, bool relative,
                      UString &message) override {
        return CQtExtractPathPolicy(Root()).CheckLink(path, target, relative, message);
    }
};
} // namespace

QtShellArchiveServices::QtShellArchiveServices(ArchiveBackend &backend, QWidget *parent)
    : QtShellArchiveServices(backend, parent, {}) {}

QtShellArchiveServices::QtShellArchiveServices(ArchiveBackend &backend, QWidget *parent,
                                               Routes routes, QString settingsPath)
    : _backend(backend), _parent(parent), _catalog(backend.codecCatalog()),
      _routes(std::move(routes)), _settingsPath(std::move(settingsPath)) {
    // Metadata alone supports source ParseOpenTypes. Native factories remain
    // with the retained immutable backend catalog, never copied/unloaded here.
    if (_catalog && _catalog->result() == S_OK)
        _metadata.Formats = _catalog->nativeCodecs().Formats;
}
QtShellArchiveServices::~QtShellArchiveServices() = default;
HRESULT QtShellArchiveServices::PresentCompressDialog(
    CCompressDialog &source, const CObjectVector<NWildcard::CCensorPath> &paths, QWidget *parent) {
    return _routes.Modal ? _routes.Modal(source, paths, parent) : E_NOTIMPL;
}
void QtShellArchiveServices::GetUserCodecs(CCodecs &codecs,
                                           CObjectVector<CCodecInfoUser> &userCodecs) {
    if (&codecs != &_metadata || !_catalog || _catalog != _backend.codecCatalog())
        throw CSystemException(E_ABORT);
    _catalog->getUserCodecs(userCodecs);
}
CCodecs &QtShellArchiveServices::Codecs() {
    if (!_catalog || _catalog->result() != S_OK)
        throw CSystemException(E_FAIL);
    return _metadata;
}
CCodecs &QtShellArchiveServices::GUICodecs(bool hashOnly) {
    // Registered native hashers remain usable without archive DLL factories.
    return hashOnly && (!_catalog || _catalog->result() != S_OK) ? _metadata : Codecs();
}
HRESULT QtShellArchiveServices::LoadHashProviders(CExternalCodecs &providers) {
    return _backend.loadHashProviders(providers);
}
HRESULT QtShellArchiveServices::LoadExternalCodecs(CExternalCodecs &providers) {
    return _backend.loadArchiveProviders(providers);
}
HRESULT QtShellArchiveServices::ExtractGUI(CCodecs &codecs, const CObjectVector<COpenType> &types,
                                           UStringVector &paths, UStringVector &fullPaths,
                                           const NWildcard::CCensorNode &censor,
                                           CExtractOptions &options, bool showDialog,
                                           bool &messageWasDisplayed,
                                           CExtractCallbackImp &callback) {
    return ExtractGUI(codecs, types, CIntVector(), paths, fullPaths, censor, options, nullptr,
                      showDialog, messageWasDisplayed, callback);
}
HRESULT QtShellArchiveServices::ExtractGUI(CCodecs &codecs, const CObjectVector<COpenType> &types,
                                           const CIntVector &excludedFormats, UStringVector &paths,
                                           UStringVector &fullPaths,
                                           const NWildcard::CCensorNode &censor,
                                           CExtractOptions &options, CHashBundle *hashBundle,
                                           bool showDialog, bool &messageWasDisplayed,
                                           CExtractCallbackImp &callback, const UString &openType) {
    if (_backend.busy() || _backend.codecCatalog() != _catalog)
        return E_FAIL;
    QtArchiveBatchOpen open(_backend);
    if (!openType.IsEmpty())
        open.SetOpenType(openType);
    else if (types.Size() == 1) {
        const auto &type = types[0];
        if (type.IsHashType)
            open.SetOpenType(UString(L"hash"));
        else if (type.FormatIndex >= 0 && unsigned(type.FormatIndex) < codecs.Formats.Size())
            open.SetOpenType(codecs.Formats[unsigned(type.FormatIndex)].Name);
    }
    QStringList inputs;
    FOR_VECTOR(index, fullPaths)
    inputs.append(QString::fromWCharArray(fullPaths[index].Ptr(), fullPaths[index].Len()));
    COutputPolicy policy(options, inputs);
    auto *previous = options.NtOptions.QtPathPolicy;
    const auto restore = qScopeGuard([&] { options.NtOptions.QtPathPolicy = previous; });
    options.NtOptions.QtPathPolicy = &policy;
    const HRESULT result = ::ExtractGUI(
        &codecs, types, excludedFormats, paths, fullPaths, censor, options, hashBundle, showDialog,
        messageWasDisplayed, &callback, &open, &open, _parent,
        [&] {
            if (_routes.OutputReady)
                _routes.OutputReady(policy.Root(), QtGUIOutputKind::Directory);
        },
        [&](const QString &destination) { return policy.DestinationError(destination); });
    return result;
}
HRESULT QtShellArchiveServices::HashCalcGUI(const CExternalCodecs *providers,
                                            const NWildcard::CCensor &censor,
                                            const CHashOptions &options,
                                            bool &messageWasDisplayed) {
    return ::HashCalcGUI(providers, censor, options, messageWasDisplayed);
}
void QtShellArchiveServices::Open(const UString &path, const UString &type) {
    if (_routes.Open)
        _result = _routes.Open(path, type);
    else
        ErrorMessageHRESULT(E_NOTIMPL);
}
void QtShellArchiveServices::ExtractArchives(const UStringVector &paths, const UString &folder,
                                             bool showDialog, bool elimDup, UInt32 zone) {
    _result = _routes.Extract ? _routes.Extract(paths, folder, showDialog, elimDup, zone)
                              : ::ExtractArchives(paths, folder, showDialog, elimDup, zone, *this);
}
void QtShellArchiveServices::TestArchives(const UStringVector &paths, bool hashMode) {
    _result = _routes.Test ? _routes.Test(paths, hashMode) : ::TestArchives(paths, hashMode, *this);
}
void QtShellArchiveServices::CompressFiles(const UString &folder, const UString &name,
                                           const UString &type, bool addExtension,
                                           const UStringVector &paths, bool email, bool showDialog,
                                           bool waitFinish) {
    CompressSelection(folder, name, type, addExtension, paths, email, showDialog, waitFinish);
}
HRESULT QtShellArchiveServices::CompressSelection(const UString &folder, const UString &name,
                                                  const UString &type, bool addExtension,
                                                  const UStringVector &paths, bool email,
                                                  bool showDialog, bool waitFinish) {
    const HRESULT result =
        _routes.Compress
            ? _routes.Compress(folder, name, type, addExtension, paths, email, showDialog,
                               waitFinish)
            : IQtCompressCallServices::CompressSelection(folder, name, type, addExtension, paths,
                                                         email, showDialog, waitFinish);
    // The source dispatcher can return S_OK after UpdateGUI has recorded a
    // cancellation or catalog failure. Preserve that operation result.
    if (result != S_OK)
        _result = result;
    return _result;
}

HRESULT QtShellArchiveServices::UpdateGUI(CCodecs &codecs, const CObjectVector<COpenType> &types,
                                          const UString &path, NWildcard::CCensor &censor,
                                          CUpdateOptions &options, bool showDialog,
                                          bool &messageWasDisplayed, CUpdateCallbackGUI &callback) {
    if (&codecs != &_metadata || _backend.busy() || _catalog != _backend.codecCatalog())
        return _result = E_ABORT;
    if (options.MethodMode.Type_Defined) {
        const int index = options.MethodMode.Type.FormatIndex;
        if (index >= 0 && unsigned(index) < codecs.Formats.Size() &&
            codecs.Formats[unsigned(index)].Flags_HashHandler())
            return _result = E_NOTIMPL;
    }
    // Unix has no upstream MAPI consumer; do not silently report email success.
#ifndef _WIN32
    if (options.EMailMode)
        return _result = E_NOTIMPL;
#endif
    _result = ::UpdateGUI(
        &codecs, types, path, censor, options, showDialog, messageWasDisplayed, &callback, _parent,
        *this, [&] {
            if (!_routes.OutputReady)
                return;
            if (options.VolumesSizes.IsEmpty())
                _routes.OutputReady(
                    QFileInfo(QString::fromWCharArray(options.ArchivePath.GetFinalPath()))
                        .absoluteFilePath(),
                    QtGUIOutputKind::Archive);
            if (!options.VolumesSizes.IsEmpty()) {
                UString first = options.ArchivePath.GetFinalVolPath(false);
                first += ".001";
                _routes.OutputReady(QFileInfo(QString::fromWCharArray(first)).absoluteFilePath(),
                                    QtGUIOutputKind::Archive);
            }
        });
    return _result;
}

HRESULT QtShellArchiveServices::UpdateArchive(CCodecs *codecs,
                                              const CObjectVector<COpenType> &types,
                                              const UString &path, NWildcard::CCensor &censor,
                                              CUpdateOptions &options, CUpdateErrorInfo &error,
                                              IOpenCallbackUI *openCallback,
                                              IUpdateCallbackUI2 *callback, bool needSetPath) {
    if (codecs != &_metadata)
        return E_INVALIDARG;
    return _backend.updateSourceArchive(path, types, censor, options, error, openCallback, callback,
                                        needSetPath, _catalog);
}
void QtShellArchiveServices::CalcChecksum(const UStringVector &paths, const UString &method) {
    const HRESULT result =
        _routes.Hash ? _routes.Hash(paths, method) : ::CalcChecksum(paths, method, *this);
    if (result != S_OK)
        _result = result;
}
void QtShellArchiveServices::ShowErrorMessage(const UString &message) {
    _result = E_FAIL;
    if (_routes.Error)
        _routes.Error(E_FAIL, message);
    else
        QMessageBox::warning(_parent, QStringLiteral("7-Zip"), QString::fromWCharArray(message));
}
void QtShellArchiveServices::ShowErrorMessageRes(UInt32 id) { ShowErrorMessage(LangString(id)); }
void QtShellArchiveServices::ErrorMessageHRESULT(HRESULT result) {
    _result = result;
    if (_routes.Error)
        _routes.Error(result, HResultToMessage(result));
    else
        ShowErrorMessage(HResultToMessage(result));
    _result = result;
}
