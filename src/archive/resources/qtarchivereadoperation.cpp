// Callback item/block projection adapted from UI/Common/ArchiveExtractCallback.cpp,
// b88c6bc09339de32f7d18bbab900686b244f42ba, Igor Pavlov, LGPL-2.1-or-later.
// Test decoding remains in the official Open/Extract handlers; Qt owns the worker.
// Single-entry file-stream ownership also follows UI/Client7z/Client7z.cpp,
// 9128b80e3a471108678e2b3ec3c985861c8d7b0f, Igor Pavlov, LGPL-2.1-or-later.
#include "archive/resources/qtarchivereadoperation.h"
#include "7zip/Common/FileStreams.h"
#include "7zip/Common/LimitedStreams.h"
#include "7zip/IPassword.h"
#include "Common/ComTry.h"
#include "Common/StringConvert.h"
#include "Windows/PropVariant.h"
#include "archive/backend/archivebackend.h"
#include "archive/backend/archivebackend_nativeentries.h"
#include "archive/callbacks/ArchiveOpenCallback.h"
#include "archive/callbacks/IFileExtractCallback.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/codecs/archivecodecprobe.h"
#include "archive/resources/archivenativeopen.h"
#include "archive/resources/qtarchivedirectoryextract.h"
#include <QDir>
#include <QMutexLocker>

namespace {
class CTestCallback final : public IArchiveExtractCallback,
                            public IArchiveExtractCallbackMessage2,
                            public ICryptoGetTextPassword,
                            public IArchiveRequestMemoryUseCallback,
                            public IOpenCallbackUI,
                            public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_5(IArchiveExtractCallback, IProgress, ICryptoGetTextPassword,
                         IArchiveRequestMemoryUseCallback, IArchiveExtractCallbackMessage2)
    QtArchiveReadOperation &operation_;
    const CArc *archive_ = nullptr;
    UInt64 total_ = 0;
    QString path_;
    bool encrypted_ = false;
    UInt32 outputIndex_ = UInt32(-1);
    UInt32 currentIndex_ = UInt32(-1);
    CMyComPtr<ISequentialOutStream> output_;
    QMutex stateMutex_;
    HRESULT ReadItem(UInt32 index, QString &path, bool &encrypted) {
        if (!archive_)
            return E_FAIL;
        CReadArcItem item;
        RINOK(archive_->GetItem(index, item))
        path = QString::fromWCharArray(item.Path);
        return Archive_GetItemBoolProp(archive_->Archive, index, kpidEncrypted, encrypted);
    }

  public:
    explicit CTestCallback(QtArchiveReadOperation &operation) : operation_(operation) {}
    void Attach(const CArc *archive) { archive_ = archive; }
    void SetOutput(UInt32 index, ISequentialOutStream *output) {
        outputIndex_ = index;
        output_ = output;
    }
    Z7_COM7F_IMF(SetTotal(UInt64 total)) override {
        {
            QMutexLocker lock(&stateMutex_);
            total_ = total;
        }
        return operation_.checkpoint();
    }
    Z7_COM7F_IMF(SetCompleted(const UInt64 *completed)) override {
        UInt64 total;
        QString path;
        {
            QMutexLocker lock(&stateMutex_);
            total = total_;
            path = path_;
        }
        if (completed)
            operation_.report(*completed, total, path);
        return operation_.checkpoint();
    }
    Z7_COM7F_IMF(GetStream(UInt32 index, ISequentialOutStream **stream, Int32 mode)) override {
        if (!stream || !archive_)
            return E_INVALIDARG;
        *stream = nullptr;
        QString path;
        bool encrypted = false;
        RINOK(ReadItem(index, path, encrypted)) {
            QMutexLocker lock(&stateMutex_);
            path_ = path;
            encrypted_ = encrypted;
            currentIndex_ = index;
        }
        RINOK(operation_.checkpoint())
        // Client7z::CArchiveExtractCallback::GetStream returns file output only
        // in extract mode. A solid handler can also request skipped siblings.
        if (index == outputIndex_ && mode == NArchive::NExtract::NAskMode::kExtract) {
            CMyComPtr<ISequentialOutStream> output = output_;
            *stream = output.Detach();
        }
        return S_OK;
    }
    Z7_COM7F_IMF(PrepareOperation(Int32)) override { return operation_.checkpoint(); }
    Z7_COM7F_IMF(SetOperationResult(Int32 result)) override {
        {
            QMutexLocker lock(&stateMutex_);
            if (output_ && currentIndex_ == outputIndex_)
                operation_.OutputOperation = result;
        }
        // Extract's HRESULT alone does not account for CRC/partial item results.
        if (result != NArchive::NExtract::NOperationResult::kOK) {
            QMutexLocker lock(&stateMutex_);
            operation_.Errors.append({path_, result, encrypted_});
        }
        return operation_.checkpoint();
    }
    Z7_COM7F_IMF(ReportExtractResult(UInt32 indexType, UInt32 index, Int32 result)) override {
        // ArchiveExtractCallback::ReportExtractResult's item/block projection.
        QString path;
        bool encrypted = false;
        if (indexType == NArchive::NEventIndexType::kInArcIndex && index != UInt32(-1)) {
            RINOK(ReadItem(index, path, encrypted))
        } else
            path = '#' + QString::number(index);
        if (result != NArchive::NExtract::NOperationResult::kOK) {
            QMutexLocker lock(&stateMutex_);
            operation_.Errors.append({path, result, encrypted});
            if (output_ && indexType == NArchive::NEventIndexType::kInArcIndex &&
                index == outputIndex_)
                operation_.OutputOperation = result;
        }
        return operation_.checkpoint();
    }
    Z7_COM7F_IMF(CryptoGetTextPassword(BSTR *value)) override { return operation_.password(value); }
    Z7_COM7F_IMF(RequestMemoryUse(UInt32 flags, UInt32, UInt32, const wchar_t *, UInt64 required,
                                  UInt64 *allowed, UInt32 *answer))
    override {
        RINOK(operation_.checkpoint())
        if (!allowed || !answer)
            return E_INVALIDARG;
        // ExtractCallback::RequestMemoryUse's non-dialog fast path: preserve
        // the decoder's already allowed budget, without silently increasing it.
        if ((flags & NRequestMemoryUseFlags::k_IsReport) == 0 && required <= *allowed) {
            *answer = NRequestMemoryAnswerFlags::k_Allow;
            return S_OK;
        }
        return E_OUTOFMEMORY; // This non-dialog API has no memory-limit consent service.
    }
    HRESULT Open_CheckBreak() override { return operation_.checkpoint(); }
    HRESULT Open_SetTotal(const UInt64 *, const UInt64 *bytes) override {
        return bytes ? SetTotal(*bytes) : operation_.checkpoint();
    }
    HRESULT Open_SetCompleted(const UInt64 *, const UInt64 *bytes) override {
        return SetCompleted(bytes);
    }
    HRESULT Open_Finished() override { return operation_.checkpoint(); }
    HRESULT Open_CryptoGetTextPassword(BSTR *value) override { return operation_.password(value); }
};

HRESULT ExtractEntry(QtArchiveReadOperation &operation,
                     const std::shared_ptr<ArchiveNativeOpen> &reader, CTestCallback &callback) {
    const CArc &arc = reader->arc();
    UInt32 count;
    RINOK(arc.Archive->GetNumberOfItems(&count))
    UInt32 selected = operation.Output.NativeIndex;
    if (selected != UInt32(-1)) {
        if (selected >= count)
            return E_INVALIDARG;
        bool directory = false, alternate = false;
        RINOK(Archive_IsItem_Dir(arc.Archive, selected, directory))
        RINOK(Archive_IsItem_AltStream(arc.Archive, selected, alternate))
        if (directory || alternate != operation.Output.AltStream)
            return E_INVALIDARG;
    }
    const auto entry = QDir::fromNativeSeparators(operation.Output.Entry);
    for (UInt32 index = 0; operation.Output.NativeIndex == UInt32(-1) && index < count; ++index) {
        RINOK(operation.checkpoint())
        UString path;
        RINOK(arc.GetItem_Path(index, path))
        if (QDir::fromNativeSeparators(QString::fromWCharArray(path.Ptr(), path.Len())) != entry)
            continue;
        bool directory = false, alternate = false;
        RINOK(Archive_IsItem_Dir(arc.Archive, index, directory))
        RINOK(Archive_IsItem_AltStream(arc.Archive, index, alternate))
        if (directory || alternate != operation.Output.AltStream)
            continue;
        // A name-only public request cannot disambiguate duplicate members.
        if (selected != UInt32(-1))
            return E_INVALIDARG;
        selected = index;
    }
    if (selected == UInt32(-1))
        return E_INVALIDARG;
    // Client7z's output-stream/Extract/Close ownership, restricted to one index.
    // This API writes a private spool: metadata and final path policy belong to
    // the source caller. Create_NEW also protects against a raced destination.
    CMyComPtr2<ISequentialOutStream, COutFileStream> output;
    output.Create_if_Empty();
    if (!output->Create_NEW(us2fs(operation.Output.Path.toStdWString().c_str())))
        return GetLastError_noZero_HRESULT();
    operation.OutputCreated = true;
    CMyComPtr<ISequentialOutStream> bounded = output.Interface();
    if (operation.Output.Limit != UInt64(-1)) {
        auto *limited = new CLimitedSequentialOutStream;
        bounded = limited;
        limited->SetStream(output.Interface());
        limited->Init(operation.Output.Limit);
    }
    callback.SetOutput(selected, bounded);
    const HRESULT result = ArchiveBackend::extractNativeArchive(reader, &selected, 1, 0, &callback);
    callback.SetOutput(UInt32(-1), nullptr);
    const HRESULT closed = output->Close();
    operation.OutputClosed = closed == S_OK;
    if (result != S_OK)
        return result;
    if (closed != S_OK)
        return closed;
    return operation.OutputOperation == NArchive::NExtract::NOperationResult::kOK ? S_OK : E_FAIL;
}
} // namespace

QtArchiveReadOperation::QtArchiveReadOperation(QString path, QString password,
                                               std::shared_ptr<const ArchiveCodecCatalog> catalog,
                                               Kind kind, QString format, QString moduleLocation)
    : catalog_(std::move(catalog)), path_(std::move(path)), kind_(kind), format_(std::move(format)),
      moduleLocation_(std::move(moduleLocation)),
      password_(password.isEmpty() ? std::nullopt : std::make_optional(std::move(password))) {}

QtArchiveReadOperation::QtArchiveReadOperation(std::shared_ptr<ArchiveNativeOpen> reader,
                                               QString password)
    : QtArchiveReadOperation({}, std::move(password), {}, Kind::ExtractEntry) {
    retainedReader_ = std::move(reader);
}

QtArchiveReadOperation::~QtArchiveReadOperation() = default;

void QtArchiveReadOperation::cancel() {
    progress_.cancel();
    {
        QMutexLocker lock(&passwordMutex_);
        passwordReady_.wakeAll();
    }
    QMutexLocker lock(&overwriteMutex_);
    overwriteReady_.wakeAll();
}

void QtArchiveReadOperation::provideOverwriteAnswer(int answer) {
    QMutexLocker lock(&overwriteMutex_);
    overwriteAnswer_ = answer;
    overwriteReady_.wakeAll();
}

HRESULT QtArchiveReadOperation::askOverwrite(const OverwriteQuestion &question, Int32 *answer) {
    if (!answer || !OverwriteRequired)
        return E_FAIL;
    {
        QMutexLocker lock(&overwriteMutex_);
        overwriteAnswer_.reset();
    }
    RINOK(checkpoint())
    OverwriteRequired(question);
    for (;;) {
        RINOK(checkpoint())
        QMutexLocker lock(&overwriteMutex_);
        if (overwriteAnswer_) {
            *answer = *overwriteAnswer_;
            return *answer >= NOverwriteAnswer::kYes && *answer <= NOverwriteAnswer::kCancel
                       ? S_OK
                       : E_FAIL;
        }
        overwriteReady_.wait(&overwriteMutex_, 50);
    }
}
void QtArchiveReadOperation::setPaused(bool paused) { progress_.setPaused(paused); }
HRESULT QtArchiveReadOperation::checkpoint() { return progress_.checkpoint(); }
void QtArchiveReadOperation::providePassword(const QString &password) {
    QMutexLocker lock(&passwordMutex_);
    password_ = password;
    passwordReady_.wakeAll();
}
HRESULT QtArchiveReadOperation::password(BSTR *value) {
    if (!value)
        return E_INVALIDARG;
    *value = nullptr;
    for (;;) {
        RINOK(checkpoint())
        QMutexLocker lock(&passwordMutex_);
        if (password_)
            return StringToBstr(password_->toStdWString().c_str(), value);
        if (!passwordRequested_) {
            if (!PasswordRequired)
                return E_ABORT;
            passwordRequested_ = true;
            lock.unlock();
            PasswordRequired();
            continue;
        }
        passwordReady_.wait(&passwordMutex_, 50);
    }
}
void QtArchiveReadOperation::report(quint64 completed, quint64 total, const QString &path) {
    if (Progress)
        Progress(completed, total, path);
}
void QtArchiveReadOperation::recordOpenError(const QString &path, const CArcErrorInfo &error) {
    if (error.IsThereErrorOrWarning())
        OpenErrors.append({path, error.GetErrorFlags(), error.GetWarningFlags(),
                           QString::fromWCharArray(error.ErrorMessage),
                           QString::fromWCharArray(error.WarningMessage)});
}
void QtArchiveReadOperation::run() {
    try {
        Result = checkpoint();
        if (Result != S_OK)
            return;
        if (!retainedReader_ && !catalog_) {
            catalog_ = ArchiveCodecProbe::LoadCatalog(moduleLocation_);
            CatalogDiagnostic = catalog_->error();
        }
        // Native entrypoints are not interruptible. Retain their image until
        // loading returns, then honor stop/pause before opening any input.
        Result = checkpoint();
        if (Result != S_OK)
            return;
        Result = retainedReader_ ? S_OK : catalog_->result();
        if (Result != S_OK) {
            CatalogDiagnostic = catalog_->error();
            return;
        }
        auto *callback = new CTestCallback(*this);
        CMyComPtr<IArchiveExtractCallback> retained = callback;
        auto reader = retainedReader_;
        if (!reader) {
            reader = std::make_shared<ArchiveNativeOpen>(catalog_);
            Result = reader->Open(path_, format_, callback);
            if (Result != S_OK)
                recordOpenError(path_, reader->archiveLink().NonOpen_ErrorInfo);
            for (unsigned i = 0; i < reader->archiveLink().Arcs.Size(); ++i) {
                const auto &arc = reader->archiveLink().Arcs[i];
                recordOpenError(QString::fromWCharArray(arc.Path), arc.ErrorInfo);
            }
        }
        if (Result != S_OK)
            return;
        OpenedFormat = QString::fromWCharArray(
            reader->nativeCodecs().GetFormatNamePtr(reader->arc().FormatIndex));
        callback->Attach(&reader->arc());
        switch (kind_) {
        case Kind::List:
            Listing = std::make_unique<NativeArchiveListing>();
            Result = ReadNativeArchiveListing(*reader, *Listing, callback);
            break;
        case Kind::Test:
            Result = ArchiveBackend::extractNativeArchive(reader, nullptr, UInt32(-1), 1, callback);
            break;
        case Kind::ExtractEntry:
            Result = ExtractEntry(*this, reader, *callback);
            break;
        case Kind::ExtractDirectory:
            Result = ExtractQtArchiveDirectory(*this, reader, callback);
            break;
        }
        callback->Attach(nullptr);
        if (Result == S_OK && (!Errors.isEmpty() || !Messages.isEmpty() ||
                               (kind_ != Kind::List && !OpenErrors.isEmpty())))
            Result = E_FAIL;
    } catch (...) {
        Result = E_OUTOFMEMORY;
    }
}
