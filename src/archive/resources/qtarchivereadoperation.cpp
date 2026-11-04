// Single-entry file-stream ownership adapted from UI/Client7z/Client7z.cpp,
// 9128b80e3a471108678e2b3ec3c985861c8d7b0f, Igor Pavlov, LGPL-2.1-or-later.
// Official Open/Extract handlers decode; the private read callback projects results.
#include "archive/resources/qtarchivereadoperation.h"
#include "7zip/Common/FileStreams.h"
#include "7zip/Common/LimitedStreams.h"
#include "Common/ComTry.h"
#include "Common/StringConvert.h"
#include "Windows/PropVariant.h"
#include "archive/backend/archivebackend.h"
#include "archive/backend/archivebackend_nativeentries.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/codecs/archivecodecprobe.h"
#include "archive/password/archivepassword.h"
#include "archive/resources/archivenativeopen.h"
#include "archive/resources/qtarchivedirectoryextract.h"
#include "archive/resources/qtarchivereadcallback_p.h"
#include <QDir>
#include <QMutexLocker>

namespace {

HRESULT FindOutputIndex(QtArchiveReadOperation &operation, const CArc &arc, UInt32 &selected) {
    UInt32 count;
    RINOK(arc.Archive->GetNumberOfItems(&count))
    selected = operation.Output.NativeIndex;
    if (selected != UInt32(-1)) {
        if (selected >= count)
            return E_INVALIDARG;
        bool directory = false, alternate = false;
        RINOK(Archive_IsItem_Dir(arc.Archive, selected, directory))
        RINOK(Archive_IsItem_AltStream(arc.Archive, selected, alternate))
        if (directory || alternate != operation.Output.AltStream)
            return E_INVALIDARG;
        return S_OK;
    }
    const auto entry = QDir::fromNativeSeparators(operation.Output.Entry);
    for (UInt32 index = 0; index < count; ++index) {
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
    return selected == UInt32(-1) ? E_INVALIDARG : S_OK;
}

HRESULT ExtractEntry(QtArchiveReadOperation &operation,
                     const std::shared_ptr<ArchiveNativeOpen> &reader,
                     QtArchiveReadCallback &callback) {
    UInt32 selected;
    RINOK(FindOutputIndex(operation, reader->arc(), selected))
    // Client7z's output-stream/Extract/Close ownership, restricted to one index.
    // This API writes a private spool: metadata and final path policy belong to
    // the source caller. Create_NEW also protects against a raced destination.
    CMyComPtr2_Create<ISequentialOutStream, COutFileStream> output;
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
      password_(password.isEmpty() ? std::nullopt : std::make_optional(ArchivePassword(password))) {
}

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
HRESULT QtArchiveReadOperation::resetPassword() {
    QMutexLocker lock(&passwordMutex_);
    password_.reset();
    passwordRequested_ = false;
    return checkpoint();
}
HRESULT QtArchiveReadOperation::password(BSTR *value) {
    if (!value)
        return E_INVALIDARG;
    *value = nullptr;
    for (;;) {
        RINOK(checkpoint())
        QMutexLocker lock(&passwordMutex_);
        if (password_)
            return password_->copyToBstr(value);
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
HRESULT QtArchiveReadOperation::process() {
    RINOK(checkpoint())
    if (!retainedReader_ && !catalog_) {
        catalog_ = ArchiveCodecProbe::LoadCatalog(moduleLocation_);
        CatalogDiagnostic = catalog_->error();
    }
    // Native entrypoints are not interruptible. Retain their image until
    // loading returns, then honor stop/pause before opening any input.
    RINOK(checkpoint())
    HRESULT result = retainedReader_ ? S_OK : catalog_->result();
    if (result != S_OK) {
        CatalogDiagnostic = catalog_->error();
        return result;
    }
    CMyComPtr2<IArchiveExtractCallback, QtArchiveReadCallback> callback;
    callback.SetFromCls(new QtArchiveReadCallback(*this));
    auto reader = retainedReader_;
    if (!reader) {
        reader = std::make_shared<ArchiveNativeOpen>(catalog_);
        result = reader->Open(path_, format_, callback.ClsPtr());
        if (result != S_OK)
            recordOpenError(path_, reader->archiveLink().NonOpen_ErrorInfo);
        for (unsigned i = 0; i < reader->archiveLink().Arcs.Size(); ++i) {
            const auto &arc = reader->archiveLink().Arcs[i];
            recordOpenError(QString::fromWCharArray(arc.Path), arc.ErrorInfo);
        }
    }
    if (result != S_OK)
        return result;
    OpenedFormat =
        QString::fromWCharArray(reader->nativeCodecs().GetFormatNamePtr(reader->arc().FormatIndex));
    Output.RecoveryFormat =
        Output.RecoveryFormat || reader->resolvedOpenType().split('.').contains("SparseBundle");
    callback->Attach(&reader->arc());
    switch (kind_) {
    case Kind::List:
        Listing = std::make_unique<NativeArchiveListing>();
        result = ReadNativeArchiveListing(*reader, *Listing, callback);
        break;
    case Kind::Test:
        result = ArchiveBackend::extractNativeArchive(reader, nullptr, UInt32(-1), 1, callback);
        break;
    case Kind::ExtractEntry:
        result = ExtractEntry(*this, reader, *callback.ClsPtr());
        break;
    case Kind::ExtractDirectory:
        result = ExtractQtArchiveDirectory(*this, reader, callback);
        break;
    }
    callback->Attach(nullptr);
    if (result == S_OK && (!Errors.isEmpty() || !Messages.isEmpty() ||
                           (kind_ != Kind::List && !OpenErrors.isEmpty())))
        return E_FAIL;
    return result;
}

void QtArchiveReadOperation::run() {
    try {
        Result = process();
    } catch (...) {
        Result = E_OUTOFMEMORY;
    }
}
