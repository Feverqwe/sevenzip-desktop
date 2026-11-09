#include "archive/resources/archivenativeopen.h"
#include "Windows/TimeUtils.h"
#include "archive/callbacks/archivepasswordretry.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/resources/archiveinputlayer.h"
#include "platform/temp/qtownedtempidentity.h"
#include <QFileInfo>
#include <QScopeGuard>
#include <algorithm>

ArchiveNativeReadScope::ArchiveNativeReadScope(std::shared_ptr<ArchiveNativeOpen> reader,
                                               IProgress *request)
    : reader_(std::move(reader)), lock_(reader_->itemStreamMutex()),
      request_(reader_->inputStream(), request) {}

namespace {
class COpenReadRequest final : public IProgress, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_1(IProgress)
    IOpenCallbackUI *callback_;

  public:
    explicit COpenReadRequest(IOpenCallbackUI *callback) : callback_(callback) {}
    // ArchiveOpenCallback::IProgress ignores internal structure-read counts.
    // The opened handler owns its Open_SetTotal/Open_SetCompleted address space.
    Z7_COM7F_IMF(SetTotal(UInt64)) override { return S_OK; }
    Z7_COM7F_IMF(SetCompleted(const UInt64 *)) override {
        return callback_ ? callback_->Open_CheckBreak() : S_OK;
    }
};
} // namespace

CMyComPtr<IProgress> CreateArchiveOpenReadRequest(IOpenCallbackUI *callback) {
    return new COpenReadRequest(callback);
}

ArchiveNativeOpen::~ArchiveNativeOpen() { Close(); }

ArchiveNativeOpen::SourceFile::SourceFile(const QString &file) {
    if (file.isEmpty())
        return;
    path = QFileInfo(file).absoluteFilePath();
    identity = QtOwnedTempIdentity(QFileInfo(path).canonicalFilePath());
    NWindows::NFile::NFind::CFileInfo info;
    if (!info.Find_FollowLink(us2fs(path.toStdWString().c_str()))) {
        identity.clear();
        return;
    }
    size = info.Size;
    modified = info.MTime;
}

bool ArchiveNativeOpen::SourceFile::IsCurrent() const {
    if (identity.isEmpty() || identity != QtOwnedTempIdentity(QFileInfo(path).canonicalFilePath()))
        return false;
    NWindows::NFile::NFind::CFileInfo current;
    return current.Find_FollowLink(us2fs(path.toStdWString().c_str())) && current.Size == size &&
           Compare_FiTime(&current.MTime, &modified) == 0;
}

const CCodecs &ArchiveNativeOpen::nativeCodecs() const { return catalog_->nativeCodecs(); }

QString ArchiveNativeOpen::resolvedOpenType() const {
    QStringList types;
    for (unsigned i = link_.Arcs.Size(); i != 0; --i) {
        const int index = link_.Arcs[i - 1].FormatIndex;
        types.append(index < 0 ? QStringLiteral("#")
                               : QString::fromWCharArray(nativeCodecs().GetFormatNamePtr(index)));
    }
    return types.join('.');
}

HRESULT ArchiveNativeOpen::Open(const QString &path, const QString &type, IOpenCallbackUI *callback,
                                const COpenOptions *requested) {
    callback_ = callback;
    directoryReadRequested_ = requested && requested->PreferDirectoryRead;
    // Preserve the empty source type's recursive intent explicitly at the
    // file-only listing/extract/reopen integration boundary.
    transportType_ = type.isEmpty() ? QStringLiteral("*:r") : type;
    const auto detach = qScopeGuard([&] { callback_ = nullptr; });
    input_ = requested ? requested->stream : nullptr;
    source_ =
        SourceFile(!requested || (!requested->stream && !requested->stdInMode) ? path : QString());
    volumes_.clear();
    sourceStable_ = !source_.identity.isEmpty();
    CMyComPtr<IProgress> readRequest = CreateArchiveOpenReadRequest(callback);
    ArchiveInputReadRequestScope inputRequest(input_, readRequest);
    // Volume streams can retain the source COpenCallbackImp beyond opening.
    // Its UI pointer must remain valid, without retaining the opening dialog.
    for (;;) {
        HRESULT result;
        if (requested) {
            COpenOptions options = *requested;
            options.filePath = path.toStdWString().c_str();
            result = catalog_->openArchive(link_, options, this);
        } else {
            result = catalog_->openArchive(link_, path, type, this);
        }
        if (result != kArchiveWrongPassword) {
            RINOK(result)
            break;
        }
        auto *retry = dynamic_cast<IArchivePasswordRetryUI *>(callback);
        if (!retry)
            return result;
        const HRESULT reset = retry->ResetArchivePassword();
        if (reset == E_NOTIMPL)
            return result;
        RINOK(reset)
        RINOK(link_.Close())
        link_.Release();
    }
    // A handler can initially scan its own SFX prefix, leaving Offset positive
    // and ArcStreamOffset zero. The source ReOpen establishes a tail stream
    // and retains the physical input for ArchiveFolderOut's prefix publisher.
    if ((!requested || !requested->stream) && !hasNestedStream() && !hasVolumes() &&
        !arc().IsReadOnly && arc().GetGlobalOffset() > 0) {
        CMyComPtr<IOutArchive> writer;
        if (arc().Archive.QueryInterface(IID_IOutArchive, &writer) == S_OK && writer) {
            CMyComPtr2_Create<IArchiveOpenCallback, COpenCallbackImp> progress;
            progress->Callback = this;
            COpenOptions options;
            options.props = nullptr;
            options.codecs = nullptr;
            options.filePath = path.toStdWString().c_str();
            options.callback = progress;
            RINOK(link_.ReOpen(options))
        }
    }
    if (!source_.path.isEmpty()) {
        FOR_VECTOR(i, link_.VolumePaths) {
            volumes_.emplace_back(QString::fromWCharArray(link_.VolumePaths[i]));
        }
    }
    if (!sourceIsCurrent())
        sourceStable_ = false;
    return S_OK;
}

bool ArchiveNativeOpen::sourceIsCurrent() const {
    if (source_.path.isEmpty())
        return true; // Explicit stream/device input keeps its existing owner policy.
    return sourceStable_ && source_.IsCurrent() &&
           std::all_of(volumes_.cbegin(), volumes_.cend(),
                       [](const SourceFile &volume) { return volume.IsCurrent(); });
}

HRESULT ArchiveNativeOpen::Open_CheckBreak() {
    return callback_ ? callback_->Open_CheckBreak() : S_OK;
}
HRESULT ArchiveNativeOpen::Open_SetTotal(const UInt64 *files, const UInt64 *bytes) {
    return callback_ ? callback_->Open_SetTotal(files, bytes) : S_OK;
}
HRESULT ArchiveNativeOpen::Open_SetCompleted(const UInt64 *files, const UInt64 *bytes) {
    return callback_ ? callback_->Open_SetCompleted(files, bytes) : S_OK;
}
HRESULT ArchiveNativeOpen::Open_Finished() { return callback_ ? callback_->Open_Finished() : S_OK; }
HRESULT ArchiveNativeOpen::Open_CryptoGetTextPassword(BSTR *password) {
    return callback_ ? callback_->Open_CryptoGetTextPassword(password) : E_ABORT;
}

HRESULT ArchiveNativeOpen::Open_FscryptKeys(BSTR *keys) {
    if (auto *request = dynamic_cast<IFscryptKeyUI *>(callback_))
        return request->Open_FscryptKeys(keys);
    return Open_CryptoGetTextPassword(keys);
}

HRESULT ArchiveNativeOpen::Close() {
    const HRESULT result = link_.IsOpen ? link_.Close() : S_OK;
    link_.Release();
    input_.Release();
    return result;
}

void ArchiveNativeOpen::CopyVolumeMetadata(CArchiveLink &link) const {
    link.VolumePaths = link_.VolumePaths;
    link.VolumesSize = link_.VolumesSize;
}

const CArcInfoEx *ArchiveNativeOpen::formatInfo() const {
    if (!link_.IsOpen)
        return nullptr;
    const auto &formats = catalog_->nativeCodecs().Formats;
    const int index = arc().FormatIndex;
    return index >= 0 && unsigned(index) < formats.Size() ? &formats[unsigned(index)] : nullptr;
}

bool ArchiveNativeOpen::hasNativeRarExtractor() const {
    if (!link_.IsOpen)
        return false;
    const auto &formats = catalog_->nativeCodecs().Formats;
    if (arc().FormatIndex < 0 || unsigned(arc().FormatIndex) >= formats.Size())
        return false;
    const auto &format = formats[unsigned(arc().FormatIndex)];
    return format.Name.IsEqualTo("Rar5") && format.LibIndex < 0;
}

bool ArchiveNativeOpen::hasNativeHashExtractor() const {
    if (!link_.IsOpen)
        return false;
    const auto &formats = catalog_->nativeCodecs().Formats;
    if (arc().FormatIndex < 0 || unsigned(arc().FormatIndex) >= formats.Size())
        return false;
    const auto &format = formats[unsigned(arc().FormatIndex)];
    return format.Flags_HashHandler() && format.LibIndex < 0;
}
