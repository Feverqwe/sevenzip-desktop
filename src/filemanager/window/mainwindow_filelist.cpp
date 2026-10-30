#include "7zip/PropID.h"
#include "7zip/UI/FileManager/resource.h"
#include "7zip/UI/FileManager/resourceGui.h"
#include "Windows/PropVariant.h"
#include "archive/resources/qtarchivereopen.h"
#include "dialogs/browse/BrowseDialog.h"
#include "filemanager/SysIconUtils.h"
#include "filemanager/panels/PanelSort.h"
#include "filemanager/panels/qtpaneladdresscombo.h"
#include "filemanager/providers/FSFolder.h"
#include "filemanager/providers/qtlocationfolder.h"
#include "filemanager/qtfilemanagercommands.h"
#include "filemanager/window/mainwindow.h"
#include "localization/qtlangcontrols.h"
#include "localization/qtlanguageservices.h"
#include <QScopeGuard>
#include <QScopedValueRollback>
#include <QScrollBar>

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
    renameEdit_.reset();
    table_->clearSelection();
    table_->setSortingEnabled(false);
    table_->clearRows();
    if (updateAddress)
        path_->setText(directory_);
    static_cast<FileTable *>(table_)->filesystem = true;
    return startFilesystemListing(sourceAlreadyBound, updateAddress);
}

HRESULT MainWindow::startFilesystemListing(bool sourceAlreadyBound, bool updateAddress) {
    COpenResult opened;
    auto &panel = app_.GetFocusedPanel();
    HRESULT result =
        sourceAlreadyBound ? S_OK : panel.BindToPath(ToUString(directory_), UString(), opened);
    CMyComPtr<IFolderFolder> folder = panel.GetFolder();
    if (result == S_OK && !panel.IsFSFolder()) {
        return renderBoundFolder(updateAddress);
    }
    if (result == S_OK && updateAddress)
        static_cast<QtPanelAddressCombo *>(pathCombo_)->ShowCurrentPath(directory_, true);
    if (panel.GetFlatMode()) {
        scanningDisk_ = true;
        setState(State::Filesystem);
        statusBar()->showMessage(QtLangText(IDS_SCANNING));
        if (!filesystemScanner_->start(directory_, true, folder)) {
            failFilesystemListing(E_ABORT);
            return E_ABORT;
        }
    } else {
        if (result == S_OK) {
            auto *spec = static_cast<NFsFolder::CFSFolder *>((IFolderFolder *)folder);
            bool defer = false;
            {
                auto previous = std::move(spec->SetCompleted);
                const auto restore = qScopeGuard([&] { spec->SetCompleted = std::move(previous); });
                // Preserve immediate small-folder callers. Stop the original
                // load at its callback boundary, then rerun it in the existing
                // worker for a larger list instead of walking all files here.
                spec->SetCompleted = [&](quint64 count) {
                    defer = count > 256;
                    return !defer;
                };
                result = panel.LoadListItems();
            }
            if (result == E_ABORT && defer) {
                scanningDisk_ = true;
                setState(State::Filesystem);
                statusBar()->showMessage(QtLangText(IDS_SCANNING));
                if (!filesystemScanner_->start(directory_, false, folder)) {
                    failFilesystemListing(E_ABORT);
                    return E_ABORT;
                }
                return S_OK;
            }
        }
        if (result == S_OK) {
            configureColumns(true);
            appendParentEntry();
            auto *spec = static_cast<NFsFolder::CFSFolder *>((IFolderFolder *)folder);
            for (UInt32 i = 0; i < spec->Files.Size(); ++i) {
                result = appendFilesystemEntry(
                    QFileInfo(QDir(directory_)
                                  .filePath(QtFilesystemPath(spec->GetRelPath(spec->Files[i])))),
                    i);
                if (result != S_OK) {
                    table_->clearRows();
                    break;
                }
            }
        }
        applySortMode(true);
        setState(State::Filesystem);
        if (result == S_OK)
            statusBar()->clearMessage();
        else
            statusBar()->showMessage(
                QCoreApplication::translate("mainwindow_filelist", "Could not read folder."));
    }
    return result;
}

HRESULT MainWindow::appendFilesystemEntry(const QFileInfo &file, quint32 sourceIndex) {
    QIcon icon;
    RINOK(app_.GetFocusedPanel().GetItemIcon(sourceIndex, icon))
#ifdef Q_OS_WIN
    // Normal Windows folders already use source FindChangeNotification. Keep
    // the portable fallback for flat descendants, outside its nonrecursive watch.
    if (app_.GetFocusedPanel().GetFlatMode())
#endif
        autoRefreshSnapshot_.insert(file.absoluteFilePath(),
                                    filesystemSignature(file.absoluteFilePath()));
    IFolderFolder *folder = app_.GetFocusedPanel().GetFolder();
    NWindows::NCOM::CPropVariant name, prefix;
    folder->GetProperty(sourceIndex, kpidName, &name);
    folder->GetProperty(sourceIndex, kpidPrefix, &prefix);
    const bool directory = app_.GetFocusedPanel().IsItem_Folder(sourceIndex);
    const QString nativeName =
        name.vt == VT_BSTR ? QString::fromWCharArray(name.bstrVal) : file.fileName();
    const QString nativePrefix =
        prefix.vt == VT_BSTR ? QDir::fromNativeSeparators(QString::fromWCharArray(prefix.bstrVal))
                             : QString();
    FilePanelModel::Row row;
    row.sourceIndex = sourceIndex;
    row.name = nativeName;
    row.prefix = app_.GetFocusedPanel().GetFlatMode() ? nativePrefix : QString();
    row.path = file.absoluteFilePath();
    row.toolTip = row.path;
    row.directory = directory;
    row.icon = icon;
    table_->panelModel()->append(std::move(row));
    return S_OK;
}

QStringList MainWindow::selectedPaths() const {
    QStringList paths;
    for (const QModelIndex &index : table_->selectionModel()->selectedRows())
        paths << table_->model()->index(index.row(), 1).data().toString();
    return paths;
}

void MainWindow::selectPaths(const QStringList &paths) {
    if (scanningDisk_ || scanningArchive_) {
        scanSelectedPaths_ = paths;
        return;
    }
    table_->clearSelection();
    const auto &panel = app_.GetFocusedPanel();
    const bool streams = panel.GetFolder() && panel.IsAltStreamsFolder();
    for (int row = 0; row < table_->rowCount(); ++row) {
        const QString path = streams ? table_->model()->index(row, 0).data(Qt::UserRole).toString()
                                     : table_->model()->index(row, 1).data().toString();
        if (paths.contains(path))
            table_->selectionModel()->select(table_->model()->index(row, 0),
                                             QItemSelectionModel::Select |
                                                 QItemSelectionModel::Rows);
    }
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
    if (!renameActivationPending_)
        commands_->ActivateItems(modifiers);
}

void MainWindow::activateRow(int row, Qt::KeyboardModifiers modifiers) {
    if (renameActivationPending_)
        return;
    if (row < 0 || row >= table_->rowCount())
        return;
    if (!table_->selectionModel()->isRowSelected(row, QModelIndex()))
        table_->selectRow(row);
    table_->setCurrentCell(row, 0, QItemSelectionModel::NoUpdate);
    activateItems(modifiers);
}
