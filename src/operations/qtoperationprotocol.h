// Application values and validation for the generated Remote Objects contract.
#pragma once
#include <QMetaType>
#include <QStringList>
#include <expected>

class OperationRequest;
class OperationReport;

enum class QtOperationPhase { Preparing, Draft, Working, Question, Reviewing };
struct QtOperationPresentation {
    QtOperationPhase Phase = QtOperationPhase::Preparing;
    QString Title;
    bool HasWindow = false;
};
Q_DECLARE_METATYPE(QtOperationPhase)

struct QtOperationResult {
    enum class Outcome {
        Success,
        Warning,
        Cancelled,
        Failure,
        LaunchFailed,
        ProtocolFailed,
        Crashed
    };
    Outcome Status = Outcome::Failure;
    int ExitCode = 2;
    QString Error;
    QStringList ChangedDirectories;
    QStringList ChangedArchives;
};
Q_DECLARE_METATYPE(QtOperationResult)

struct QtOperationJob {
    enum class Command { Test, Extract, Compress, Hash, Benchmark };
    Command Operation = Command::Test;
    QStringList Paths;
    bool YesToAll = false;
    QString Destination;
    bool ShowDialog = true;
    bool ElimDup = false;
    quint32 WriteZone = quint32(-1);
    QString ArchivePath;
    QString ArchiveType;
    bool AddExtension = true;
    bool EMail = false;
    bool HashMode = false;
    QString HashMethod;
    QString HashBasePrefix;
    bool HashEnterToDirs = true;
    bool TotalMode = false;
};

std::expected<void, QString> ValidateOperationJob(const QtOperationJob &job);
OperationRequest OperationRequestFromJob(const QtOperationJob &job);
QtOperationJob OperationJobFromRequest(const OperationRequest &request);
OperationReport OperationReportFromResult(const QtOperationResult &result);
std::expected<QtOperationResult, QtOperationResult::Outcome>
OperationResultFromReport(const OperationReport &report);
bool ValidateOperationOutputs(const QStringList &directories, const QStringList &archives);
