#include "archive/backend/archivebackend.h"
#include "archive/resources/archivereadpolicy.h"
#include "archive/resources/qtarchivereadoperation.h"
#include "localization/qtlanguageservices.h"
#include <QFile>
#include <QFileInfo>

void ArchiveBackend::finishSourceExtractOperation(const QtArchiveReadOperation &operation) {
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
    const bool recovery = sourceExtractRecovery_ || IsArchiveRecoveryFormat(operation.OpenedFormat);
    const bool partial = (lastResult_ == E_FAIL || lastResult_ == S_FALSE) && recovery &&
                         operation.OutputCreated && operation.OutputClosed &&
                         sourceExtractOperation_ > kOK;
    if (partial)
        lastResult_ = S_FALSE;
    if (operation.OutputCreated && lastResult_ != S_OK &&
        (!partial || QFileInfo(sourceExtractOutput_).size() <= 0))
        QFile::remove(sourceExtractOutput_);
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
    sourceExtractEntry_ = entry;
    sourceExtractAltStream_ = isAltStream;
    sourceExtractOutput_ = QFileInfo(output).absoluteFilePath();
    sourceExtractRecovery_ = IsArchiveRecoveryFormat(format);
    if (format.isEmpty() && hasListedSourceArchive(archive_))
        sourceExtractRecovery_ =
            IsArchiveRecoveryFormat(listedSourceArchives_.value(archive_).Format);
    startNativeRead(Job::SourceExtract, archive_, password, format);
}
