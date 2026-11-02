// Original Common/Extract.cpp and Agent/Agent.cpp extraction entry sequences;
// 7-Zip 9128b80e3a471108678e2b3ec3c985861c8d7b0f, Igor Pavlov,
// LGPL-2.1-or-later. Qt supplies joined-worker interaction and bounded output.
#include "archive/resources/qtarchivedirectoryextract.h"
#include "7zip/IPassword.h"
#include "Common/StringConvert.h"
#include "archive/backend/archivebackend_nativeentries.h"
#include "archive/operations/Extract.h"
#include "archive/resources/archivenativeopen.h"
#include "archive/resources/qtarchivereadoperation.h"
#include "archive/resources/qtextractpathpolicy.h"
#include <QDir>
#include <QMutexLocker>

namespace {
class CDirectoryCallback final : public IFolderArchiveExtractCallback,
                                 public IFolderArchiveExtractCallback2,
                                 public ICryptoGetTextPassword,
                                 public IArchiveRequestMemoryUseCallback,
                                 public IExtractCallbackUI,
                                 public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_5(IFolderArchiveExtractCallback, IProgress, IFolderArchiveExtractCallback2,
                         ICryptoGetTextPassword, IArchiveRequestMemoryUseCallback)
    QtArchiveReadOperation &operation_;
    IArchiveExtractCallback *progress_;
    QMutex mutex_;
    UInt64 total_ = 0;
    QString path_;

  public:
    CDirectoryCallback(QtArchiveReadOperation &operation, IArchiveExtractCallback *progress)
        : operation_(operation), progress_(progress) {}
    Z7_COM7F_IMF(SetTotal(UInt64 total)) override {
        QMutexLocker lock(&mutex_);
        total_ = total;
        return operation_.checkpoint();
    }
    Z7_COM7F_IMF(SetCompleted(const UInt64 *completed)) override {
        {
            QMutexLocker lock(&mutex_);
            if (completed)
                operation_.report(*completed, total_, path_);
        }
        return operation_.checkpoint();
    }
    Z7_COM7F_IMF(AskOverwrite(const wchar_t *existing, const FILETIME *existingTime,
                              const UInt64 *existingSize, const wchar_t *name, const FILETIME *time,
                              const UInt64 *size, Int32 *answer))
    override {
        // RejectExisting also guards collisions created after initial validation.
        if (operation_.Directory->Options.overwriteMode ==
            ExtractionOptions::OverwriteMode::RejectExisting)
            return E_FAIL;
        const OverwriteQuestion question{
            NativeOverwriteFile(QString::fromWCharArray(existing), existingTime, existingSize,
                                true),
            NativeOverwriteFile(QString::fromWCharArray(name), time, size, false)};
        RINOK(operation_.askOverwrite(question, answer))
        // The UI may have been open while the destination changed. Recheck
        // before the source callback deletes/renames the existing output.
        QString error;
        if (!ArchiveBackend::validateExtractionOutput(operation_.Directory->Destination,
                                                      question.Existing.Path, false, &error)) {
            operation_.Messages.append(error);
            return E_INVALIDARG;
        }
        return S_OK;
    }
    Z7_COM7F_IMF(PrepareOperation(const wchar_t *name, Int32, Int32, const UInt64 *)) override {
        QMutexLocker lock(&mutex_);
        path_ = QString::fromWCharArray(name);
        return operation_.checkpoint();
    }
    Z7_COM7F_IMF(MessageError(const wchar_t *message)) override {
        QMutexLocker lock(&mutex_);
        operation_.Messages.append(QString::fromWCharArray(message));
        return operation_.checkpoint();
    }
    Z7_COM7F_IMF(SetOperationResult(Int32 result, Int32 encrypted)) override {
        QMutexLocker lock(&mutex_);
        if (result != NArchive::NExtract::NOperationResult::kOK)
            operation_.Errors.append({path_, result, encrypted != 0});
        return operation_.checkpoint();
    }
    Z7_COM7F_IMF(ReportExtractResult(Int32 result, Int32 encrypted, const wchar_t *name)) override {
        QMutexLocker lock(&mutex_);
        if (result != NArchive::NExtract::NOperationResult::kOK)
            operation_.Errors.append({QString::fromWCharArray(name), result, encrypted != 0});
        return operation_.checkpoint();
    }
    Z7_COM7F_IMF(CryptoGetTextPassword(BSTR *value)) override { return operation_.password(value); }
    Z7_COM7F_IMF(RequestMemoryUse(UInt32 flags, UInt32 indexType, UInt32 index, const wchar_t *path,
                                  UInt64 required, UInt64 *allowed, UInt32 *answer))
    override {
        CMyComPtr<IArchiveRequestMemoryUseCallback> callback;
        RINOK(progress_->QueryInterface(IID_IArchiveRequestMemoryUseCallback, (void **)&callback))
        return callback->RequestMemoryUse(flags, indexType, index, path, required, allowed, answer);
    }
    HRESULT BeforeOpen(const wchar_t *, bool) override { return operation_.checkpoint(); }
    HRESULT OpenResult(const CCodecs *, const CArchiveLink &, const wchar_t *,
                       HRESULT result) override {
        return result;
    }
    HRESULT ThereAreNoFiles() override { return operation_.checkpoint(); }
    HRESULT ExtractResult(HRESULT result) override { return result; }
    HRESULT SetPassword(const UString &) override { return S_OK; }
};

class CDirectoryExtract final : public IQtArchiveExtractService {
    QtArchiveReadOperation &operation_;
    const std::shared_ptr<ArchiveNativeOpen> &reader_;
    const NativeArchiveListing &listing_;

  public:
    CDirectoryExtract(QtArchiveReadOperation &operation,
                      const std::shared_ptr<ArchiveNativeOpen> &reader,
                      const NativeArchiveListing &listing)
        : operation_(operation), reader_(reader), listing_(listing) {}
    HRESULT Extract(const UInt32 *indices, UInt32 count, Int32 testMode,
                    IArchiveExtractCallback *callback) override {
        QStringList selected;
        for (UInt32 n = 0; n < count; ++n) {
            RINOK(operation_.checkpoint())
            UString path;
            RINOK(reader_->arc().GetItem_Path(indices[n], path))
            selected.append(QString::fromWCharArray(path));
        }
        const auto &request = *operation_.Directory;
        QString error;
        if (!ArchiveBackend::validateExtractionEntries(listing_.Entries, request.Destination,
                                                       selected, request.Options, &error)) {
            operation_.Messages.append(error);
            return E_INVALIDARG;
        }
        return ArchiveBackend::extractNativeArchive(reader_, indices, count, testMode, callback);
    }
};

void AddLiteral(NWildcard::CCensorNode &censor, const QString &path, bool include) {
    NWildcard::CItem item;
    // SDK archive names use UTF-16 units even when the host wchar_t is 32-bit.
    SplitPathToParts(
        MultiByteToUnicodeString(QDir::fromNativeSeparators(path).toUtf8().constData(), CP_UTF8),
        item.PathParts);
    item.ForFile = item.ForDir = true;
    item.Recursive = false;
    item.WildcardMatching = false;
    censor.AddItem(include, item);
}

CExtractOptions MakeDirectoryExtractOptions(const QtArchiveDirectoryExtractRequest &request,
                                            CQtExtractPathPolicy &policy) {
    CExtractOptions options;
    options.NtOptions.QtPathPolicy = &policy;
    options.OutputDir = us2fs(request.Destination.toStdWString().c_str());
    options.OutDirMode = NExtractOutDirMode::k_Direct;
    options.PathMode = request.Options.pathMode == ExtractionOptions::PathMode::NoPaths
                           ? NExtract::NPathMode::kNoPaths
                       : request.Options.pathMode == ExtractionOptions::PathMode::CurrentPaths
                           ? NExtract::NPathMode::kCurPaths
                           : NExtract::NPathMode::kFullPaths;
    switch (request.Options.overwriteMode) {
    case ExtractionOptions::OverwriteMode::Ask:
    case ExtractionOptions::OverwriteMode::RejectExisting:
        options.OverwriteMode = NExtract::NOverwriteMode::kAsk;
        break;
    case ExtractionOptions::OverwriteMode::SkipExisting:
        options.OverwriteMode = NExtract::NOverwriteMode::kSkip;
        break;
    case ExtractionOptions::OverwriteMode::OverwriteExisting:
        options.OverwriteMode = NExtract::NOverwriteMode::kOverwrite;
        break;
    case ExtractionOptions::OverwriteMode::RenameExtracted:
        options.OverwriteMode = NExtract::NOverwriteMode::kRename;
        break;
    case ExtractionOptions::OverwriteMode::RenameExisting:
        options.OverwriteMode = NExtract::NOverwriteMode::kRenameExisting;
        break;
    }
    return options;
}
} // namespace

HRESULT ExtractQtArchiveDirectory(QtArchiveReadOperation &operation,
                                  const std::shared_ptr<ArchiveNativeOpen> &reader,
                                  IArchiveExtractCallback *progress) {
    const auto *format = reader->formatInfo();
    if (!operation.Directory || (format && format->Flags_HashHandler()))
        return E_NOTIMPL; // Common/Extract and Agent forbid extracting a checksum list.
    const auto &request = *operation.Directory;
    // Common/Extract's outer-file fallback for entries without an mtime.
    NWindows::NFile::NFind::CFileInfo file;
    if (!file.Find_FollowLink(us2fs(reader->archiveLink().Arcs[0].Path)) || file.IsDir())
        return GetLastError_noZero_HRESULT();
    reader->arc().MTime.Def = true;
    reader->arc().MTime.Set_From_FiTime(file.MTime);
    NativeArchiveListing listing;
    RINOK(ReadNativeArchiveListing(*reader, listing, progress))
    NWildcard::CCensorNode censor;
    if (request.Selected.isEmpty())
        censor.Add_Wildcard();
    else
        for (const auto &path : request.Selected)
            AddLiteral(censor, path, true);
    // Copy's public selection is literal. Move also needs the selected host's
    // streams: its source deletion includes them, just like Agent::CopyTo.
    if (request.Options.Purpose == QtArchiveExtractPurpose::Copy)
        for (const auto &entry : listing.Entries)
            if (entry.properties.value(QStringLiteral("Alternate Stream")) == "+" &&
                !request.Selected.isEmpty() && !request.Selected.contains(entry.path))
                AddLiteral(censor, entry.path, false);

    CQtExtractPathPolicy policy(request.Destination);
    const CExtractOptions options = MakeDirectoryExtractOptions(request, policy);
    UStringVector parts;
    const bool streamFolder =
        request.Options.pathMode == ExtractionOptions::PathMode::CurrentPaths &&
        request.Options.currentFolder.endsWith(':');
    if (request.Options.pathMode == ExtractionOptions::PathMode::CurrentPaths) {
        QString folder =
            streamFolder ? request.Options.currentFolder.chopped(1) : request.Options.currentFolder;
        folder.replace('\\', '/');
        SplitPathToParts(MultiByteToUnicodeString(folder.toUtf8().constData(), CP_UTF8), parts);
    }
    CMyComPtr2_Create<IArchiveExtractCallback, CArchiveExtractCallback> callback;
    callback->QtExtractPurpose = request.Options.Purpose;
    callback->InitForMulti(false, options.PathMode, options.OverwriteMode, options.ZoneMode, false);
    callback->InitBeforeNewArchive();
    CMyComPtr2<IFolderArchiveExtractCallback, CDirectoryCallback> ui;
    ui.SetFromCls(new CDirectoryCallback(operation, progress));
    CDirectoryExtract extract(operation, reader, listing);
    UInt64 processed = 0;
    UString error;
    const HRESULT result =
        DecompressArchive(const_cast<CCodecs *>(&reader->nativeCodecs()), reader->archiveLink(),
                          reader->arc().GetEstmatedPhySize(), censor, options, false, ui.ClsPtr(),
                          ui, callback.ClsPtr(), error, processed, &extract, &parts, streamFolder);
    if (!error.IsEmpty())
        operation.Messages.append(QString::fromWCharArray(error));
    if (result == S_OK && operation.Messages.isEmpty() && operation.Errors.isEmpty() &&
        operation.OpenErrors.isEmpty())
        operation.ExtractStatus = callback->QtExtractStatus;
    return result;
}
