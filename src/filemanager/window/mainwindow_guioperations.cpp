#include "archive/resources/qtarchivefileresource.h"
#include "dialogs/taskresult/qttaskresultdialog.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtwindowworkspace.h"
#include "filemanager/window/qtworkspaceapplication.h"
#include "fileops/links/qtlinkservices.h"
#include "operations/qtoperationlauncher.h"
#include "operations/qtoperationregistry.h"
#include "platform/temp/qtownedtempidentity.h"
#include <QDir>
#include <QFileInfo>
#include <algorithm>

void MainWindow::ArchivePublished(const QString &archive, const CPanel *updated) {
    if (!app_.QtOperations().closed() && guiOutputRefresh_) {
        int skipPanel = -1;
        for (unsigned panel = 0; updated && panel < kNumPanelsMax; ++panel)
            if (updated == &app_.Panels[panel])
                skipPanel = int(panel);
        workspace_->InvalidateArchive(archive, skipPanel);
        guiOutputRefresh_->Enqueue({}, {archive}, skipPanel);
    }
}

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
QString MissingInputMessage(const QString &path) {
    return QCoreApplication::translate("mainwindow_filelist", "Path not found: ") +
           QDir::toNativeSeparators(path);
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
    const QStringList sources = ArchivePaths(paths);
    if (sources.size() == 1 && !QFileInfo::exists(sources.front()) &&
        !QtIsFileSystemLink(sources.front())) {
        showStatusMessage(MissingInputMessage(sources.front()));
        return E_FAIL;
    }
    auto *operation = backend_->startGuiCompress(
        QFileInfo(QString::fromWCharArray(folder + name)).absoluteFilePath(),
        QString::fromWCharArray(type), addExtension, sources, email, showDialog, error);
    if (operation)
        clearStatusMessage();
    return observeGuiArchiveOperation(operation, error);
}

HRESULT MainWindow::launchGuiArchiveExtract(const UStringVector &paths, const UString &folder,
                                            bool showDialog, bool elimDup, UInt32 writeZone) {
    const QStringList archives = ArchivePaths(paths);
    for (const auto &path : archives)
        if (!QFileInfo::exists(path)) {
            showStatusMessage(MissingInputMessage(path));
            return E_FAIL;
        }
    QString error;
    auto *operation = backend_->startGuiExtract(archives, QString::fromWCharArray(folder),
                                                showDialog, elimDup, writeZone, error);
    if (operation)
        clearStatusMessage();
    return observeGuiArchiveOperation(operation, error);
}

HRESULT MainWindow::observeGuiArchiveOperation(QtOperationLauncher *operation,
                                               const QString &error) {
    const unsigned source = app_.GetFocusedPanelIndex();
    if (!operation) {
        app_.Panels[source].MessageBox_Error(ToUString(error));
        return E_FAIL;
    }
    const QUuid review = QtOperationRegistry::Application().Registration(*operation);
    auto *application = QtWorkspaceApplication::Find();
    if (application)
        connect(operation, &QtOperationLauncher::finished, application,
                [application](const QtOperationResult &result) {
                    for (const auto &archive : result.ChangedArchives)
                        application->ArchivePublished(archive);
                    application->FilesystemPublished(result.ChangedDirectories);
                });
    connect(operation, &QtOperationLauncher::finished, this,
            [this, source, review, publishLocally = !application](const QtOperationResult &result) {
                if (app_.QtOperations().closed())
                    return;
                if (publishLocally) {
                    for (const auto &archive : result.ChangedArchives)
                        ArchivePublished(archive);
                    guiOutputRefresh_->Enqueue(result.ChangedDirectories, {});
                }
                using Outcome = QtOperationResult::Outcome;
                if (!QtWorkspaceApplication::Find())
                    for (const auto &pending : QtOperationRegistry::Application().Reviews())
                        if (pending.Id == review && result.Status == Outcome::Cancelled) {
                            QtTaskResultDialog dialog(pending);
                            dialog.setAttribute(Qt::WA_DeleteOnClose, false);
                            dialog.exec();
                            QtOperationRegistry::Application().Acknowledge(review);
                            return;
                        }
                if ((!QtWorkspaceApplication::Find() || review.isNull()) &&
                    (result.Status == Outcome::LaunchFailed ||
                     result.Status == Outcome::ProtocolFailed ||
                     result.Status == Outcome::Crashed)) {
                    app_.Panels[source].MessageBox_Error(ToUString(result.Error));
                    QtOperationRegistry::Application().Acknowledge(review);
                }
                // Source dialogs in 7zG own ordinary errors. Cancelled work
                // gets a final application-owned review after native exit.
            });
    return S_OK;
}

void MainWindow::refreshGuiOutputPanel(unsigned index, const QtGuiOutputRefresh::Outputs &output) {
    if (workspace_->RefreshPublishedArchive(index))
        return;
    auto &panel = app_.Panels[index];
    if (!panel.PanelCreated)
        return;
    if (panel.IsArcFolder()) {
        const auto &parents = panel.GetParentFolders();
        if (parents.Size() > 1 && !output.directories.isEmpty()) {
            const QString outer = ToQString(parents.Front().VirtualPath);
            if (!QFileInfo::exists(outer) &&
                std::any_of(output.directories.cbegin(), output.directories.cend(),
                            [&](const QString &directory) {
                                return ContainsDirectory(NormalPath(directory), NormalPath(outer));
                            }) &&
                workspace_->RefreshPublishedArchive(index, false, true))
                return;
        }
        auto *folder = dynamic_cast<CAgentFolder *>(panel.GetFolder());
        auto *resource =
            folder ? dynamic_cast<QtArchiveFileResource *>(folder->GetEngineResource()) : nullptr;
        // Stream/device generations retain their direct owner. ReOpen of an
        // ordinary file reuses the original source folder/subfolder workflow.
        if (!resource || resource->HasStreamInput())
            return;
        const bool changed = std::any_of(
            output.archives.cbegin(), output.archives.cend(),
            [&](const QString &archive) { return SameFile(resource->EnginePath, archive); });
        const bool removed =
            !changed && !QFileInfo::exists(resource->EnginePath) &&
            std::any_of(output.directories.cbegin(), output.directories.cend(),
                        [&](const QString &directory) {
                            return ContainsDirectory(NormalPath(directory),
                                                     NormalPath(resource->EnginePath));
                        });
        if (!changed && !removed)
            return;
        const QString parent = QFileInfo(resource->EnginePath).absolutePath();
        const QPointer<QWidget> focus = focusWidget();
        withPanelProjection(index, [&] {
            if (changed)
                refreshCurrent();
            else
                navigateToImpl(parent, true);
        });
        // ReOpen retains modal source guards while restoring the original
        // projection. Publish its resulting idle/scan state before focus.
        if (changed)
            setState(projectedUi().state);
        if (focus && focus->isEnabled() && focus->isVisible())
            focus->setFocus(Qt::OtherFocusReason);
        return;
    }
    if (!panel.Is_IO_FS_Folder())
        return;
    const QString current = NormalPath(QString::fromWCharArray(panel.GetFsPath()));
    for (const auto &directory : output.directories) {
        const QString changed = NormalPath(directory);
        if (ContainsDirectory(current, changed) || ContainsDirectory(changed, current)) {
            panel.OnReload(true);
            return;
        }
    }
}
