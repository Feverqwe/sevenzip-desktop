#include "support/operation_parent.h"
#include "operations/qtoperationlauncher.h"
#include <QCoreApplication>
#include <QTimer>

int RunOperationParent(QCoreApplication &application, const QString &host) {
    QtOperationLauncher launcher;
    QtOperationJob job;
    job.Paths = {qEnvironmentVariable("SEVENZIP_OPERATION_ARCHIVE")};
    job.YesToAll = true;
    if (qEnvironmentVariableIsSet("SEVENZIP_OPERATION_PROBE_HASH")) {
        job.Operation = QtOperationJob::Command::Hash;
        job.HashMethod = "SHA256";
    }
    if (qEnvironmentVariableIsSet("SEVENZIP_OPERATION_PROBE_EXTRACT")) {
        job.Operation = QtOperationJob::Command::Extract;
        job.Destination = qEnvironmentVariable("SEVENZIP_OPERATION_DESTINATION");
        job.ShowDialog = false;
    }
    if (qEnvironmentVariableIsSet("SEVENZIP_OPERATION_PROBE_COMPRESS")) {
        job.Operation = QtOperationJob::Command::Compress;
        job.ArchivePath = qEnvironmentVariable("SEVENZIP_OPERATION_CREATE_TARGET");
        job.ArchiveType = "zip";
        job.AddExtension = false;
        job.ShowDialog = false;
    }
    QObject::connect(&launcher, &QtOperationLauncher::accepted, &application,
                     [&] { application.exit(0); });
    QObject::connect(&launcher, &QtOperationLauncher::finished, &application,
                     [&] { application.exit(103); });
    QTimer::singleShot(12000, &application, [&] { application.exit(104); });
    QString error;
    if (!launcher.start(host, job, error))
        return 105;
    return application.exec();
}
