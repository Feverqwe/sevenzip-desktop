#include "archive/resources/archiveinputpath.h"
#include "filemanager/MyLoadMenu.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtwindowworkspace.h"
#include <QDir>
#include <QFileInfo>
#include <QTimer>

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

void MainWindow::recordVisitedPath(const QString &path) { workspace_->RecordVisit(path); }

void MainWindow::showFoldersHistory() {
    if (projectedUi().state == State::Loading || projectedUi().state == State::Operating)
        return;
    if (auto *anchor = projectedUi().recentPlaces) {
        const unsigned panel = projectedPanel_;
        QTimer::singleShot(0, anchor,
                           [this, panel, anchor] { workspace_->ShowRecentPlaces(panel, *anchor); });
    }
}
void MainWindow::refreshBookmarks() { MyLoadBookmarks(openBookmarks_, saveBookmarks_, app_); }

void MainWindow::saveBookmark(int index) {
    if (index < 0 || index >= 10 || projectedUi().state == State::Loading ||
        projectedUi().state == State::Operating)
        return;
    auto &panel = app_.GetFocusedPanel();
    panel.LoadFullPath();
    workspace_->SetBookmark(unsigned(index), panel.GetCurrentFolderPrefix());
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
