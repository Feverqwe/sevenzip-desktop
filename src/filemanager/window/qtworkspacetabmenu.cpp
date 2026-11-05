#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtwindowworkspace.h"
#include <QApplication>
#include <QMenu>
#include <QTabBar>

void QtWindowWorkspace::TabMenu(unsigned panel, const QPoint &point) {
    const int index = panels_[panel].bar->tabAt(point);
    if (index < 0)
        return;
    const QString id = panels_[panel].tabs[index].id;
    QMenu menu;
    connect(menu.addAction(tr("New tab")), &QAction::triggered, this,
            [this, panel] { NewTab(panel); });
    connect(menu.addAction(tr("Duplicate location")), &QAction::triggered, this, [this, id] {
        if (!Ready())
            return;
        const auto position = FindTab(id);
        if (!position)
            return;
        if (panels_[position->panel].current == position->index)
            Capture(position->panel, false);
        const QString path = panels_[position->panel].tabs[position->index].path;
        NewTab(position->panel, path);
    });
    connect(menu.addAction(tr("Move to other panel")), &QAction::triggered, this, [this, id] {
        if (!Ready() || !FindTab(id))
            return;
        if (window_.app_.NumPanels == 1)
            window_.togglePanels();
        WhenReady([this, id] {
            if (const auto position = FindTab(id))
                TransferTab(id, *this, 1 - position->panel);
        });
    });
    connect(menu.addAction(tr("Move to new window")), &QAction::triggered, this,
            [this, id] { DetachTab(id); });
    auto *windows = menu.addMenu(tr("Move to window"));
    for (auto *widget : QApplication::topLevelWidgets()) {
        auto *window = dynamic_cast<MainWindow *>(widget);
        if (!window || window == &window_ || !window->isVisible())
            continue;
        const QPointer<MainWindow> receiver(window);
        connect(windows->addAction(window->windowTitle()), &QAction::triggered, this,
                [this, receiver, id] {
                    if (receiver)
                        TransferTab(id, *receiver->workspace_, receiver->projectedPanel_);
                });
    }
    windows->setEnabled(!windows->actions().isEmpty());
    menu.addSeparator();
    connect(menu.addAction(tr("Close tab")), &QAction::triggered, this, [this, id] {
        if (const auto position = FindTab(id))
            CloseTab(position->panel, position->index);
    });
    menu.exec(panels_[panel].bar->mapToGlobal(point));
}
