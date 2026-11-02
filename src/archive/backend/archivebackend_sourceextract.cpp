#include "archive/backend/archivebackend.h"
#include "archive/resources/archivereadpolicy.h"
#include "archive/resources/qtarchivereadoperation.h"
#include "localization/qtlanguageservices.h"
#include <QFile>
#include <QFileInfo>

HRESULT ArchiveBackend::finishSourceExtractOperation(const QtArchiveReadOperation &operation,
                                                     HRESULT result) {
    using namespace NArchive::NExtract::NOperationResult;
    sourceExtractOperation_ = operation.OutputOperation;
    if (sourceExtractOperation_ == kOK && !operation.Errors.isEmpty())
        sourceExtractOperation_ = operation.Errors.last().Result;
    if (sourceExtractOperation_ == kOK && !operation.OpenErrors.isEmpty()) {
        // The source stream API must not publish bytes as verified when opening
        // reported damage or trailing data. Preserve the CLI recovery contract
        // using CArcErrorInfo flags instead of inferring it from printed labels.
        UInt32 flags = 0;
        for (const auto &error : operation.OpenErrors)
            flags |= error.ErrorFlags | error.WarningFlags;
        sourceExtractOperation_ = flags & kpv_ErrorFlags_UnexpectedEnd  ? kUnexpectedEnd
                                  : flags & kpv_ErrorFlags_DataAfterEnd ? kDataAfterEnd
                                                                        : kHeadersError;
    }
    const bool recovery =
        operation.Output.RecoveryFormat || IsArchiveRecoveryFormat(operation.OpenedFormat);
    const bool partial = (result == E_FAIL || result == S_FALSE) && recovery &&
                         operation.OutputCreated && operation.OutputClosed &&
                         sourceExtractOperation_ > kOK;
    if (partial)
        result = S_FALSE;
    if (operation.OutputCreated && result != S_OK &&
        (!partial || QFileInfo(operation.Output.Path).size() <= 0))
        QFile::remove(operation.Output.Path);
    return result;
}

void ArchiveBackend::extractSourceEntry(const QString &archive, const QString &entry,
                                        const QString &output, const QString &format,
                                        const QString &password, bool isAltStream) {
    if (busy()) {
        emit finished(false, QCoreApplication::translate("archivebackend_sourceextract",
                                                         "Another operation is still running."));
        return;
    }
    lastResult_ = E_FAIL;
    sourceExtractOperation_ = -1;
    if (entry.isEmpty() || entry.contains(QChar(0)) || !QFileInfo(archive).isFile() ||
        output.isEmpty() || QFileInfo::exists(output) || format.contains(QChar(0))) {
        emit finished(false, QCoreApplication::translate("archivebackend_sourceextract",
                                                         "Invalid extraction file or stream."));
        return;
    }
    archive_ = QFileInfo(archive).absoluteFilePath();
    auto worker = std::make_unique<QtArchiveReadOperation>(
        archive_, password, codecCatalog(), QtArchiveReadOperation::Kind::ExtractEntry, format,
        configuredModule_);
    worker->Output = {entry, QFileInfo(output).absoluteFilePath(), isAltStream};
    worker->Output.RecoveryFormat = IsArchiveRecoveryFormat(format);
    if (format.isEmpty() && hasListedSourceArchive(archive_))
        worker->Output.RecoveryFormat =
            IsArchiveRecoveryFormat(listedSourceArchives_.value(archive_).Format);
    startReadOperation(Job::SourceExtract, std::move(worker));
}
