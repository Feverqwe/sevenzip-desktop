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
#include "localization/qtlangcontrols.h"
#include "platform/trash/qttrashservice.h"
#include "settings/ViewSettings.h"
#include "ui/progress/ProgressDialog2.h"
#include <QFile>
#include <QScopedValueRollback>

void MainWindow::transferFiles(const QStringList &sources, const QString &destination, bool move,
                               bool directPath, std::function<void(bool)> completed) {
    if (app_.QtOperations().closed())
        return;
    QtAppOperationState::CallScope call(app_.QtOperations());
    const QString destinationDirectory =
        directPath ? QFileInfo(destination).absolutePath() : destination;
    if (projectedUi().state != State::Filesystem || !QFileInfo(destinationDirectory).isDir() ||
        QFileInfo(destinationDirectory).isSymLink() || sources.isEmpty() ||
        (directPath && sources.size() != 1)) {
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
    fileTask_ = task;
    task->BindOwner(app_.Panels[projectedPanel_], *this);
    if (completed)
        connect(task, &QtLocalFileTask::transferCompleted, task, std::move(completed));
    connect(task, &QtLocalFileTask::finished, this,
            [this, task](bool success, bool, const QString &message) {
                if (app_.QtOperations().closed())
                    return;
                const QPointer<MainWindow> guard(this);
                if (!guard || app_.QtOperations().closed())
                    return;
                if (fileTask_ == task)
                    fileTask_ = nullptr;
                if (success)
                    statusBar()->showMessage(message);
                // OnCopy's continuation owns both reloads, saved selections
                // and final focus after this worker releases its operation.
                setState(State::Filesystem);
                appPresentation_->CopyFinished();
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
        return;
    }
    const bool directPath = indices.Size() == 1 && !destination.endsWith('/') &&
                            !QFileInfo(destination).isDir() && route == QtCopyRoute::FileSystem;
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
        return true;
    }
    UStringVector names;
    FOR_VECTOR(i, indices) names.Add(source.GetItemRelPath2(indices[i]));
    if (names.IsEmpty())
        return true;
    CSelectedState destinationSelection;
    target.SaveSelectedState(destinationSelection);
    UStringVector messages;
    HRESULT result;
    {
        QScopedValueRollback<bool> progressOwner(sourceArchiveProgress_, true);
        result = target.CopyFrom(move, source.GetFsPath(), names, true, &messages);
    }
    // The native call has completed. Release its retained source guards before
    // changing the panel projection; source RefreshListCtrl can then own a scan.
    if (app_.QtOperations().kind() == QtAppOperationState::Kind::Operation)
        app_.QtOperations().End();
    if (result == S_OK) {
        refreshArchiveTransferDestination(destinationIndex, destinationSelection);
        if (move)
            appPresentation_->RefreshCopyPanel(sourceIndex);
        statusBar()->showMessage(
            move ? QCoreApplication::translate("mainwindow_fileoperations",
                                               "Items moved into the archive.")
                 : QCoreApplication::translate("mainwindow_fileoperations",
                                               "Items copied into the archive."));
    } else if (result != E_ABORT) {
        QMessageBox::warning(this, QtLangText(IDS_COPYING),
                             QString::fromWCharArray(HResultToMessage(result).Ptr()));
    }
    return true;
}
