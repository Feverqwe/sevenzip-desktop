#pragma once

#include "Windows/FileFind.h"
#include "archive/opening/OpenArchive.h"
#include "archive/resources/archiveinputlayer.h"
#include "crypto/fscrypt/fscryptkeyrequest.h"
#include <QString>
#include <memory>
#include <mutex>

class ArchiveCodecCatalog;
CMyComPtr<IProgress> CreateArchiveOpenReadRequest(IOpenCallbackUI *callback);

// A source reader and its exact codec-index owner form one lifetime unit.
// The catalog is destroyed after all handler/volume/stream references.
class ArchiveNativeOpen final : public IOpenCallbackUI, public IFscryptKeyUI {
    std::shared_ptr<const ArchiveCodecCatalog> catalog_;
    IOpenCallbackUI *callback_ = nullptr;
    QString transportType_;
    CArchiveLink link_;
    CMyComPtr<IInStream> input_;
    QString sourcePath_, sourceIdentity_;
    NWindows::NFile::NFind::CFileInfo sourceInfo_;
    bool sourceStable_ = false;
    bool directoryReadRequested_ = false;
    std::mutex itemStreamMutex_;
    HRESULT Open_CheckBreak() override;
    HRESULT Open_SetTotal(const UInt64 *files, const UInt64 *bytes) override;
    HRESULT Open_SetCompleted(const UInt64 *files, const UInt64 *bytes) override;
    HRESULT Open_Finished() override;
    HRESULT Open_CryptoGetTextPassword(BSTR *password) override;
    HRESULT Open_FscryptKeys(BSTR *keys) override;

  public:
    explicit ArchiveNativeOpen(std::shared_ptr<const ArchiveCodecCatalog> catalog)
        : catalog_(std::move(catalog)) {}
    ~ArchiveNativeOpen();
    HRESULT Open(const QString &path, const QString &type, IOpenCallbackUI *callback,
                 const COpenOptions *requested = nullptr);
    HRESULT Close();
    // Reading a retained generation stays valid after publication. A writer
    // replacing its physical source must first check that generation is current.
    bool sourceIsCurrent() const;
    const CArc &arc() const { return *link_.GetArc(); }
    CArc &arc() { return link_.Arcs.Back(); }
    const CArchiveLink &archiveLink() const { return link_; }
    IInStream *inputStream() const { return input_; }
    const CCodecs &nativeCodecs() const;
    std::shared_ptr<const ArchiveCodecCatalog> codecCatalog() const { return catalog_; }
    std::mutex &itemStreamMutex() { return itemStreamMutex_; }
    bool hasNestedStream() const { return link_.Arcs.Size() > 1; }
    bool directoryReadRequested() const { return directoryReadRequested_; }
    bool hasVolumes() const { return !link_.VolumePaths.IsEmpty(); }
    bool hasCommentWriter() const;
    const CArcInfoEx *formatInfo() const;
    bool hasNativeRarExtractor() const;
    bool hasNativeHashExtractor() const;
    QString transportType() const { return transportType_; }
    QString resolvedOpenType() const;
    void CopyVolumeMetadata(CArchiveLink &link) const;
};

// Keep the reader generation alive until the operation callback is restored
// and its native stream lock is released.
class ArchiveNativeReadScope final {
    std::shared_ptr<ArchiveNativeOpen> reader_;
    std::lock_guard<std::mutex> lock_;
    ArchiveInputReadRequestScope request_;

  public:
    ArchiveNativeReadScope(std::shared_ptr<ArchiveNativeOpen> reader, IProgress *request);
    ArchiveNativeReadScope(const ArchiveNativeReadScope &) = delete;
    ArchiveNativeReadScope &operator=(const ArchiveNativeReadScope &) = delete;
};
