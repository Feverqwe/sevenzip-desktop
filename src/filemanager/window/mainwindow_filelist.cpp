#include "filemanager/qtfilemanagercommands.h"
#include "filemanager/rename/qtrenamecontroller.h"
#include "filemanager/window/mainwindow.h"
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
        path_->setText(state_ == State::Viewing ? archiveDisplayPath() : directory_);
        return E_FAIL;
    }
    const QString next = info.canonicalFilePath();
    if (remember && state_ == State::Namespace)
        history_.append(ToQString(GetFolderPath(app_.GetFocusedPanel().GetFolder())));
    else if (remember && !directory_.isEmpty() && directory_ != next)
        history_.append(directory_);
    cancelListPublication();
    if (!sourceAlreadyBound)
        app_.GetFocusedPanel().CloseOpenFolders();
    directory_ = next;
    autoRefreshSnapshot_.clear();
    autoRefreshSnapshot_.insert(directory_, filesystemSignature(directory_));
    recordVisitedPath(directory_);
    archive_.clear();
    displayArchive_.clear();
    archivePassword_.clear();
    archiveFolder_.clear();
    renameController_->Cancel(projectedPanel_);
    table_->clearSelection();
    table_->setSortingEnabled(false);
    table_->clearRows();
    if (updateAddress)
        path_->setText(directory_);
    static_cast<FileTable *>(table_)->filesystem = true;
    return startFilesystemListing(sourceAlreadyBound, updateAddress);
}

void MainWindow::openPath(const QString &path) { navigateTo(QFileInfo(path).absoluteFilePath()); }

HRESULT MainWindow::navigateTo(const QString &path, bool sourceAddress) {
    if (state_ == State::Loading || state_ == State::Operating)
        return E_ABORT;
    if (state_ == State::Viewing && path == archiveDisplayPath())
        return S_OK;
    // A typed relative address belongs to the displayed filesystem folder.
    // CPanel::BindToPath still resolves the resulting native path.
    const QString nativeAddress =
        !sourceAddress && state_ == State::Filesystem && QDir::isRelativePath(path)
            ? QDir(directory_).filePath(path)
            : path;
    const QFileInfo info(nativeAddress);
    if (!sourceAddress && info.isDir())
        return showDirectory(nativeAddress, true);
    else if (!sourceAddress && info.isFile() && !info.isSymLink())
        return openArchiveAs(nativeAddress, {});
    else {
        QString nativePath = nativeAddress;
        if (!sourceAddress)
            nativePath.replace(QStringLiteral(" / "), QStringLiteral("/"));
        auto &panel = app_.GetFocusedPanel();
        HRESULT result;
        {
            QScopedValueRollback<bool> source(sourceArchiveProgress_, true);
            COpenResult opened;
            result = panel.BindToPath(ToUString(nativePath), UString(), opened);
        }
        renderBoundFolder();
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
    if (row < 0 || row >= table_->rowCount())
        return;
    if (!table_->selectionModel()->isRowSelected(row, QModelIndex()))
        table_->selectRow(row);
    table_->setCurrentCell(row, 0, QItemSelectionModel::NoUpdate);
    activateItems(modifiers);
}
