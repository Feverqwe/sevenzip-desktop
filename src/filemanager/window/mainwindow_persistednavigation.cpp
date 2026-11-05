#include "archive/resources/archiveinputpath.h"
#include "filemanager/MyLoadMenu.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/window/mainwindow.h"
#include <QDir>
#include <QFileInfo>

QString MainWindow::sourcePathFromDisplayPath(QString path) const {
    // A display separator follows the archive, including a nested archive
    // already open in this panel. Spaces in actual path components survive.
    const QString separator = QStringLiteral(" / ");
    for (int pos = path.indexOf(separator); pos >= 0;
         pos = path.indexOf(separator, pos + separator.size()))
        if (IsArchiveFileOrPackage(QFileInfo(path.left(pos))) ||
            (projectedUi().state == State::Viewing &&
             QDir::fromNativeSeparators(path.left(pos)) == projectedUi().displayArchive)) {
            path.replace(pos, separator.size(), QStringLiteral("/"));
            break;
        }
    return path;
}

void MainWindow::recordVisitedPath(const QString &path) {
    if (visitedPathDraft_ && visitedPathDraft_->panel == projectedPanel_) {
        visitedPathDraft_->path = path;
        return;
    }
    app_.AppState.FolderHistory.AddString(ToUString(path));
    app_.AppState.FolderHistory.Save(toolbarSettingsPath());
}

void MainWindow::showFoldersHistory() {
    if (projectedUi().state == State::Loading || projectedUi().state == State::Operating)
        return;
    const unsigned owner = app_.GetFocusedPanelIndex();
    const QString selected =
        CPanel::FoldersHistory(app_.AppState.FolderHistory, toolbarSettingsPath(), this);
    if (!selected.isNull()) {
        activatePanelUi(owner);
        navigateTo(sourcePathFromDisplayPath(selected), true);
    } else
        refreshActions();
}
void MainWindow::refreshBookmarks() { MyLoadBookmarks(openBookmarks_, saveBookmarks_, app_); }

void MainWindow::saveBookmark(int index) {
    if (index < 0 || index >= 10 || projectedUi().state == State::Loading ||
        projectedUi().state == State::Operating)
        return;
    auto &panel = app_.GetFocusedPanel();
    panel.LoadFullPath();
    CPanel::SetBookmark(app_.AppState.FastFolders, (unsigned)index, panel.GetCurrentFolderPrefix());
    app_.AppState.FastFolders.Save(toolbarSettingsPath());
    refreshActions();
}

void MainWindow::openBookmark(int index) {
    if (index < 0 || index >= 10 || projectedUi().state == State::Loading ||
        projectedUi().state == State::Operating)
        return;
    const QString path =
        ToQString(CPanel::OpenBookmark(app_.AppState.FastFolders, (unsigned)index));
    navigateTo(sourcePathFromDisplayPath(path), true);
}
