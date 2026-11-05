#include "archive/opening/archivecontainerpath.h"
#include "archive/resources/archiveinputpath.h"
#include "filemanager/qtfilemanagercommands.h"
#include "filemanager/rename/qtrenamecontroller.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtwindowworkspace.h"
#include "settings/ViewSettings.h"
#include <QScopedValueRollback>

HRESULT MainWindow::showDirectory(const QString &path, bool remember, bool sourceAlreadyBound,
                                  bool updateAddress) {
    if (app_.QtOperations().closed())
        return E_ABORT;
    QtAppOperationState::CallScope call(app_.QtOperations());
    const QFileInfo info(path);
    if (!info.isDir()) {
        statusBar()->showMessage(
            QCoreApplication::translate("mainwindow_filelist", "Folder not found: ") + path);
        projectedUi().path->setText(
            projectedUi().state == State::Viewing ? archiveDisplayPath() : projectedUi().directory);
        return E_FAIL;
    }
    const QString next = info.canonicalFilePath();
    Q_UNUSED(remember);
    cancelListPublication();
    if (!sourceAlreadyBound)
        app_.GetFocusedPanel().CloseOpenFolders();
    projectedUi().directory = next;
    auto &snapshot = panelUi_[projectedPanel_].autoRefreshSnapshot;
    snapshot.clear();
    snapshot.insert(projectedUi().directory, filesystemSignature(projectedUi().directory));
    projectedUi().archive.clear();
    projectedUi().displayArchive.clear();
    projectedUi().archivePassword.clear();
    projectedUi().archiveFolder.clear();
    renameController_->Cancel(projectedPanel_);
    projectedUi().table->clearSelection();
    projectedUi().table->setSortingEnabled(false);
    projectedUi().table->clearRows();
    if (updateAddress)
        projectedUi().path->setText(projectedUi().directory);
    static_cast<FileTable *>(projectedUi().table)->filesystem = true;
    return startFilesystemListing(sourceAlreadyBound, updateAddress);
}

void MainWindow::openPath(const QString &path) { navigateTo(QFileInfo(path).absoluteFilePath()); }

HRESULT MainWindow::navigateTo(const QString &path, bool sourceAddress) {
    return workspace_->Navigate([&] { return navigateToImpl(path, sourceAddress); });
}

HRESULT MainWindow::navigateToImpl(const QString &path, bool sourceAddress) {
    if (projectedUi().state == State::Loading || projectedUi().state == State::Operating)
        return E_ABORT;
    projectedUi().locationUnavailable = false;
    if (projectedUi().state == State::Viewing && path == archiveDisplayPath())
        return S_OK;
    // A typed relative address belongs to the displayed filesystem folder.
    // CPanel::BindToPath still resolves the resulting native path.
    const QString nativeAddress =
        !sourceAddress && projectedUi().state == State::Filesystem && QDir::isRelativePath(path)
            ? QDir(projectedUi().directory).filePath(path)
            : path;
    const QFileInfo info(nativeAddress);
    if (!sourceAddress && info.isDir() && !IsSparseBundleName(ToUString(nativeAddress))) {
        workspace_->ProtectNavigation(nativeAddress);
        return showDirectory(nativeAddress, true);
    } else if (!sourceAddress && IsArchiveFileOrPackage(info))
        return openArchiveAs(nativeAddress, {});
    else {
        QString nativePath = nativeAddress;
        if (!sourceAddress)
            nativePath = sourcePathFromDisplayPath(nativePath);
        auto &panel = app_.GetFocusedPanel();
        workspace_->ProtectNavigation(nativePath);
        HRESULT result;
        {
            QScopedValueRollback<bool> source(sourceArchiveProgress_, true);
            COpenResult opened;
            result = panel.BindToPath(ToUString(nativePath), UString(), opened);
        }
        const HRESULT rendered = renderBoundFolder();
        if (result == S_OK)
            result = rendered;
        if (result != S_OK && result != E_ABORT)
            statusBar()->showMessage(
                QCoreApplication::translate("mainwindow_filelist", "Path not found: ") + path);
        return result;
    }
}

void MainWindow::activateItems(Qt::KeyboardModifiers modifiers) {
    if (!renameController_->Pending())
        commands_->ActivateItems(modifiers);
}

void MainWindow::activateRow(int row, Qt::KeyboardModifiers modifiers) {
    if (renameController_->Pending())
        return;
    if (row < 0 || row >= projectedUi().table->rowCount())
        return;
    if (!projectedUi().table->selectionModel()->isRowSelected(row, QModelIndex()))
        projectedUi().table->selectRow(row);
    projectedUi().table->setCurrentCell(row, 0, QItemSelectionModel::NoUpdate);
    activateItems(modifiers);
}
