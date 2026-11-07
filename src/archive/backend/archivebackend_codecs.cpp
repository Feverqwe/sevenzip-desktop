#include "7zip/Common/CreateCoder.h"
#include "7zip/IPassword.h"
#include "7zip/UI/Common/DefaultName.h"
#include "7zip/UI/Common/SetProperties.h"
#include "C/7zTypes.h"
#include "archive/backend/archivebackend.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/codecs/archivecodecprobe.h"
#include "archive/opening/OpenArchive.h"
#include "archive/operations/ExtractingFilePath.h"
#include "archive/operations/Update.h"
#include "archive/resources/archivedevice.h"
#include "archive/resources/archiveinputlayer.h"
#include "archive/resources/archivenativeopen.h"
#include "gui/UpdateGUI.h"
#include "localization/qtlanguageservices.h"
#include <QFileInfo>
#include <cerrno>

namespace {
HRESULT LoadProviders(const std::shared_ptr<const ArchiveCodecCatalog> &catalog, bool hashOnly,
                      CExternalCodecs &providers) {
    if (!catalog || catalog->result() != S_OK)
        return hashOnly ? S_OK : E_NOTIMPL;
    auto &codecs = const_cast<CCodecs &>(catalog->nativeCodecs());
    if (!hashOnly)
        providers.GetCodecs = static_cast<ICompressCodecsInfo *>(&codecs);
    providers.GetHashers = static_cast<IHashers *>(&codecs);
    return providers.Load();
}

HRESULT ValidateNativeUpdatePaths(UInt32 count, IArchiveUpdateCallback *callback,
                                  bool allowAbsolute = false) {
    if (!callback)
        return E_INVALIDARG;
    for (UInt32 i = 0; i < count; ++i) {
        Int32 newData, newProps;
        UInt32 originalIndex;
        RINOK(callback->GetUpdateItemInfo(i, &newData, &newProps, &originalIndex))
        if (!newProps)
            continue;
        NWindows::NCOM::CPropVariant path;
        RINOK(callback->GetProperty(i, kpidPath, &path))
        // Single-stream handlers may have no stored filename. An unchanged
        // item without new properties preserves its original archive name.
        if (path.vt == VT_EMPTY)
            continue;
        if (path.vt != VT_BSTR)
            return E_INVALIDARG;
        const QString requested = QString::fromWCharArray(path.bstrVal);
        // Original UpdateCallback marks enumerated NTFS streams explicitly.
        // Validate the base path and source-corrected stream name separately;
        // an unmarked colon must never bypass ordinary path validation.
        NWindows::NCOM::CPropVariant stream;
        RINOK(callback->GetProperty(i, kpidIsAltStream, &stream))
        if (stream.vt != VT_EMPTY && stream.vt != VT_BOOL)
            return E_INVALIDARG;
        if (stream.vt != VT_BOOL || stream.boolVal != VARIANT_TRUE) {
            if (!ArchiveBackend::sourceUpdatePathIsSafe(requested, allowAbsolute))
                return E_INVALIDARG;
            continue;
        }
        if (requested.contains(QChar(0)))
            return E_INVALIDARG;
        const int colon = FindAltStreamColon_in_Path(path.bstrVal);
        if (colon <= 0 ||
            !ArchiveBackend::sourceUpdatePathIsSafe(requested.left(colon), allowAbsolute))
            return E_INVALIDARG;
        UString name(path.bstrVal + colon + 1);
        UString corrected = name;
        Correct_AltStream_Name(corrected);
        // The original correction accepts the optional NTFS :$DATA suffix.
        if (name.Len() >= 6 && StringsAreEqualNoCase_Ascii(name.RightPtr(6), ":$DATA"))
            name.DeleteFrom(name.Len() - 6);
        if (name != corrected)
            return E_INVALIDARG;
    }
    return S_OK;
}
} // namespace

ArchiveBackend *ArchiveBackend::createIndependentBackend(QObject *parent) const {
    return independentBackendFactory()(parent);
}

ArchiveBackend::IndependentFactory ArchiveBackend::independentBackendFactory() const {
    return [module = configuredModule_, catalog = codecCatalog()](QObject *parent) {
        auto *backend = new ArchiveBackend(parent, module);
        backend->codecCatalog_.store(catalog);
        return backend;
    };
}

HRESULT ArchiveBackend::openNativeArchive(const QString &path, const QString &type,
                                          IOpenCallbackUI *callback,
                                          std::shared_ptr<ArchiveNativeOpen> &reader,
                                          const COpenOptions *options) const {
    return openNativeArchive(codecCatalog(), path, type, callback, reader, options);
}

HRESULT ArchiveBackend::openNativeArchive(const std::shared_ptr<const ArchiveCodecCatalog> &catalog,
                                          const QString &path, const QString &type,
                                          IOpenCallbackUI *callback,
                                          std::shared_ptr<ArchiveNativeOpen> &reader,
                                          const COpenOptions *options) {
    reader.reset();
    // A typed metadata service can supply a folder without a retained reader.
    // Backend reads themselves acquire native providers in the joined worker.
    if (!catalog || catalog->result() != S_OK)
        return S_OK;
    auto opened = std::make_shared<ArchiveNativeOpen>(catalog);
    const HRESULT result = opened->Open(path, type, callback, options);
    if (result == S_OK || result == S_FALSE)
        reader = std::move(opened);
    return result;
}

HRESULT
ArchiveBackend::openDeviceFilesystem(const std::shared_ptr<const ArchiveCodecCatalog> &catalog,
                                     IInStream *input, const QString &name,
                                     IOpenCallbackUI *callback,
                                     std::shared_ptr<ArchiveNativeOpen> &reader, QString *error,
                                     const COpenOptions *options) {
    return OpenDeviceFilesystem(catalog, input, name, callback, reader, error, options);
}

HRESULT ArchiveBackend::loadCodecCatalog(const QString &module, quint32 engineVersion) {
    if (busy() || codecDiscoveryActive_)
        return E_ABORT;
    if (!engineVersion)
        return E_INVALIDARG;
    // Existing consumers can retain the old immutable catalog/handler lease.
    // A failed replacement publishes no stale or guessed capabilities.
    std::shared_ptr<const ArchiveCodecCatalog> catalog =
        std::make_shared<ArchiveCodecCatalog>(module, engineVersion);
    const HRESULT result = catalog->result();
    codecCatalog_.store(std::move(catalog));
    return result;
}

HRESULT ArchiveBackend::updateNativeComment(const std::shared_ptr<ArchiveNativeOpen> &reader,
                                            ISequentialOutStream *output, UInt32 count,
                                            IArchiveUpdateCallback *callback) const {
    if (!reader || !reader->hasCommentWriter())
        return E_NOTIMPL;
    return updateNativeArchive(reader, output, count, callback);
}

HRESULT ArchiveBackend::updateSourceArchive(const QString &path, const QString &type,
                                            NWildcard::CCensor &censor, CUpdateOptions &options,
                                            CUpdateErrorInfo &errorInfo,
                                            IOpenCallbackUI *openCallback,
                                            IUpdateCallbackUI2 *callback, bool needSetPath) const {
    if (!callback || !openCallback)
        return E_INVALIDARG;
    if (busy())
        return E_ABORT;
    const auto catalog = codecCatalog();
    if (!catalog)
        return E_NOTIMPL;
    return catalog->updateArchive(path, type, censor, options, errorInfo, openCallback, callback,
                                  needSetPath);
}

HRESULT ArchiveBackend::updateSourceArchive(
    const UString &path, const CObjectVector<COpenType> &types, NWildcard::CCensor &censor,
    CUpdateOptions &options, CUpdateErrorInfo &errorInfo, IOpenCallbackUI *openCallback,
    IUpdateCallbackUI2 *callback, bool needSetPath,
    const std::shared_ptr<const ArchiveCodecCatalog> &catalog) const {
    if (!callback || !openCallback || !catalog)
        return E_INVALIDARG;
    // GUI preflight checks busy state before starting this source worker.
    // Worker code only reads the atomically published catalog, never QProcess.
    if (catalog != codecCatalog())
        return E_ABORT;
    return catalog->updateArchive(path, types, censor, options, errorInfo, openCallback, callback,
                                  needSetPath);
}

HRESULT ArchiveBackend::setNativeArchiveProperties(const std::shared_ptr<ArchiveNativeOpen> &reader,
                                                   const wchar_t *const *names,
                                                   const PROPVARIANT *values, UInt32 count) const {
    if (!reader || !reader->arc().Archive || reader->arc().IsReadOnly || reader->hasNestedStream())
        return E_NOTIMPL;
    std::lock_guard<std::mutex> lock(reader->itemStreamMutex());
    CMyComPtr<ISetProperties> properties;
    RINOK(reader->arc().Archive.QueryInterface(IID_ISetProperties, &properties))
    return properties->SetProperties(names, values, count);
}

HRESULT ArchiveBackend::updateNativeArchive(const std::shared_ptr<ArchiveNativeOpen> &reader,
                                            ISequentialOutStream *output, UInt32 count,
                                            IArchiveUpdateCallback *callback) const {
    if (!reader || !reader->arc().Archive || reader->arc().IsReadOnly || reader->hasNestedStream())
        return E_NOTIMPL;
    std::lock_guard<std::mutex> lock(reader->itemStreamMutex());
    CMyComPtr<IOutArchive> writer;
    RINOK(reader->arc().Archive.QueryInterface(IID_IOutArchive, &writer))
    RINOK(ValidateNativeUpdatePaths(count, callback))
    // AgentOut::CommonUpdate retains the handler's properties and calls it
    // directly with the original callback, including tree/raw/crypto services.
    return writer->UpdateItems(output, count, callback);
}

HRESULT ArchiveBackend::updateNativeArchive(const std::shared_ptr<ArchiveNativeOpen> &reader,
                                            ISequentialOutStream *output, UInt32 count,
                                            IArchiveUpdateCallback *callback,
                                            const CompressionOptions &options) const {
    if (!reader || !reader->arc().Archive || reader->arc().IsReadOnly || reader->hasNestedStream())
        return E_NOTIMPL;
    std::lock_guard<std::mutex> lock(reader->itemStreamMutex());
    CMyComPtr<IOutArchive> writer;
    RINOK(reader->arc().Archive.QueryInterface(IID_IOutArchive, &writer))
    RINOK(ValidateNativeUpdatePaths(count, callback, options.PathMode == NWildcard::k_AbsPath))
    const auto *format = reader->formatInfo();
    if (!format || (options.SymLinks.Val && !format->Flags_SymLinks()) ||
        (options.HardLinks.Val && !format->Flags_HardLinks()) ||
        (options.AltStreams.Val && !format->Flags_AltStreams()) ||
        (options.NtSecurity.Val && !format->Flags_NtSecurity()))
        return E_NOTIMPL;
    CMyComPtr<ICryptoGetTextPassword2> crypto;
    callback->QueryInterface(IID_ICryptoGetTextPassword2, (void **)&crypto);
    QString password;
    if (crypto) {
        Int32 defined = 0;
        CMyComBSTR_Wipe value;
        RINOK(crypto->CryptoGetTextPassword2(&defined, &value))
        if (defined)
            password = QString::fromWCharArray(value);
    }
    CObjectVector<CProperty> properties;
    QString error;
    if (!compressionProperties(QString::fromWCharArray(format->Name.Ptr()).toLower(), password,
                               options.EncryptHeaders, options, &properties, &error))
        return E_INVALIDARG;
    // Common/Update.cpp's writer property and callback sequence. No staging
    // layer intervenes between native reparse properties and the handler.
    RINOK(SetProperties(writer, properties))
    return writer->UpdateItems(output, count, callback);
}

HRESULT ArchiveBackend::extractNativeArchive(const std::shared_ptr<ArchiveNativeOpen> &reader,
                                             const UInt32 *indices, UInt32 count, Int32 testMode,
                                             IArchiveExtractCallback *callback) {
    if (!reader || !reader->arc().Archive)
        return E_NOTIMPL;
    ArchiveNativeReadScope readRequest(reader, callback);
    if (reader->hasNativeHashExtractor()) {
        RINOK(validateNativeHashReads(*reader, indices, count))
    }
    return reader->arc().Archive->Extract(indices, count, testMode, callback);
}

bool ArchiveBackend::discoverCodecCatalog(const QString &module) {
    if (busy())
        return false;
    try {
        if (!codecProbe_)
            codecProbe_ = std::make_unique<ArchiveCodecProbe>();
        const bool started = codecProbe_->Start(
            module.isEmpty() ? configuredModule_ : module, [this](ArchiveCodecProbeResult result) {
                // Keep UI receivers outside the probe's completion frame so
                // closing the backend cannot invalidate that delivery's owner.
                QMetaObject::invokeMethod(
                    this,
                    [this, result = std::move(result)] {
                        codecDiscoveryActive_ = false;
                        codecCatalog_.store(codecDiscoveryCancelled_
                                                ? std::shared_ptr<const ArchiveCodecCatalog>()
                                                : result.catalog);
                        emit codecCatalogReady(
                            !codecDiscoveryCancelled_ && result.result == S_OK,
                            codecDiscoveryCancelled_
                                ? QCoreApplication::translate("archivebackend_codecs",
                                                              "Codec discovery cancelled.")
                                : result.error);
                    },
                    Qt::QueuedConnection);
            });
        if (started) {
            codecDiscoveryActive_ = true;
            codecDiscoveryCancelled_ = false;
        }
        return started;
    } catch (...) {
        return false;
    }
}

HRESULT ArchiveBackend::loadHashProviders(CExternalCodecs &providers) const {
    providers.ClearAndRelease();
    // Original registered hashers remain available without a DLL.
    return LoadProviders(codecCatalog(), true, providers);
}

HRESULT ArchiveBackend::loadArchiveProviders(CExternalCodecs &providers) const {
    providers.ClearAndRelease();
    return LoadProviders(codecCatalog(), false, providers);
}

QStringList ArchiveBackend::supportedHashMethods(const QStringList &methods) const {
    const auto catalog = codecCatalog();
    CExternalCodecs providers;
    QStringList available;
    if (LoadProviders(catalog, true, providers) != S_OK)
        return available;
    for (const QString &method : methods) {
        CMethodId id;
        bool supported;
        if (method == QStringLiteral("*")) {
            CRecordVector<CMethodId> hashes;
            GetHashMethods(&providers, hashes);
            supported = !hashes.IsEmpty();
        } else
            supported = FindHashMethod(&providers, AString(method.toUtf8().constData()), id);
        if (supported)
            available.append(method);
    }
    return available;
}

void ArchiveBackend::cancelCodecDiscovery() {
    if (codecDiscoveryActive_)
        codecDiscoveryCancelled_ = true;
    if (codecProbe_)
        codecProbe_->Cancel();
}

QString ArchiveBackend::openDialogFilter() const {
    const QString allFiles =
        QCoreApplication::translate("archivebackend_codecs", "All files") + " (*)";
    const auto catalog = codecCatalog();
    if (!catalog || catalog->result() != S_OK)
        return allFiles;
    QStringList masks;
    for (const QString &extension : catalog->extensions())
        masks.append("*." + extension);
    if (masks.isEmpty())
        return allFiles;
    return QCoreApplication::translate("archivebackend_codecs", "Archives") + " (" +
           masks.join(' ') + ");;" + allFiles;
}

bool ArchiveBackend::supportsArchiveUpdate(const QString &format) const {
    const auto catalog = codecCatalog();
    return catalog && catalog->result() == S_OK && catalog->canUpdate(format);
}

ArchiveSfxModule ArchiveBackend::sfxModule(const QString &path) const {
    const auto catalog = codecCatalog();
    return catalog && catalog->result() == S_OK ? catalog->sfxModule(path)
                                                : ArchiveSfxModule::Read({});
}
