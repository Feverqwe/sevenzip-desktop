#include "7zip/PropID.h"
#include "7zip/UI/FileManager/resource.h"
#include "Windows/PropVariant.h"
#include "filemanager/panels/qtpaneladdresscombo.h"
#include "filemanager/providers/FSFolder.h"
#include "filemanager/providers/qtlocationfolder.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtapppresentation.h"
#include "localization/qtlangcontrols.h"
#include <QScopeGuard>

HRESULT MainWindow::startFilesystemListing(bool sourceAlreadyBound, bool updateAddress) {
    COpenResult opened;
    auto &panel = app_.GetFocusedPanel();
    HRESULT result = sourceAlreadyBound
                         ? S_OK
                         : panel.BindToPath(ToUString(projectedUi().directory), UString(), opened);
    if (result != S_OK)
        return finishSynchronousFilesystemListing(result);
    CMyComPtr<IFolderFolder> folder = panel.GetFolder();
    if (!panel.IsFSFolder())
        return renderBoundFolder(updateAddress);
    if (updateAddress)
        static_cast<QtPanelAddressCombo *>(projectedUi().pathCombo)
            ->ShowCurrentPath(projectedUi().directory);
    if (panel.GetFlatMode())
        return startFilesystemScan(folder, true);

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
    if (result == E_ABORT && defer)
        return startFilesystemScan(folder, false);
    if (result == S_OK) {
        configureColumns();
        appendParentEntry();
        for (UInt32 i = 0; i < spec->Files.Size(); ++i) {
            result = appendFilesystemEntry(
                QFileInfo(QDir(projectedUi().directory)
                              .filePath(QtFilesystemPath(spec->GetRelPath(spec->Files[i])))),
                i);
            if (result != S_OK) {
                projectedUi().table->clearRows();
                break;
            }
        }
    }
    return finishSynchronousFilesystemListing(result);
}

HRESULT MainWindow::finishSynchronousFilesystemListing(HRESULT result) {
    applySortMode(true);
    setState(State::Filesystem);
    if (result == S_OK)
        statusBar()->clearMessage();
    else
        statusBar()->showMessage(
            QCoreApplication::translate("mainwindow_filelist", "Could not read folder."));
    return result;
}

HRESULT MainWindow::startFilesystemScan(IFolderFolder *folder, bool flat) {
    projectedUi().scanningDisk = true;
    setState(State::Filesystem);
    statusBar()->showMessage(QtLangText(IDS_SCANNING));
    if (projectedUi().filesystemScanner->start(projectedUi().directory, flat, folder))
        return S_OK;
    failFilesystemListing(E_ABORT);
    return E_ABORT;
}

HRESULT MainWindow::appendFilesystemEntry(const QFileInfo &file, quint32 sourceIndex) {
    QIcon icon;
    RINOK(app_.GetFocusedPanel().GetItemIcon(sourceIndex, icon))
#ifdef Q_OS_WIN
    // Normal Windows folders already use source FindChangeNotification. Keep
    // the portable fallback for flat descendants, outside its nonrecursive watch.
    if (app_.GetFocusedPanel().GetFlatMode())
#endif
        panelUi_[projectedPanel_].autoRefreshSnapshot.insert(
            file.absoluteFilePath(), filesystemSignature(file.absoluteFilePath()));
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
    projectedUi().table->panelModel()->append(std::move(row));
    return S_OK;
}

void MainWindow::connectFilesystemScanner() {
    for (unsigned index = 0; index < kNumPanelsMax; ++index) {
        auto &ui = panelUi_[index];
        ui.listProjection = new QtFolderProjection(this);
        ui.filesystemScanner = new FilesystemScanner(this);
        auto *scanner = ui.filesystemScanner;
        connect(scanner, &FilesystemScanner::folderReady, this,
                [this, index](IFolderFolder *folder) {
                    withPanelProjection(index, [&] {
                        if (app_.QtOperations().closed())
                            return;
                        QtAppOperationState::CallScope call(app_.QtOperations());
                        auto &panel = app_.GetFocusedPanel();
                        if (panel.GetFolder() != folder)
                            panel.SetNewFolder(folder);
                        panel.LoadFullPath();
                        const HRESULT loaded = panel.LoadListItems(true);
                        if (loaded != S_OK) {
                            panelUi_[index].filesystemScanner->cancel();
                            failFilesystemListing(loaded);
                            return;
                        }
                        configureColumns();
                        appendParentEntry();
                    });
                });
        connect(scanner, &FilesystemScanner::batchReady, this,
                [this, index](const QFileInfoList &files, quint32 firstSourceIndex) {
                    withPanelProjection(index, [&] {
                        if (app_.QtOperations().closed())
                            return;
                        QtAppOperationState::CallScope call(app_.QtOperations());
                        quint32 sourceIndex = firstSourceIndex;
                        for (const QFileInfo &file : files) {
                            const HRESULT result = appendFilesystemEntry(file, sourceIndex++);
                            if (result != S_OK) {
                                panelUi_[index].filesystemScanner->cancel();
                                failFilesystemListing(result);
                                return;
                            }
                        }
                        statusBar()->showMessage(
                            QCoreApplication::translate("mainwindow", "Shown: %1 items…")
                                .arg(projectedUi().table->rowCount()));
                    });
                });
        connect(scanner, &FilesystemScanner::advanced, this, [this, index](quint64 count) {
            if (index != projectedPanel_)
                return;
            if (app_.QtOperations().closed())
                return;
            statusBar()->showMessage(
                QtLangText(IDS_SCANNING) +
                QCoreApplication::translate("mainwindow", " %1 items").arg(count));
        });
        connect(scanner, &FilesystemScanner::completed, this, [this, index] {
            withPanelProjection(index, [this] {
                if (app_.QtOperations().closed())
                    return;
                QtAppOperationState::CallScope call(app_.QtOperations());
                projectedUi().scanningDisk = false;
                applySortMode(true);
                completeListSelection();
                restoreAutoRefreshViewport();
                setState(State::Filesystem);
                statusBar()->showMessage(QCoreApplication::translate("mainwindow", "%1 items")
                                             .arg(projectedUi().table->rowCount()));
            });
        });
        connect(scanner, &FilesystemScanner::failed, this, [this, index](qint32 result) {
            withPanelProjection(index, [this, result] {
                if (app_.QtOperations().closed())
                    return;
                QtAppOperationState::CallScope call(app_.QtOperations());
                app_.GetFocusedPanel().LoadListItems(true, HRESULT(result));
                failFilesystemListing(HRESULT(result));
            });
        });
    }
}

void MainWindow::failFilesystemListing(HRESULT result) {
    auto &panel = app_.GetFocusedPanel();
    panelUi_[projectedPanel_].pendingSelection.reset();
    panelUi_[projectedPanel_].pendingViewport.reset();
    projectedUi().table->clearSelection();
    projectedUi().table->clearRows();
    projectedUi().table->setSortingEnabled(true);
    // OnCopy ignores RefreshListCtrl errors, including a retained worker load.
    // Navigation and explicit reload keep their existing diagnostic policy.
    if (!appPresentation_->IsCopyPending())
        panel.MessageBox_Error_HRESULT(result);
    if (app_.QtOperations().closed())
        return;
    projectedUi().scanningDisk = false;
    setState(State::Filesystem);
    statusBar()->showMessage(QCoreApplication::translate("mainwindow", "Could not read folder."));
}
