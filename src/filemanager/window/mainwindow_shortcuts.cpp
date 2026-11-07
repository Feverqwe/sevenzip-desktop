#include "filemanager/panels/qtnativepanelkeys.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtfileclipboard.h"
#include "filemanager/window/qtnavigationbar.h"
#include "filemanager/window/qtwindowworkspace.h"
#include "filemanager/window/qtworkspaceapplication.h"
#include "localization/qtlangcontrols.h"

void MainWindow::configureShortcuts() {
    const auto bindPanel = [this](QAction *action, const QList<QKeySequence> &keys) {
        action->setShortcuts(keys);
        action->setShortcutContext(Qt::WidgetShortcut);
        panelShortcutActions_.append(action);
    };
    const auto modern = [](Qt::Key key) { return QKeySequence(Qt::CTRL | Qt::SHIFT | key); };
    bindPanel(toolbarAdd_, {modern(Qt::Key_A)});
    bindPanel(extract_, {modern(Qt::Key_E)});
    bindPanel(test_, {modern(Qt::Key_T)});
    bindPanel(copy_, {modern(Qt::Key_C), QKeySequence(Qt::Key_F5)});
    bindPanel(move_, {modern(Qt::Key_M), QKeySequence(Qt::Key_F6)});
    bindPanel(createFolder_, {modern(Qt::Key_N), QKeySequence(Qt::Key_F7)});
    bindPanel(createFile_, {QKeySequence(Qt::SHIFT | Qt::Key_F4)});
    bindPanel(selectAll_, QKeySequence::keyBindings(QKeySequence::SelectAll));
    // Shift+Delete remains the source's explicit permanent-delete command.
    bindPanel(clipboard_->cut, {QKeySequence(Qt::CTRL | Qt::Key_X)});
    bindPanel(clipboard_->copy, QKeySequence::keyBindings(QKeySequence::Copy));
    bindPanel(clipboard_->paste, QKeySequence::keyBindings(QKeySequence::Paste));
    auto *edit = findChild<QMenu *>("editMenu");
    for (auto *action : {clipboard_->paste, clipboard_->copy, clipboard_->cut})
        edit->insertAction(edit->actions().first(), action);
    bindPanel(openInside_, {QKeySequence(Qt::CTRL | Qt::Key_PageDown)});
    bindPanel(openOutside_, {QKeySequence(Qt::SHIFT | Qt::Key_Return)});
    bindPanel(viewFile_, {QKeySequence(Qt::Key_F3)});
    bindPanel(editFile_, {QKeySequence(Qt::Key_F4)});
#ifdef Q_OS_MACOS
    bindPanel(openItem_, {QKeySequence(Qt::CTRL | Qt::Key_Down)});
    bindPanel(rename_, {QKeySequence(Qt::Key_Return), QKeySequence(Qt::Key_Enter),
                        QKeySequence(Qt::Key_F2)});
    bindPanel(remove_, {QKeySequence(Qt::CTRL | Qt::Key_Backspace),
                        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Backspace)});
    bindPanel(properties_, {QKeySequence(Qt::CTRL | Qt::Key_I)});
    bindPanel(up_, {QKeySequence(Qt::CTRL | Qt::Key_Up)});
    bindPanel(back_, {QKeySequence(Qt::CTRL | Qt::Key_BracketLeft)});
    bindPanel(forward_, {QKeySequence(Qt::CTRL | Qt::Key_BracketRight)});
#else
    bindPanel(openItem_, {QKeySequence(Qt::Key_Return), QKeySequence(Qt::Key_Enter)});
    bindPanel(rename_, {QKeySequence(Qt::Key_F2)});
    bindPanel(remove_, {QKeySequence(Qt::Key_Delete)});
    bindPanel(properties_, {QKeySequence(Qt::ALT | Qt::Key_Return)});
    bindPanel(up_, {QKeySequence(Qt::ALT | Qt::Key_Up)});
    bindPanel(back_, {QKeySequence(Qt::ALT | Qt::Key_Left)});
    bindPanel(forward_, {QKeySequence(Qt::ALT | Qt::Key_Right)});
#endif
    findChild<QAction *>("commentAction")->setShortcuts({});
    twoPanelsAction_->setShortcuts({modern(Qt::Key_2), QKeySequence(Qt::Key_F9)});
    panelShortcutActions_.append(twoPanelsAction_);
    refresh_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    settingsAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Comma));
    settingsAction_->setMenuRole(QAction::PreferencesRole);
    findChild<QAction *>("openArchiveAction")
        ->setShortcuts(QKeySequence::keyBindings(QKeySequence::Open));
    exitAction_->setShortcuts(QKeySequence::keyBindings(QKeySequence::Quit));
    foldersHistory_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_F12));
    for (int i = 0; i < 4; ++i)
        viewActions_[i]->setShortcut(QKeySequence(Qt::CTRL | (Qt::Key_1 + i)));
    for (int i = 0; i < 5; ++i)
        sortActions_[i]->setShortcut(QKeySequence(Qt::CTRL | (Qt::Key_F3 + i)));
    auto *focusPath = new QAction(this);
    focusPath->setObjectName("focusPathAction");
    focusPath->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
    addAction(focusPath);
    connect(focusPath, &QAction::triggered, this, [this] {
        if (!app_.QtOperations().closed() && !app_.QtOperations().blocking() &&
            !QApplication::activeModalWidget()) {
            projectedUi().pathCombo->hidePopup();
            projectedUi().navigation->Edit();
        }
    });
    auto *newWindow = new QAction(tr("New window"), this);
    newWindow->setObjectName("newWindowAction");
    QtBindLocalText(newWindow, "QtWindowWorkspace",
                    QT_TRANSLATE_NOOP("QtWindowWorkspace", "New window"));
    newWindow->setProperty("workspaceCommand", true);
    newWindow->setShortcut(QKeySequence::New);
    panelShortcutActions_.append(newWindow);
    addAction(newWindow);
    connect(newWindow, &QAction::triggered, this, [this] {
        if (auto *application = QtWorkspaceApplication::Find()) {
            application->NewWindow(workspace_->CurrentLocation());
        }
    });
    auto *closeWindow = new QAction(tr("Close window"), this);
    QtBindLocalText(closeWindow, "QtWindowWorkspace",
                    QT_TRANSLATE_NOOP("QtWindowWorkspace", "Close window"));
    closeWindow->setProperty("workspaceCommand", true);
    closeWindow->setObjectName("closeWindowAction");
    closeWindow->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_W));
    panelShortcutActions_.append(closeWindow);
    addAction(closeWindow);
    connect(closeWindow, &QAction::triggered, this, &QWidget::close);
    auto *file = findChild<QMenu *>("fileMenu");
    file->insertAction(file->actions().first(), newWindow);
    file->addAction(closeWindow);
    if (auto *application = QtWorkspaceApplication::Find()) {
        disconnect(exitAction_, &QAction::triggered, this, nullptr);
        connect(exitAction_, &QAction::triggered, application, &QtWorkspaceApplication::Quit);
    }
    for (QAction *action : findChildren<QAction *>())
        if (!action->shortcuts().isEmpty())
            action->setShortcutVisibleInContextMenu(true);
    for (const auto &ui : panelUi_) {
        if (ui.table)
            ui.table->addActions(panelShortcutActions_);
        if (ui.iconList)
            ui.iconList->addActions(panelShortcutActions_);
    }
    refreshToolbarTexts();
}

QAction *MainWindow::panelShortcut(const QKeyEvent &key) const {
    for (QAction *action : panelShortcutActions_)
        if (QtPanelShortcutMatches(key, action->shortcuts()))
            return action;
    return nullptr;
}

bool MainWindow::reservedPanelKey(const QKeyEvent &key) const {
    if (QtPanelShortcutMatches(key, {QKeySequence(Qt::CTRL | Qt::Key_N),
                                     QKeySequence(Qt::CTRL | Qt::Key_Z),
                                     QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)}))
        return true;
#ifdef Q_OS_MACOS
    if (key.key() == Qt::Key_Delete)
        return true;
#endif
    return false;
}

bool MainWindow::isPanelEditorFocused() const {
    QWidget *focus = QApplication::focusWidget();
    if (!focus)
        return false;
    const auto target = panelInputTarget(focus, projectedPanel_);
    return target != PanelInputTarget::List && target != PanelInputTarget::ListViewport &&
           ((projectedUi().table && projectedUi().table->isAncestorOf(focus)) ||
            (projectedUi().iconList && projectedUi().iconList->isAncestorOf(focus)));
}
