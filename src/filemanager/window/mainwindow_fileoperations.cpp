#include "7zip/UI/FileManager/resource.h"
#include "7zip/UI/FileManager/resourceGui.h"
#include "7zip/UI/GUI/ExtractRes.h"
#include "Windows/FileName.h"
#include "archive/callbacks/IFileExtractCallback.h"
#include "dialogs/overwrite/OverwriteDialog.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/providers/FSFolder.h"
#include "filemanager/providers/qtlocationfolder.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtapppresentation.h"
#include "filemanager/window/qtlocalfiletask.h"
#include "filemanager/window/qtworkspaceapplication.h"
#include "localization/qtlangcontrols.h"
#include "platform/trash/qttrashservice.h"
#include "settings/ViewSettings.h"
#include "ui/controls/qtwindowstatusbar.h"
#include "ui/progress/ProgressDialog2.h"
#include <QFile>
#include <QScopedValueRollback>

void MainWindow::transferFiles(const QStringList &sources, const QString &destination, bool move,
                               bool directPath,
                               std::function<void(const QStringList &)> transferOutput) {
    if (app_.QtOperations().closed())
        return;
    QtAppOperationState::CallScope call(app_.QtOperations());
    const QString destinationDirectory =
        directPath ? QFileInfo(destination).absolutePath() : destination;
    const QFileInfo directory(destinationDirectory);
    if (projectedUi().state != State::Filesystem || !directory.isDir() || directory.isSymLink() ||
        directory.isJunction() || sources.isEmpty() || (directPath && sources.size() != 1)) {
        QMessageBox::warning(this, "Sevenzip",
                             QCoreApplication::translate("mainwindow_fileoperations",
                                                         "The destination folder is unavailable."));
        return;
    }
    auto *operation =
        new FileTransferOperation(sources, destination, move, nullptr, {},
                                  directPath ? FileTransferOperation::DestinationKind::DirectPath
                                             : FileTransferOperation::DestinationKind::Directory);
    auto *task =
        new QtLocalFileTask(*operation, QtLangText(move ? IDS_MOVING : IDS_COPYING), destination);
    const unsigned owner = projectedPanel_;
    statusStrip_->SetMessage(&app_.Panels[owner], {});
    fileTask_ = task;
    task->SetLocations(sources.join('\n'), destination);
    task->BindOwner(app_.Panels[owner], *this);
    const bool refreshesLocally = appPresentation_->IsCopyPending();
    connect(task, &QtLocalFileTask::transferOutput, this,
            [this, sources, destinationDirectory, move, refreshesLocally, owner] {
                QStringList changed{destinationDirectory};
                // A merge can move children while leaving their source folder.
                if (move)
                    for (const auto &source : sources)
                        changed.append(QFileInfo(source).absolutePath());
                if (auto *application = QtWorkspaceApplication::Find())
                    application->FilesystemPublished(changed, refreshesLocally ? this : nullptr);
                else if (!refreshesLocally)
                    guiOutputRefresh_->Enqueue(changed, {});
                // The copy continuation reloads the peer's current provider.
                // An archive provider also needs to check whether its file changed.
                if (refreshesLocally && app_.NumPanels == 2 && app_.Panels[1 - owner].IsArcFolder())
                    guiOutputRefresh_->Enqueue(changed, {}, int(owner));
            });
    if (transferOutput)
        connect(task, &QtLocalFileTask::transferOutput, task, std::move(transferOutput));
    connect(task, &QtLocalFileTask::finished, this,
            [this, task, operation, owner, move](bool success, bool canceled,
                                                 const QString &message, bool partial) {
                if (app_.QtOperations().closed())
                    return;
                const QPointer<MainWindow> guard(this);
                if (!guard || app_.QtOperations().closed())
                    return;
                if (fileTask_ == task)
                    fileTask_ = nullptr;
                // OnCopy's continuation owns both reloads, saved selections
                // and final focus after this worker releases its operation.
                // A context-menu transfer may outlive its temporary panel projection.
                // Release the operation before setState runs under a new projection.
                if (projectedPanel_ != owner)
                    app_.QtOperations().End();
                withPanelProjection(owner, [this] { setState(State::Filesystem); });
                const bool refreshSource =
                    !success && (!canceled || (move && operation->hasRetainedOutput()));
                appPresentation_->CopyFinished(!success || partial, refreshSource);
                if (success && guard)
                    statusStrip_->SetMessage(&app_.Panels[owner], message);
            });
    setState(State::Operating);
    task->Start();
}

void MainWindow::executeCopyDestination(bool move, bool copyToSame, unsigned sourceIndex,
                                        const QString &destination,
                                        const CRecordVector<UInt32> &indices, QtCopyRoute route) {
    const bool streams = app_.GetFocusedPanel().IsAltStreamsFolder();
    const bool devices =
        app_.GetFocusedPanel().IsFSDrivesFolder() && app_.GetFocusedPanel().IsDeviceDrivesPrefix();
    if (sourceIndex != app_.GetFocusedPanelIndex() || indices.IsEmpty() ||
        (projectedUi().state != State::Viewing && projectedUi().state != State::Filesystem &&
         !streams && !devices))
        return;
    QtAppOperationState::CallScope call(app_.QtOperations());
    if (route == QtCopyRoute::DestinationPanel &&
        transferToArchivePanel(destination, move, indices))
        return;
    bool alternateDestination = false;
#ifdef _WIN32
    alternateDestination = route != QtCopyRoute::StagedDestinationPanel &&
                           NWindows::NFile::NName::IsAltPathPrefix(us2fs(ToUString(destination)));
#endif
    if (streams || devices || alternateDestination) {
        CCopyToOptions options;
        options.folder = ToUString(destination);
        options.moveMode = move;
        options.includeAltStreams = true;
        options.replaceAltStreamChars = false;
        options.showErrorMessages = true;
        UStringVector messages;
        const HRESULT result = app_.Panels[sourceIndex].CopyTo(options, indices, &messages);
        // Original OnCopy reports the writer result before refreshing either
        // list. Retain both pending publications until ExecuteCopy releases
        // its operation guards; a blocked reload would only return S_OK.
        if (result != S_OK && result != E_ABORT)
            app_.Panels[sourceIndex].MessageBox_Error_HRESULT(result);
        if (result != S_OK)
            appPresentation_->CopyFinished(true);
        return;
    }
    const bool directPath = route == QtCopyRoute::DirectFileSystem ||
                            (indices.Size() == 1 && !destination.endsWith('/') &&
                             !QFileInfo(destination).isDir() && route == QtCopyRoute::FileSystem);
    if (projectedUi().state == State::Viewing) {
        if (route == QtCopyRoute::StagedDestinationPanel &&
            transferBetweenArchivePanels(destination, move, indices))
            return;
        transferFromArchive(destination, move, copyToSame, sourceIndex, indices);
        return;
    }
    QStringList sources;
    CPanel &source = app_.Panels[sourceIndex];
    source.LoadFullPath();
    UStringVector filePaths;
    source.GetFilePaths(indices, filePaths);
    FOR_VECTOR(i, filePaths)
    sources << QtFilesystemPath(us2fs(filePaths[i]));
    if (sources.isEmpty())
        return;
    transferFiles(sources, destination, move, directPath);
}

bool MainWindow::transferToArchivePanel(const QString &destination, bool move,
                                        const CRecordVector<UInt32> &indices) {
    if (app_.NumPanels != 2 || !app_.GetFocusedPanel().Is_IO_FS_Folder())
        return false;
    const unsigned sourceIndex = app_.GetFocusedPanelIndex();
    const unsigned destinationIndex = 1 - sourceIndex;
    CPanel &source = app_.Panels[sourceIndex];
    CPanel &target = app_.Panels[destinationIndex];
    if (panelUi_[destinationIndex].state != State::Viewing ||
        QDir::cleanPath(destination) != QDir::cleanPath(ToQString(target.GetFsPath())))
        return false;
    auto *folder = dynamic_cast<CAgentFolder *>(target.GetFolder());
    if (!folder || target.GetParentFolders().Size() != 1 || target.IsThereReadOnlyFolder()) {
        QMessageBox::warning(
            this, QtLangText(IDS_COPYING),
            QCoreApplication::translate("mainwindow_fileoperations",
                                        "Writing to this archive folder is unavailable."));
        appPresentation_->CopyFinished(true);
        return true;
    }
    UStringVector names;
    FOR_VECTOR(i, indices) names.Add(source.GetItemRelPath2(indices[i]));
    if (names.IsEmpty())
        return true;
    CSelectedState destinationSelection;
    target.SaveSelectedState(destinationSelection);
    const QString archive = ToQString(target.GetParentFolders().Front().VirtualPath);
    UStringVector messages;
    HRESULT result;
    bool published = false;
    {
        QScopedValueRollback<bool> progressOwner(sourceArchiveProgress_, true);
        result = target.CopyFrom(move, source.GetFsPath(), names, false, &messages, nullptr,
                                 std::nullopt, &published);
    }
    // The native call has completed. Release its retained source guards before
    // changing the panel projection; source RefreshListCtrl can then own a scan.
    if (app_.QtOperations().kind() == QtAppOperationState::Kind::Operation)
        app_.QtOperations().End();
    if (result == S_OK || published)
        refreshArchiveTransferDestination(destinationIndex, destinationSelection);
    if (published) {
        if (auto *application = QtWorkspaceApplication::Find()) {
            application->ArchivePublished(archive, &target);
            if (move)
                application->FilesystemPublished({ToQString(source.GetFsPath())}, this);
        } else
            ArchivePublished(archive, &target);
    }
    if (result == S_OK) {
        if (move)
            appPresentation_->RefreshCopyPanel(sourceIndex);
        showStatusMessage(move ? QCoreApplication::translate("mainwindow_fileoperations",
                                                             "Items moved into the archive.")
                               : QCoreApplication::translate("mainwindow_fileoperations",
                                                             "Items copied into the archive."));
    } else {
        appPresentation_->CopyFinished(true, move && published);
    }
    if (result != S_OK || !messages.IsEmpty()) {
        QStringList diagnostics;
        FOR_VECTOR(i, messages)
        diagnostics.append(ToQString(messages[i]));
        CProgressDialog::PresentResult(this, {.title = QtLangText(move ? IDS_MOVING : IDS_COPYING),
                                              .result = result,
                                              .diagnostics = diagnostics,
                                              .source = ToQString(source.GetFsPath()),
                                              .destination = destination,
                                              .partial = published && result != S_OK});
    }
    return true;
}
