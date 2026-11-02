#include "archive/resources/qtarchivefileresource.h"
#include "7zip/Common/FileStreams.h"
#include "7zip/Common/StreamUtils.h"
#include "7zip/Compress/CopyCoder.h"
#include "Common/StringConvert.h"
#include "Windows/PropVariant.h"
#include "archive/callbacks/ArchiveOpenCallback.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/opening/OpenArchive.h"
#include "archive/resources/archiveinputlayer.h"
#include "archive/resources/archivelayercontext.h"
#include "archive/resources/archivenativeopen.h"
#include "archive/resources/archivepropertysource.h"
#include "archive/resources/qtarchivedeleteinputs.h"
#include "archive/resources/qtarchiveproperties.h"
#include "fileops/native/FileDir.h"
#include "platform/temp/WorkDir.h"
#include "platform/temp/qtownedtempregistry.h"
#include "ui/progress/ProgressDialog2.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>

namespace {
class CSpoolProgress final : public ICompressProgressInfo, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_1(ICompressProgressInfo)
    IProgress *_progress;

  public:
    explicit CSpoolProgress(IProgress *progress) : _progress(progress) {}
    Z7_COM7F_IMF(SetRatioInfo(const UInt64 *inSize, const UInt64 *)) override {
        return _progress ? _progress->SetCompleted(inSize) : S_OK;
    }
};

HRESULT CopySpool(IInStream *input, COutFileStream &output, IProgress *progress) {
    RINOK(InStream_SeekToBegin(input))
    CMyComPtr<ICompressProgressInfo> copyProgress = new CSpoolProgress(progress);
    RINOK(NCompress::CopyStream(input, &output, copyProgress))
    return output.Close();
}
} // namespace

QtArchiveFileResource::~QtArchiveFileResource() {
    CloseNativeOpen();
    if (!_temporary.isEmpty())
        QFile::remove(_temporary);
}

HRESULT QtArchiveFileResource::CloseNativeOpen() {
    _nativePropertiesExplicit = false;
    auto reader = std::move(_nativeOpen);
    // Child streams can still use this generation after navigation or refresh.
    // Its final lease releases the handlers, input and codec image together.
    return reader && reader.use_count() == 1 ? reader->Close() : S_OK;
}

void QtArchiveFileResource::AttachNativeOpen(std::shared_ptr<ArchiveNativeOpen> reader) {
    _nativeMetadata = false;
    _nativePropertiesExplicit = false;
    _nativeNested = reader && reader->hasNestedStream();
    if (reader)
        _containerLevels = reader->archiveLink().Arcs.Size();
    _nativeReadOnly = reader && reader->arc().IsReadOnly;
    _nativeOpenType = reader ? reader->transportType() : QString();
    _nativeOpen = std::move(reader);
}

bool QtArchiveFileResource::RequiresOuterOpen() const {
    return _nativeNested || (_nativeOpen ? _nativeOpen->hasVolumes() : Info.volumeCount > 1);
}

QString QtArchiveFileResource::TransportFormat() const {
    // The source opener accepts a good outer chain with a rejected inner file.
    // CLI autodetection would retry that rejected level and fail the listing;
    // stop at the exact handler chain already accepted by OpenArchive.
    if (_nativeOpen && !_nativeOpen->archiveLink().NonOpen_ArcPath.IsEmpty())
        return _nativeOpen->resolvedOpenType();
    return _nativeNested ? _nativeOpenType : (RequiresOuterOpen() ? QString() : Info.format);
}

quint64 QtArchiveFileResource::ContainerDepth() const {
    return _parentContainerDepth + qMax(_containerLevels, Info.containerLevels);
}

void QtArchiveFileResource::RetainStreamInput(std::shared_ptr<ArchiveInputLayer> input) {
    if (const auto depth = input->ContainingContainerDepth()) {
        _parentContainerDepth = *depth;
        _inputContextDefined = true;
    }
    _streamInput = std::move(input);
}

bool QtArchiveFileResource::HasUpdateService() const {
    if (HasStreamInput())
        return false;
    // The original publisher can preserve a native SFX prefix when its source
    // input stream and offset are retained. A process-only plan cannot do so.
    const bool preservesPrefix = HasNativeUpdateService() && _nativeOpen->arc().InStream &&
                                 _nativeOpen->arc().ArcStreamOffset != 0;
    return bool(_update) && (Info.offset == 0 || preservesPrefix) && !RequiresOuterOpen() &&
           !_nativeReadOnly && _filesystemReadLimitations.isEmpty();
}

HRESULT QtArchiveFileResource::ApplyOutputMTime(CWorkDirTempFile &file) const {
    return _outputMTime ? file.SetMTime(*_outputMTime) : S_OK;
}

HRESULT QtArchiveFileResource::AdoptPublishedOwnedTemp(const CWorkDirTempFile &file) {
    // A user archive is never registered by publication. Only a private spool
    // already owned by this resource can receive the successfully moved object.
    if (_temporary.isEmpty())
        return S_OK;
    const QString destination = QString::fromWCharArray(fs2us(file.Get_OriginalFilePath()).Ptr());
    if (QDir::cleanPath(destination) != QDir::cleanPath(_temporary) || !file.OwnedLease())
        return E_INVALIDARG;
    auto replacement = file.OwnedLease();
    if (replacement->MatchesObjectAt(_temporary)) {
        if (!replacement->RelocateAfterMove(_temporary))
            return E_FAIL;
    } else {
        // Cross-volume source moves can copy into a different OS file object.
        replacement = QtOwnedTempRegistry().RegisterCreated(_temporary);
        if (!replacement)
            return E_FAIL;
    }
    _temporaryLease = std::move(replacement);
    return S_OK;
}

void QtArchiveFileResource::CaptureOutputMTime(const CFiTime &time, bool defined) {
    _outputMTime.reset();
    if (_compressionOptions && _compressionOptions->SetArcMTime.Val && defined)
        _outputMTime = time;
}

void QtArchiveFileResource::CaptureDeleteInputs(const CDirItems &items,
                                                const CByteBuffer &processed) {
    _deleteInputs.reset();
    if (_compressionOptions && _compressionOptions->DeleteAfterCompressing)
        _deleteInputs = std::make_shared<QtArchiveDeleteInputs>(items, processed,
                                                                _compressionOptions->SymLinks);
}

HRESULT QtArchiveFileResource::DeleteInputsAfterPublication(IUpdateDeletionCallback &callback) {
    auto inputs = std::move(_deleteInputs);
    return inputs ? inputs->Delete(callback) : S_OK;
}

HRESULT QtArchiveFileResource::GetArchiveProperty(PROPID id, PROPVARIANT *value) const {
    return ArchivePropertySource(this).GetProperty(id, value);
}

HRESULT QtArchiveFileResource::GetNumberOfArchiveProperties(UInt32 *count) const {
    return ArchivePropertySource(this).GetCount(count);
}

HRESULT QtArchiveFileResource::GetArchivePropertyInfo(UInt32 index, BSTR *name, PROPID *id,
                                                      VARTYPE *type) const {
    return ArchivePropertySource(this).GetInfo(index, name, id, type);
}

HRESULT QtArchiveFileResource::ConfigureArc(CArc &arc, const CCodecs *codecs) const {
    const QFileInfo file(EnginePath);
    if (!HasStreamInput() && !file.isFile())
        return E_FAIL;
    arc.Path = EnginePath.toStdWString().c_str();
    if (_nativeOpen) {
        const CArc &native = _nativeOpen->arc();
        // A native index belongs to its retained catalog. Map the handler name
        // into the caller's metadata table for source IsHashHandler; never copy
        // an index between different codec owners.
        if (codecs)
            if (const CArcInfoEx *format = _nativeOpen->formatInfo())
                arc.FormatIndex = codecs->FindFormatForArchiveType(format->Name);
        arc.DefaultName = native.DefaultName;
        arc.IsReadOnly =
            HasStreamInput() || native.IsReadOnly || !_filesystemReadLimitations.isEmpty();
        arc.FileSize = native.FileSize;
        arc.ArcStreamOffset = native.ArcStreamOffset;
        arc.InStream = native.InStream;
        arc.GetRawProps.Release();
        arc.GetRootProps.Release();
        arc.IsTree = false;
        arc.Ask_Deleted = false;
        arc.Ask_AltStream = false;
        arc.Ask_Aux = false;
        arc.Ask_INode = false;
        if (ArchiveUsesNativeItemIndexes(arc.Archive)) {
            arc.Archive->QueryInterface(IID_IArchiveGetRawProps, (void **)&arc.GetRawProps);
            arc.GetRootProps = native.GetRootProps;
            arc.IsTree = native.IsTree;
            arc.Ask_Deleted = native.Ask_Deleted;
            arc.Ask_AltStream = native.Ask_AltStream;
            arc.Ask_Aux = native.Ask_Aux;
            arc.Ask_INode = native.Ask_INode;
        }
        return arc.ReadBasicProps(arc.Archive, native.ArcStreamOffset, S_OK);
    }
    if (_codecs && _codecs->result() == S_OK)
        arc.SetDefaultName(_codecs->nativeCodecs(), _codecs->formatIndex(Info.format));
    arc.IsReadOnly = !_filesystemReadLimitations.isEmpty();
    // Typed metadata reports the logical stream across all split parts; source
    // ReadBasicProps compares kpidPhySize with that combined stream size.
    arc.FileSize = UInt64(Info.volumeCount > 1 && Info.totalPhysicalSize >= file.size()
                              ? Info.totalPhysicalSize
                              : file.size());
    arc.ArcStreamOffset = 0;
    return arc.ReadBasicProps(arc.Archive, 0, S_OK);
}

bool QtArchiveFileResource::CopyNativeVolumeMetadata(CArchiveLink &link) const {
    if (!_nativeOpen)
        return false;
    _nativeOpen->CopyVolumeMetadata(link);
    return true;
}

const CArchiveLink *QtArchiveFileResource::NativeArchiveLink() const {
    return _nativeOpen ? &_nativeOpen->archiveLink() : nullptr;
}

const CCodecs *QtArchiveFileResource::NativeArchiveCodecs() const {
    return _nativeOpen ? &_nativeOpen->nativeCodecs() : nullptr;
}

HRESULT QtArchiveFileResource::Update(ISequentialOutStream *output, IInArchive *metadata,
                                      UInt32 count, IArchiveUpdateCallback *callback) {
    if (!HasUpdateService())
        return E_NOTIMPL;
    // A zero-item handler may never report progress. Use the source Sync at
    // both sides of the worker boundary, before the publisher can move output.
    if (_sync) {
        RINOK(_sync->CheckStop())
    }
    RINOK(_update(output, metadata, count, callback))
    return _sync ? _sync->CheckStop() : S_OK;
}

HRESULT QtArchiveFileResource::ReOpen(IFolderFolder **metadata, IArchiveOpenCallback *callback) {
    return _reopen ? _reopen(metadata, callback) : E_NOTIMPL;
}

HRESULT QtArchiveFileResource::Extract(IInArchive *metadata, bool nativeIndexes,
                                       const UInt32 *indices, UInt32 count, Int32 testMode,
                                       IArchiveExtractCallback *callback) {
    return _extract ? _extract(metadata, nativeIndexes, indices, count, testMode, callback)
                    : E_NOTIMPL;
}

std::unique_ptr<IQtProgressTransport>
QtArchiveFileResource::CreateProgressTransport(CProgressDialog &dialog) {
    bool nativeWorker = HasStreamInput() || HasNativeUpdateService();
    if (_compressionOptions && NativeItemProperties()) {
        const auto &options = *_compressionOptions;
        nativeWorker = nativeWorker || options.SymLinks.Val || options.HardLinks.Val ||
                       options.AltStreams.Val || options.NtSecurity.Val;
    }
    return CreateProgressTransport(dialog, nativeWorker);
}

std::unique_ptr<IQtProgressTransport>
QtArchiveFileResource::CreateProgressTransport(CProgressDialog &dialog, bool nativeWorker) {
    return _progress ? _progress(dialog, nativeWorker) : nullptr;
}

HRESULT QtArchiveFileResource::Spool(IInStream *stream, IProgress *progress) {
    if (!_temporary.isEmpty())
        return E_FAIL;
    // The file-backed service retains the source stream and copy algorithm;
    // Use the original temp-name/create service with the final output handle.
    // QTemporaryFile::close can retain its OS handle, preventing a native
    // writable reopen on Windows. This also removes the second file open.
    FString prefix;
    if (!NWindows::NFile::NDir::MyGetTempPath(prefix))
        return GetLastError_noZero_HRESULT();
    prefix += FTEXT("sevenzip-open");
    auto *outputSpec = new COutFileStream;
    CMyComPtr<ISequentialOutStream> output = outputSpec;
    outputSpec->ProcessedSize = 0;
#ifndef _WIN32
    // Preserve the private spool permissions of the former Qt temp service.
    outputSpec->File.mode_for_Create = 0600;
#endif
    AString postfix;
    if (!NWindows::NFile::NDir::CreateTempFile2(prefix, true, postfix, &outputSpec->File))
        return GetLastError_noZero_HRESULT();
    prefix += postfix;
    const QString path = QDir::fromNativeSeparators(QString::fromWCharArray(fs2us(prefix)));
    std::shared_ptr<QtOwnedTempLease> lease;
    auto cleanup = qScopeGuard([&] {
        outputSpec->Close();
        QFile::remove(path);
    });
    lease = QtOwnedTempRegistry().RegisterCreated(path);
    if (!lease)
        return E_FAIL;
    RINOK(CopySpool(stream, *outputSpec, progress))
    _temporary = path;
    _temporaryLease = std::move(lease);
    EnginePath = path;
    cleanup.dismiss();
    return S_OK;
}

HRESULT QtArchiveFileResource::SpoolToOwnedFile(IInStream *stream, const QString &path,
                                                IProgress *progress) {
    if (!stream || !_temporary.isEmpty() || !QDir::isAbsolutePath(path))
        return E_INVALIDARG;
    auto *outputSpec = new COutFileStream;
    CMyComPtr<ISequentialOutStream> output = outputSpec;
    if (!outputSpec->Create_NEW(us2fs(path.toStdWString().c_str())))
        return GetLastError_noZero_HRESULT();
    auto cleanup = qScopeGuard([&] {
        outputSpec->Close();
        QFile::remove(path);
    });
    RINOK(CopySpool(stream, *outputSpec, progress))
    EnginePath = path;
    cleanup.dismiss();
    return S_OK; // The source CTempFileInfo owns cleanup of this file.
}
