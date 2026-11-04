// Qt publication of native filesystem scan results; native owners stay in PanelListLoad/FSFolder.
#include "7zip/UI/FileManager/resource.h"
#include "Common/StringConvert.h"
#include "filemanager/providers/qtlocationfolder.h"
#include "filemanager/window/mainwindow.h"
#include "localization/qtlangcontrols.h"

void MainWindow::connectFilesystemScanner() {
    for (unsigned index = 0; index < kNumPanelsMax; ++index) {
        auto &ui = panelUi_[index];
        ui.listProjection = new QtFolderProjection(this);
        ui.filesystemScanner = new FilesystemScanner(this);
        auto *scanner = ui.filesystemScanner;
        connect(scanner, &FilesystemScanner::folderReady, this,
                [this, index](IFolderFolder *folder) { filesystemScanFolderReady(index, folder); });
        connect(scanner, &FilesystemScanner::batchReady, this,
                [this, index](quint32 first, quint32 count) {
                    filesystemScanBatchReady(index, first, count);
                });
        connect(scanner, &FilesystemScanner::advanced, this,
                [this, index](quint64 count) { filesystemScanAdvanced(index, count); });
        connect(scanner, &FilesystemScanner::completed, this,
                [this, index] { filesystemScanCompleted(index); });
        connect(scanner, &FilesystemScanner::failed, this,
                [this, index](qint32 result) { filesystemScanFailed(index, result); });
    }
}

void MainWindow::filesystemScanFolderReady(unsigned index, IFolderFolder *folder) {
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
}

void MainWindow::filesystemScanBatchReady(unsigned index, quint32 firstSourceIndex, quint32 count) {
    withPanelProjection(index, [&] {
        if (app_.QtOperations().closed())
            return;
        QtAppOperationState::CallScope call(app_.QtOperations());
        const QString directory =
            QtFilesystemPath(us2fs(GetFolderPath(app_.GetFocusedPanel().GetFolder())));
        for (quint32 i = 0; i < count; ++i) {
            const HRESULT result = appendFilesystemEntry(directory, firstSourceIndex + i);
            if (result != S_OK) {
                panelUi_[index].filesystemScanner->cancel();
                failFilesystemListing(result);
                return;
            }
        }
        statusBar()->showMessage(QCoreApplication::translate("mainwindow", "Shown: %1 items…")
                                     .arg(projectedUi().table->rowCount()));
    });
}

void MainWindow::filesystemScanAdvanced(unsigned index, quint64 count) {
    if (index != projectedPanel_)
        return;
    if (app_.QtOperations().closed())
        return;
    statusBar()->showMessage(QtLangText(IDS_SCANNING) +
                             QCoreApplication::translate("mainwindow", " %1 items").arg(count));
}

void MainWindow::filesystemScanCompleted(unsigned index) {
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
}

void MainWindow::filesystemScanFailed(unsigned index, qint32 result) {
    withPanelProjection(index, [this, result] {
        if (app_.QtOperations().closed())
            return;
        QtAppOperationState::CallScope call(app_.QtOperations());
        app_.GetFocusedPanel().LoadListItems(true, HRESULT(result));
        failFilesystemListing(HRESULT(result));
    });
}
