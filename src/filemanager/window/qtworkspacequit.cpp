#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtlocalfiletask.h"
#include "filemanager/window/qtworkspaceapplication.h"
#include "operations/qtoperationlauncher.h"
#include "operations/qtoperationregistry.h"

void QtWorkspaceApplication::Quit() {
    if (quitting_)
        return;
    QPointer<QWidget> blocker = QApplication::activeModalWidget();
    std::vector<QPointer<MainWindow>> closing;
    for (const auto &window : windows_)
        if (window && !window->app_.QtOperations().closed()) {
            closing.push_back(window);
            if (!blocker)
                blocker = window->applicationQuitBlocker();
        }
    if (blocker) {
        for (auto *task : QtLocalFileTask::Active())
            if (task->OwnsWindow(*blocker)) {
                task->Show();
                return;
            }
        blocker->show();
        blocker->raise();
        blocker->activateWindow();
        return;
    }
    const auto reviews = QtOperationRegistry::Application().Reviews();
    if (!reviews.isEmpty()) {
        ShowTaskReview(reviews.first().Id, true);
        return;
    }
    const auto localTasks = QtLocalFileTask::Active();
    if (!localTasks.isEmpty()) {
        localTasks.first()->Show();
        return;
    }
    const auto tasks = QtOperationLauncher::Active();
    if (!tasks.isEmpty()) {
        tasks.first()->Show();
        return;
    }
    Save();
    quitting_ = true;
    for (const auto &window : closing)
        if (window)
            window->close();
    for (const auto &window : windows_)
        if (window && !window->app_.QtOperations().closed()) {
            quitting_ = false;
            return;
        }
}
