#include "archive/resources/qtarchivefileresource.h"
#include "filemanager/window/mainwindow.h"
#include "operations/qtoperationlauncher.h"
#include "platform/temp/qtownedtempidentity.h"
#include <QDir>
#include <QFileInfo>

namespace {
QStringList ArchivePaths(const UStringVector &paths) {
    QStringList result;
    FOR_VECTOR(i, paths) { result.append(QString::fromWCharArray(paths[i])); }
    return result;
}
QString NormalPath(const QString &path) {
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}
bool ContainsDirectory(const QString &parent, const QString &child) {
    const QString relative = QDir(parent).relativeFilePath(child);
    return QDir::isRelativePath(relative) && relative != ".." && !relative.startsWith("../");
}
bool SameFile(const QString &left, const QString &right) {
    const QString a = NormalPath(left), b = NormalPath(right);
    if (a == b)
        return true;
    const QString identity = QtOwnedTempIdentity(a);
    return !identity.isEmpty() && identity == QtOwnedTempIdentity(b);
}
} // namespace

bool MainWindow::canLaunchGuiFilesystemOperation() {
    const auto &panel = app_.GetFocusedPanel();
    return panel.Is_IO_FS_Folder() && !panel.IsDeviceDrivesPrefix() &&
           panel.GetParentFolders().IsEmpty();
}

HRESULT MainWindow::launchGuiArchiveTest(const UStringVector &paths, bool hashMode) {
    QString error;
    auto *operation = backend_->startGuiTest(ArchivePaths(paths), error, hashMode);
    return observeGuiArchiveOperation(operation, error);
}

HRESULT MainWindow::launchGuiHash(const UStringVector &paths, const UString &method,
                                  const UString &basePrefix, bool enterToDirs) {
    QString error;
    auto *operation =
        backend_->startGuiHash(ArchivePaths(paths), QString::fromWCharArray(method),
                               QString::fromWCharArray(basePrefix), enterToDirs, error);
    return observeGuiArchiveOperation(operation, error);
}

HRESULT MainWindow::launchGuiArchiveCompress(const UString &folder, const UString &name,
                                             const UString &type, bool addExtension,
                                             const UStringVector &paths, bool email,
                                             bool showDialog) {
    QString error;
    auto *operation = backend_->startGuiCompress(
        QFileInfo(QString::fromWCharArray(folder + name)).absoluteFilePath(),
        QString::fromWCharArray(type), addExtension, ArchivePaths(paths), email, showDialog, error);
    return observeGuiArchiveOperation(operation, error);
}

HRESULT MainWindow::launchGuiArchiveExtract(const UStringVector &paths, const UString &folder,
                                            bool showDialog, bool elimDup, UInt32 writeZone) {
    QString error;
    auto *operation =
        backend_->startGuiExtract(ArchivePaths(paths), QString::fromWCharArray(folder), showDialog,
                                  elimDup, writeZone, error);
    return observeGuiArchiveOperation(operation, error);
}

HRESULT MainWindow::observeGuiArchiveOperation(QtOperationLauncher *operation,
                                               const QString &error) {
    const unsigned source = app_.GetFocusedPanelIndex();
    if (!operation) {
        app_.Panels[source].MessageBox_Error(ToUString(error));
        return E_FAIL;
    }
    connect(operation, &QtOperationLauncher::finished, this,
            [this, source](const QtOperationResult &result) {
                if (app_.QtOperations().closed())
                    return;
                guiOutputRefresh_->Enqueue(result.ChangedDirectories, result.ChangedArchives);
                using Outcome = QtOperationResult::Outcome;
                if (result.Status == Outcome::LaunchFailed ||
                    result.Status == Outcome::ProtocolFailed || result.Status == Outcome::Crashed)
                    app_.Panels[source].MessageBox_Error(ToUString(result.Error));
                // Source dialogs in 7zG own ordinary errors and cancellation.
            });
    return S_OK;
}

void MainWindow::refreshGuiOutputPanel(unsigned index, const QtGuiOutputRefresh::Outputs &output) {
    auto &panel = app_.Panels[index];
    if (!panel.PanelCreated)
        return;
    if (panel.IsArcFolder()) {
        auto *folder = dynamic_cast<CAgentFolder *>(panel.GetFolder());
        auto *resource =
            folder ? dynamic_cast<QtArchiveFileResource *>(folder->GetEngineResource()) : nullptr;
        // Stream/device generations retain their direct owner. ReOpen of an
        // ordinary file reuses the original source folder/subfolder workflow.
        if (!resource || resource->HasStreamInput())
            return;
        for (const auto &archive : output.archives)
            if (SameFile(resource->EnginePath, archive)) {
                const QPointer<QWidget> focus = focusWidget();
                withPanelProjection(index, [this] { refreshCurrent(); });
                // Projection retains modal source guards while restoring the
                // original panel. Publish the resulting idle/scan state after
                // that restoration, before restoring the remembered focus.
                setState(projectedUi().state);
                if (focus && focus->isEnabled() && focus->isVisible())
                    focus->setFocus(Qt::OtherFocusReason);
                return;
            }
        if (!QFileInfo::exists(resource->EnginePath))
            for (const auto &directory : output.directories)
                if (ContainsDirectory(NormalPath(directory), NormalPath(resource->EnginePath))) {
                    const QString parent = QFileInfo(resource->EnginePath).absolutePath();
                    const QPointer<QWidget> focus = focusWidget();
                    withPanelProjection(index, [this, parent] { showDirectory(parent, false); });
                    if (focus && focus->isEnabled() && focus->isVisible())
                        focus->setFocus(Qt::OtherFocusReason);
                    return;
                }
        return;
    }
    if (!panel.Is_IO_FS_Folder())
        return;
    const QString current = NormalPath(QString::fromWCharArray(panel.GetFsPath()));
    for (const auto &directory : output.directories) {
        const QString changed = NormalPath(directory);
        if (ContainsDirectory(current, changed) || ContainsDirectory(changed, current)) {
            panel.OnReload();
            return;
        }
    }
}
