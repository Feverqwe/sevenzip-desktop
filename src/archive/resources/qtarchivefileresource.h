// Owned engine resource for a source archive folder or direct device stream.
#pragma once
#include "7zip/IStream.h"
#include "Common/MyCom.h"
#include "Windows/FileFind.h"
#include "archive/backend/archivebackend.h"
#include "ui/progress/qtprogresstransport.h"
#include <QPointer>
#include <QString>
#include <functional>
#include <optional>

struct IInStream;
struct IProgress;
class ArchiveBackend;
class ArchiveCodecCatalog;
class ArchiveNativeOpen;
class ArchiveInputLayer;
struct CArchiveLink;
class CProgressSync;
struct IInArchive;
struct IArchiveUpdateCallback;
struct IArchiveExtractCallback;
struct ISequentialOutStream;
struct IFolderFolder;
struct IArchiveOpenCallback;
class CArc;
class CCodecs;
class CWorkDirTempFile;
class QtArchiveDeleteInputs;
class QtOwnedTempLease;
struct IUpdateDeletionCallback;
class CDirItems;
class QtArchiveFileResource final : public IUnknown, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_0
    QString _temporary;
    std::shared_ptr<QtOwnedTempLease> _temporaryLease;
    std::function<HRESULT(ISequentialOutStream *, IInArchive *, UInt32, IArchiveUpdateCallback *)>
        _update;
    CProgressSync *_sync = nullptr;
    std::function<HRESULT(IFolderFolder **, IArchiveOpenCallback *)> _reopen;
    std::function<HRESULT(IInArchive *, bool, const UInt32 *, UInt32, Int32,
                          IArchiveExtractCallback *)>
        _extract;
    std::function<std::unique_ptr<IQtProgressTransport>(CProgressDialog &, bool)> _progress;
    std::optional<CompressionOptions> _compressionOptions;
    std::optional<QtArchiveTransferInputs> _transferInputs;
    std::optional<CFiTime> _outputMTime;
    std::shared_ptr<QtArchiveDeleteInputs> _deleteInputs;
    std::shared_ptr<const ArchiveCodecCatalog> _codecs;
    std::shared_ptr<ArchiveNativeOpen> _nativeOpen;
    CMyComPtr<IInStream> _directInput;
    std::shared_ptr<ArchiveInputLayer> _streamInput;
    bool _nativeNested = false;
    quint64 _parentContainerDepth = 0;
    unsigned _containerLevels = 1;
    bool _inputContextDefined = false;
    bool _nativeReadOnly = false;
    bool _nativeMetadata = false;
    bool _nativePropertiesExplicit = false;
    QString _filesystemReadLimitations;
    std::function<HRESULT(const wchar_t *const *, const PROPVARIANT *, UInt32)> _setProperties;
    QString _nativeOpenType;
    HRESULT RefreshDeviceInput(IOpenCallbackUI *callback, QString *error = nullptr);
    HRESULT ReOpenArchive(const QPointer<ArchiveBackend> &backend, const QString &format,
                          IFolderFolder **metadata, IArchiveOpenCallback *callback);

  public:
    QString EnginePath;
    ArchiveInfo Info;
    explicit QtArchiveFileResource(const QString &path) : EnginePath(path) {}
    ~QtArchiveFileResource();
    void AttachNativeOpen(std::shared_ptr<ArchiveNativeOpen> reader);
    HRESULT OpenDeviceInput(ArchiveBackend &backend, IInStream *input, IOpenCallbackUI *callback,
                            QString *error = nullptr);
    bool HasDirectInput() const { return _directInput != nullptr; }
    bool HasStreamInput() const { return HasDirectInput() || _streamInput != nullptr; }
    void RetainStreamInput(std::shared_ptr<ArchiveInputLayer> input);
    quint64 ContainerDepth() const;
    void SetParentContainerDepth(quint64 depth) {
        if (!_inputContextDefined)
            _parentContainerDepth = depth;
    }
    bool AllowsIncompleteDeviceRead() const;
    const QString &FilesystemReadLimitations() const { return _filesystemReadLimitations; }
    void SetFilesystemReadLimitations(const QString &limitations) {
        _filesystemReadLimitations = limitations;
    }
    HRESULT CloseNativeOpen();
    std::shared_ptr<ArchiveNativeOpen> MatchNativeItemOrder(IFolderFolder *metadata) const;
    void UseNativeMetadata();
    bool UsesNativeMetadata() const { return _nativeMetadata; }
    bool HasCommentService() const;
    bool HasNativeExtractService() const;
    bool HasNativeRarReader() const;
    bool HasNativeHashReader() const;
    IInArchive *NativeItemProperties() const;
    bool HasNativeItemStreamService() const;
    HRESULT Spool(IInStream *stream, IProgress *progress);
    HRESULT SpoolToOwnedFile(IInStream *stream, const QString &path, IProgress *progress);
    HRESULT GetArchiveProperty(PROPID id, PROPVARIANT *value) const;
    HRESULT GetNumberOfArchiveProperties(UInt32 *count) const;
    HRESULT GetArchivePropertyInfo(UInt32 index, BSTR *name, PROPID *id, VARTYPE *type) const;
    HRESULT ConfigureArc(CArc &arc, const CCodecs *codecs = nullptr) const;
    bool CopyNativeVolumeMetadata(CArchiveLink &link) const;
    const CArchiveLink *NativeArchiveLink() const;
    std::shared_ptr<ArchiveNativeOpen> NativeReaderLease() const { return _nativeOpen; }
    const CCodecs *NativeArchiveCodecs() const;
    void AttachCodecCatalog(std::shared_ptr<const ArchiveCodecCatalog> codecs) {
        _codecs = std::move(codecs);
    }
    void AttachUpdateService(ArchiveBackend &backend, const QString &format);
    void AttachProgressService(ArchiveBackend &backend);
    void AttachExtractService(ArchiveBackend &backend, const QString &format);
    void AttachReopenService(ArchiveBackend &backend, const QString &format);
    HRESULT ReOpen(IFolderFolder **metadata, IArchiveOpenCallback *callback);
    std::unique_ptr<IQtProgressTransport> CreateProgressTransport(CProgressDialog &dialog);
    std::unique_ptr<IQtProgressTransport> CreateProgressTransport(CProgressDialog &dialog,
                                                                  bool nativeWorker);
    // The caller owns the dialog until the native worker has joined.
    void SetUpdateProgress(CProgressSync *sync) { _sync = sync; }
    void SetCompressionOptions(const CompressionOptions &options) { _compressionOptions = options; }
    std::optional<QtArchiveTransferInputs> &TransferInputs() { return _transferInputs; }
    void ClearCompressionOptions() {
        _compressionOptions.reset();
        _deleteInputs.reset();
    }
    HRESULT ApplyOutputMTime(CWorkDirTempFile &file) const;
    HRESULT AdoptPublishedOwnedTemp(const CWorkDirTempFile &file);
    void CaptureOutputMTime(const CFiTime &time, bool defined);
    void CaptureDeleteInputs(const CDirItems &items, const CByteBuffer &processed);
    void DiscardDeleteInputs() { _deleteInputs.reset(); }
    HRESULT DeleteInputsAfterPublication(IUpdateDeletionCallback &callback);
    const std::optional<CompressionOptions> &CompressionSettings() const {
        return _compressionOptions;
    }
    bool HasUpdateService() const;
    bool HasNativeUpdateService() const;
    bool HasNativePropertyService() const;
    HRESULT SetNativeProperties(const wchar_t *const *names, const PROPVARIANT *values,
                                UInt32 count);
    bool RequiresOuterOpen() const;
    QString TransportFormat() const;
    bool HasExtractService() const;
    HRESULT Update(ISequentialOutStream *output, IInArchive *metadata, UInt32 count,
                   IArchiveUpdateCallback *callback);
    HRESULT Extract(IInArchive *metadata, bool nativeIndexes, const UInt32 *indices, UInt32 count,
                    Int32 testMode, IArchiveExtractCallback *callback);
};
