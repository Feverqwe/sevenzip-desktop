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
    auto worker = std::make_unique<QtArchiveReadOperation>(
        archive, password, catalog,
        job == Job::List      ? QtArchiveReadOperation::Kind::List
        : job == Job::Extract ? QtArchiveReadOperation::Kind::ExtractDirectory
                              : QtArchiveReadOperation::Kind::Test,
        format, configuredModule_);
    worker->Directory = std::move(directory);
    startReadOperation(job, std::move(worker));
}

void ArchiveBackend::startReadOperation(Job job, std::unique_ptr<QtArchiveReadOperation> worker) {
    readOperation_ = std::move(worker);
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
    readOperation_->OverwriteRequired = [this, operation](const OverwriteQuestion &question) {
        QMetaObject::invokeMethod(
            this,
            [this, operation, question] {
                if (!operation || readOperation_.get() != operation.data() || cancelled_)
                    return;
                if (!isSignalConnected(
                        QMetaMethod::fromSignal(&ArchiveBackend::overwriteRequired))) {
                    readOperation_->provideOverwriteAnswer(-1);
                    return;
                }
                overwritePromptActive_ = true;
                emit overwriteRequired(question);
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
    auto listing = std::move(operation->Listing);
    const bool listingJob = job_ == Job::List;
    const bool sourceExtractJob = job_ == Job::SourceExtract;
    const bool extractJob = job_ == Job::Extract || sourceExtractJob;
    const QString path = archive_;
    lastResult_ = cancelled_ ? E_ABORT : operation->Result;
    if (sourceExtractJob)
        finishSourceExtractOperation(*operation);
    const HRESULT result = lastResult_;
    overwritePromptActive_ = false;
    job_ = Job::None;
    const QPointer<ArchiveBackend> owner(this);
    const QStringList diagnostics = ReadDiagnostics(*operation, result, path);
    if (result != E_ABORT && operation->catalog() && operation->catalog()->result() == S_OK &&
        !codecCatalog()) {
        codecCatalog_.store(operation->catalog());
    }
    const bool publishListing = result == S_OK && listingJob && listing;
    if (publishListing) {
        listing->Info.readLimitations = diagnostics.join('\n');
        entries_ = std::move(listing->Entries);
        const QFileInfo source(path);
        listedSourceArchives_.insert(path,
                                     {source.size(), source.lastModified(),
                                      source.metadataChangeTime(), entries_, listing->Info.format});
    }
    const QList<ArchiveEntry> entries = publishListing ? entries_ : QList<ArchiveEntry>();
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
    if (publishListing) {
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
