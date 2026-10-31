#include "archive/backend/archivebackend.h"
#include "archive/backend/archivebackend_nativeentries.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/resources/qtarchivedirectoryextract.h"
#include "archive/resources/qtarchivereadoperation.h"
#include "filemanager/ExtractCallback.h"
#include "localization/qtlanguageservices.h"
#include <QFileInfo>
#include <QMetaMethod>
#include <QPointer>

namespace {
QStringList ReadDiagnostics(const QtArchiveReadOperation &operation, HRESULT result,
                            const QString &archive) {
    QStringList diagnostics;
    if (result == E_ABORT)
        return diagnostics;
    if (!operation.CatalogDiagnostic.isEmpty() && operation.catalog()->result() != S_OK)
        diagnostics.append(operation.CatalogDiagnostic);
    diagnostics.append(operation.Messages);
    for (const auto &error : operation.OpenErrors) {
        QStringList detail;
        for (const auto &text :
             {GetOpenArcErrorMessage(error.ErrorFlags), GetOpenArcErrorMessage(error.WarningFlags)})
            if (!text.IsEmpty())
                detail.append(QString::fromWCharArray(text));
        if (!error.ErrorMessage.isEmpty())
            detail.append(error.ErrorMessage);
        if (!error.WarningMessage.isEmpty())
            detail.append(error.WarningMessage);
        diagnostics.append(error.Path + ": " + detail.join('\n'));
    }
    for (const auto &error : operation.Errors) {
        UString message;
        const auto path = error.Path.toStdWString();
        SetExtractErrorMessage(error.Result, error.Encrypted, path.c_str(), message);
        diagnostics.append(QString::fromWCharArray(message));
    }
    if (result != S_OK && diagnostics.isEmpty()) {
        UString message;
        if (result == S_FALSE) {
            const auto path = archive.toStdWString();
            SetExtractErrorMessage(NArchive::NExtract::NOperationResult::kIsNotArc, 0, path.c_str(),
                                   message);
        } else
            message = HResultToMessage(result);
        diagnostics.append(QString::fromWCharArray(message));
    }
    return diagnostics;
}
} // namespace

void ArchiveBackend::startNativeRead(
    Job job, const QString &archive, const QString &password, const QString &format,
    std::shared_ptr<const QtArchiveDirectoryExtractRequest> directory) {
    const auto catalog = codecCatalog();
    QtArchiveReadOperation::Work work;
    if (job == Job::List) {
        nativeListing_ = std::make_shared<NativeArchiveListing>();
        work = [listing = nativeListing_](const std::shared_ptr<ArchiveNativeOpen> &reader,
                                          IArchiveExtractCallback *callback) {
            return ReadNativeArchiveListing(*reader, *listing, callback);
        };
    } else if (job == Job::Test) {
        work = [](const std::shared_ptr<ArchiveNativeOpen> &reader,
                  IArchiveExtractCallback *callback) {
            return ArchiveBackend::extractNativeArchive(reader, nullptr, UInt32(-1), 1, callback);
        };
    }
    readOperation_ = std::make_unique<QtArchiveReadOperation>(
        archive, password, catalog, std::move(work),
        job == Job::List            ? QtArchiveReadOperation::Kind::List
        : job == Job::SourceExtract ? QtArchiveReadOperation::Kind::ExtractEntry
        : job == Job::Extract       ? QtArchiveReadOperation::Kind::ExtractDirectory
                                    : QtArchiveReadOperation::Kind::Test,
        format, configuredModule_);
    readOperation_->Directory = std::move(directory);
    if (job == Job::SourceExtract)
        readOperation_->Output = {sourceExtractEntry_, sourceExtractOutput_,
                                  sourceExtractAltStream_};
    const QPointer<QtArchiveReadOperation> operation(readOperation_.get());
    const quint64 progressGeneration = beginWorkerProgress();
    readOperation_->Progress = [this, progressGeneration](quint64 completed, quint64 total,
                                                          const QString &path) {
        const int percent = total ? int(qMin(100.0, 100.0 * completed / total)) : 0;
        postWorkerProgress(progressGeneration, percent, path);
    };
    readOperation_->PasswordRequired = [this, operation] {
        QMetaObject::invokeMethod(
            this,
            [this, operation] {
                if (operation && readOperation_.get() == operation.data() && !cancelled_)
                    emit passwordRequired();
            },
            Qt::QueuedConnection);
    };
    readOperation_->OverwriteRequired = [this, operation](const QString &path,
                                                          const ArchiveEntry &entry) {
        QMetaObject::invokeMethod(
            this,
            [this, operation, path, entry] {
                if (!operation || readOperation_.get() != operation.data() || cancelled_)
                    return;
                if (!isSignalConnected(
                        QMetaMethod::fromSignal(&ArchiveBackend::overwriteRequired))) {
                    readOperation_->provideOverwriteAnswer(-1);
                    return;
                }
                overwritePromptActive_ = true;
                emit overwriteRequired(path, entry);
            },
            Qt::QueuedConnection);
    };
    connect(readOperation_.get(), &QThread::finished, this, [this, operation] {
        if (operation && readOperation_.get() == operation.data())
            finishReadOperation();
    });
    cancelled_ = false;
    lastResult_ = E_FAIL;
    job_ = job;
    startWorkerPhase(*readOperation_);
}

void ArchiveBackend::finishReadOperation() {
    if (!readOperation_)
        return;
    readOperation_->wait();
    finishWorkerProgress();
    auto operation = std::move(readOperation_);
    auto listing = std::move(nativeListing_);
    const bool listingJob = job_ == Job::List;
    const bool sourceExtractJob = job_ == Job::SourceExtract;
    const bool extractJob = job_ == Job::Extract || sourceExtractJob;
    const QString path = archive_;
    lastResult_ = cancelled_ ? E_ABORT : operation->Result;
    if (sourceExtractJob)
        finishSourceExtractOperation(*operation);
    const HRESULT result = lastResult_;
    pendingPassword_.clear();
    overwritePromptActive_ = false;
    job_ = Job::None;
    const QPointer<ArchiveBackend> owner(this);
    const QStringList diagnostics = ReadDiagnostics(*operation, result, path);
    if (result != E_ABORT && operation->catalog() && operation->catalog()->result() == S_OK &&
        !codecCatalog()) {
        codecCatalog_.store(operation->catalog());
    }
    QStringList output = diagnostics;
    // A usable catalog can report a discovery note without making the
    // archive listing limited or adding an error to the final result.
    if (result != E_ABORT && !operation->CatalogDiagnostic.isEmpty() &&
        operation->catalog()->result() == S_OK)
        output.prepend(operation->CatalogDiagnostic);
    for (const auto &message : output) {
        emit outputReceived(message);
        if (!owner)
            return;
    }
    if (result == S_OK && listingJob && listing) {
        listing->Info.readLimitations = diagnostics.join('\n');
        entries_ = std::move(listing->Entries);
        listed_ = true;
        encrypted_ = listing->Info.encrypted;
        const QFileInfo source(path);
        listedSourceArchives_.insert(
            path, {source.size(), source.lastModified(), source.metadataChangeTime(), entries_,
                   encrypted_, !diagnostics.isEmpty(), listing->Info.format,
                   listing->Info.containerLevels, !listing->CanUpdate || !source.isWritable()});
        const auto entries = entries_;
        emit infoReady(listing->Info);
        if (!owner)
            return;
        emit entriesReady(entries);
        if (!owner)
            return;
    }
    emit finished(
        result == S_OK,
        result == S_OK
            ? listingJob
                  ? QCoreApplication::translate("archivebackend_completion", "Archive opened.")
              : extractJob
                  ? QCoreApplication::translate("archivebackend_completion",
                                                "Extraction completed.")
                  : QCoreApplication::translate("archivebackend_completion", "No errors found.")
        : result == E_ABORT ? QCoreApplication::translate("archivebackend", "Operation canceled.")
                            : diagnostics.join('\n'));
}
