#include "operations/qtoperationprotocol.h"
#include "rep_operation_merged.h"
#include <QCoreApplication>
#include <QDir>
#include <QStringConverter>

namespace {
bool TextIsRepresentable(const QString &text) {
    QStringEncoder encoder(QStringEncoder::Utf8);
    const auto encoded = encoder(text);
    Q_UNUSED(encoded);
    return !encoder.hasError() && !text.contains(QChar::Null);
}
bool PathIsRepresentable(const QString &path) {
    return TextIsRepresentable(path) && QDir::isAbsolutePath(path);
}
bool RelativeHashPath(QString path) {
    if (!TextIsRepresentable(path) || path.isEmpty() || !QDir::isRelativePath(path))
        return false;
#ifdef Q_OS_WIN
    path.replace('\\', '/');
#endif
    const auto parts = path.split('/');
    return !parts.contains("..") && !parts.contains(".");
}
} // namespace

std::expected<void, QString> ValidateOperationJob(const QtOperationJob &job) {
    using Command = QtOperationJob::Command;
    bool valid = true;
    switch (job.Operation) {
    case Command::Test:
        break;
    case Command::Extract:
        valid = PathIsRepresentable(job.Destination) &&
                (job.WriteZone <= 2 || job.WriteZone == quint32(-1));
        break;
    case Command::Compress:
        valid = PathIsRepresentable(job.ArchivePath) && TextIsRepresentable(job.ArchiveType);
        break;
    case Command::Hash:
        valid = TextIsRepresentable(job.HashMethod);
        if (!job.HashBasePrefix.isEmpty())
            valid = valid && PathIsRepresentable(job.HashBasePrefix) &&
                    (job.HashBasePrefix.endsWith('/')
#ifdef Q_OS_WIN
                     || job.HashBasePrefix.endsWith('\\') || job.HashBasePrefix.endsWith(':')
#endif
                    );
        break;
    default:
        valid = false;
    }
    valid = valid && !job.Paths.isEmpty();
    // Bound accepted application input independently of QtRO's wire framing.
    // Count UTF-16 data and per-path overhead before publishing the request.
    quint64 size =
        quint64(job.Destination.size() + job.ArchivePath.size() + job.ArchiveType.size() +
                job.HashMethod.size() + job.HashBasePrefix.size()) *
        2;
    for (const auto &path : job.Paths) {
        size += quint64(path.size()) * 2 + 4;
        valid = valid && (job.Operation == Command::Hash && !job.HashBasePrefix.isEmpty()
                              ? RelativeHashPath(path)
                              : PathIsRepresentable(path));
    }
    if (!valid || size > 64 * 1024 * 1024) {
        return std::unexpected(
            QCoreApplication::translate("QtOperation", "Invalid operation message."));
    }
    return {};
}

OperationRequest OperationRequestFromJob(const QtOperationJob &job) {
    return OperationRequest(int(job.Operation), job.Paths, job.YesToAll, job.Destination,
                            job.ShowDialog, job.ElimDup, job.WriteZone, job.ArchivePath,
                            job.ArchiveType, job.AddExtension, job.EMail, job.HashMode,
                            job.HashMethod, job.HashBasePrefix, job.HashEnterToDirs);
}

QtOperationJob OperationJobFromRequest(const OperationRequest &request) {
    QtOperationJob job;
    job.Operation = QtOperationJob::Command(request.operation());
    job.Paths = request.paths();
    job.YesToAll = request.yesToAll();
    job.Destination = request.destination();
    job.ShowDialog = request.showDialog();
    job.ElimDup = request.elimDup();
    job.WriteZone = request.writeZone();
    job.ArchivePath = request.archivePath();
    job.ArchiveType = request.archiveType();
    job.AddExtension = request.addExtension();
    job.EMail = request.email();
    job.HashMode = request.hashMode();
    job.HashMethod = request.hashMethod();
    job.HashBasePrefix = request.hashBasePrefix();
    job.HashEnterToDirs = request.hashEnterToDirs();
    return job;
}

OperationReport OperationReportFromResult(const QtOperationResult &result) {
    return OperationReport(int(result.Status), result.ExitCode, result.ChangedDirectories,
                           result.ChangedArchives, result.CancellationNeedsReview);
}

bool ValidateOperationOutputs(const QStringList &directories, const QStringList &archives) {
    for (const auto &paths : {directories, archives})
        for (const auto &path : paths)
            if (!PathIsRepresentable(path))
                return false;
    return true;
}

std::expected<QtOperationResult, QtOperationResult::Outcome>
OperationResultFromReport(const OperationReport &report) {
    using Outcome = QtOperationResult::Outcome;
    const auto status = Outcome(report.outcome());
    const int code = report.exitCode();
    bool valid = false;
    switch (status) {
    case Outcome::Success:
        valid = code == 0;
        break;
    case Outcome::Warning:
        valid = code == 1;
        break;
    case Outcome::Cancelled:
        valid = code == 255;
        break;
    case Outcome::Failure:
        valid = code > 1 && code < 255;
        break;
    default:
        break;
    }
    if (!valid || (report.cancellationNeedsReview() && status != Outcome::Cancelled) ||
        !ValidateOperationOutputs(report.changedDirectories(), report.changedArchives()))
        return std::unexpected(Outcome::ProtocolFailed);
    return QtOperationResult{status,
                             code,
                             {},
                             report.changedDirectories(),
                             report.changedArchives(),
                             report.cancellationNeedsReview()};
}
