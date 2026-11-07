#include "archive/backend/archivebackend.h"
#include "operations/qtoperationlauncher.h"

QtOperationLauncher *ArchiveBackend::startGuiTest(const QStringList &archives, QString &error,
                                                  bool hashMode) {
    QtOperationJob job;
    job.Paths = archives;
    job.HashMode = hashMode;
    return QtOperationLauncher::Launch(QtOperationLauncher::GuiProgram(), job, error);
}

QtOperationLauncher *ArchiveBackend::startGuiHash(const QStringList &paths, const QString &method,
                                                  const QString &basePrefix, bool enterToDirs,
                                                  QString &error) {
    QtOperationJob job;
    job.Operation = QtOperationJob::Command::Hash;
    job.Paths = paths;
    job.HashMethod = method;
    job.HashBasePrefix = basePrefix;
    job.HashEnterToDirs = enterToDirs;
    return QtOperationLauncher::Launch(QtOperationLauncher::GuiProgram(), job, error);
}

QtOperationLauncher *ArchiveBackend::startGuiCompress(const QString &archive, const QString &type,
                                                      bool addExtension, const QStringList &paths,
                                                      bool email, bool showDialog, QString &error) {
    QtOperationJob job;
    job.Operation = QtOperationJob::Command::Compress;
    job.Paths = paths;
    job.ArchivePath = archive;
    job.ArchiveType = type;
    job.AddExtension = addExtension;
    job.EMail = email;
    job.ShowDialog = showDialog;
    return QtOperationLauncher::Launch(QtOperationLauncher::GuiProgram(), job, error);
}

QtOperationLauncher *ArchiveBackend::startGuiExtract(const QStringList &archives,
                                                     const QString &destination, bool showDialog,
                                                     bool elimDup, quint32 writeZone,
                                                     QString &error) {
    QtOperationJob job;
    job.Operation = QtOperationJob::Command::Extract;
    job.Paths = archives;
    job.Destination = destination;
    job.ShowDialog = showDialog;
    job.ElimDup = elimDup;
    job.WriteZone = writeZone;
    return QtOperationLauncher::Launch(QtOperationLauncher::GuiProgram(), job, error);
}
