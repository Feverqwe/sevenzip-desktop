#include "archive/resources/qtarchivebatchopen.h"
#include "7zip/IPassword.h"
#include "7zip/IProgress.h"
#include "7zip/UI/Common/DefaultName.h"
#include "7zip/UI/Common/SetProperties.h"
#include "Common/ComTry.h"
#include "Common/StringConvert.h"
#include "archive/agent/Agent.h"
#include "archive/resources/qtarchivefileresource.h"
#include "archive/resources/qtarchivefoldermanager.h"
#include "archive/resources/qtarchiveproperties.h"
#include "archive/resources/qtfilesystemreadprompt.h"
#include "fileops/patterns/Wildcard.h"
#include "ui/progress/ProgressDialog2.h"
#include <QDir>
#include <QFileInfo>

namespace {
class CBatchOpenProgress final : public IProgress,
                                 public ICryptoGetTextPassword,
                                 public IQtArchiveReadConsent,
                                 public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_2(IProgress, ICryptoGetTextPassword)
    IOpenCallbackUI *_callback;
    CProgressDialog *_dialog;

  public:
    CBatchOpenProgress(IOpenCallbackUI *callback, CProgressDialog *dialog)
        : _callback(callback), _dialog(dialog) {}
    CProgressDialog *ReadConsentDialog() const override { return _dialog; }
    IOpenCallbackUI *ReadConsentCallback() const override { return _callback; }
    Z7_COM7F_IMF(SetTotal(UInt64 total)) override {
        return _callback ? _callback->Open_SetTotal(nullptr, &total) : S_OK;
    }
    Z7_COM7F_IMF(SetCompleted(const UInt64 *completed)) override {
        return _callback ? _callback->Open_SetCompleted(nullptr, completed) : S_OK;
    }
    Z7_COM7F_IMF(CryptoGetTextPassword(BSTR *password)) override {
        return _callback ? _callback->Open_CryptoGetTextPassword(password) : E_NOINTERFACE;
    }
};
} // namespace

QtArchiveBatchOpen::QtArchiveBatchOpen(ArchiveBackend &backend)
    : _backend(backend), _manager(new QtArchiveFolderManager(backend)) {}

void QtArchiveBatchOpen::SetExtractionPolicy(const QString &destination,
                                             const ExtractionOptions &options) {
    _destination = destination;
    _extractOptions = &options;
}

HRESULT QtArchiveBatchOpen::Open(CArchiveLink &link, const COpenOptions &options,
                                 IOpenCallbackUI *callback) {
    COM_TRY_BEGIN
    if (options.stdInMode || options.filePath.IsEmpty())
        return E_NOTIMPL;
    CMyComPtr<IProgress> progress = new CBatchOpenProgress(callback, _dialog);
    CMyComPtr<IFolderFolder> folder;
    auto *manager = static_cast<QtArchiveFolderManager *>(_manager.Interface());
    const HRESULT result = manager->OpenArchive(
        options, _openType.IsEmpty() ? nullptr : _openType.Ptr(), &folder, progress);
    if (result != S_OK)
        return result;
    auto *agent = dynamic_cast<CAgentFolder *>(folder.Interface());
    auto *resource =
        agent ? dynamic_cast<QtArchiveFileResource *>(agent->GetEngineResource()) : nullptr;
    if (!resource || !agent->GetArchiveProperties())
        return E_FAIL;
    const bool nativeIndexes = ArchiveUsesNativeItemIndexes(agent->GetArchiveProperties());
    if (_extractOptions) {
        QString error;
        if (!_backend.validateExtraction(_destination, {}, *_extractOptions, &error, nativeIndexes,
                                         {},
                                         nativeIndexes && resource->UsesNativeMetadata()
                                             ? resource->NativeArchiveLink()->GetArc()
                                             : nullptr))
            return E_INVALIDARG;
    }
    // Source OpenArchive applies COpenOptions properties to the native handler.
    // The file-only folder stage has already opened it; RAR extraction options
    // (memx/crc/mt) remain applicable before its original Extract call.
    if (options.props && nativeIndexes)
        RINOK(SetProperties(resource->NativeItemProperties(), *options.props))
    CArc &arc = link.Arcs.AddNew();
    arc.Archive = agent->GetArchiveProperties();
    // Process-owned archives have no native codec slot. ConfigureArc maps a
    // retained native handler into the caller's table when that table exists.
    arc.FormatIndex = -1;
    arc.DefaultName =
        GetDefaultName2(ExtractFileNameFromPath(options.filePath), UString(), UString());
    RINOK(resource->ConfigureArc(arc, options.codecs))
    // Open2 records the exact volumes actually used by its retained callback.
    // Keep that source accounting even when the process listing omits a count.
    if (!resource->CopyNativeVolumeMetadata(link) && resource->Info.volumeCount > 1) {
        const QFileInfo first(QString::fromWCharArray(options.filePath));
        const QString name = first.fileName();
        if (!name.endsWith(QStringLiteral(".001")) || resource->Info.volumeCount > 10000)
            return E_NOTIMPL;
        const QString prefix = name.left(name.size() - 3);
        qint64 total = first.size();
        for (int number = 2; number <= resource->Info.volumeCount; ++number) {
            const QFileInfo volume(first.dir().filePath(
                prefix + QStringLiteral("%1").arg(number, 3, 10, QLatin1Char('0'))));
            if (!volume.isFile())
                return E_FAIL;
            const QString absolute = volume.absoluteFilePath();
            link.VolumePaths.Add(UString(absolute.toStdWString().c_str()));
            link.VolumesSize += UInt64(volume.size());
            total += volume.size();
        }
        if (resource->Info.totalPhysicalSize >= 0 && total != resource->Info.totalPhysicalSize)
            return E_FAIL;
    }
    link.IsOpen = true;
    return S_OK;
    COM_TRY_END
}

std::unique_ptr<IQtProgressTransport>
QtArchiveBatchOpen::CreateProgressTransport(CProgressDialog &dialog) {
    _dialog = &dialog;
    auto *services = dynamic_cast<IQtProgressServices *>(_manager.Interface());
    return services ? services->CreateProgressTransport(dialog) : nullptr;
}
