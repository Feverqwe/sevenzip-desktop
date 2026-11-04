#include "filemanager/MyLoadMenu.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/window/mainwindow.h"

namespace {
QString SavedSourcePath(QString path) {
    // Earlier Qt settings used a display separator after the real archive.
    // Convert only that boundary; spaces inside source path components survive.
    const QString separator = QStringLiteral(" / ");
    for (int pos = path.indexOf(separator); pos >= 0;
         pos = path.indexOf(separator, pos + separator.size()))
        if (QFileInfo(path.left(pos)).isFile()) {
            path.replace(pos, separator.size(), QStringLiteral("/"));
            break;
        }
    return path;
}
} // namespace

void MainWindow::recordVisitedPath(const QString &path) {
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
        navigateTo(SavedSourcePath(selected), true);
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
    navigateTo(SavedSourcePath(path), true);
}
