#include "7zip/UI/FileManager/resource.h"
#include "filemanager/panels/qtnativepanelkeys.h"
#include "filemanager/panels/qtpaneladdresscombo.h"
#include "filemanager/panels/qtpanelkey.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtnavigationbar.h"
#include "filemanager/window/qtwindowworkspace.h"
#include <QContextMenuEvent>
#include <QMouseEvent>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

MainWindow::PanelInputTarget MainWindow::panelInputTarget(QObject *object, unsigned panel) const {
    const auto &ui = panelUi_[panel];
    if (object == ui.path || object == ui.pathCombo)
        return PanelInputTarget::Address;
    if (object == ui.table || object == ui.iconList)
        return PanelInputTarget::List;
    if ((ui.table && object == ui.table->viewport()) ||
        (ui.iconList && object == ui.iconList->viewport()))
        return PanelInputTarget::ListViewport;
    return PanelInputTarget::Other;
}

unsigned MainWindow::panelIndexFor(QObject *object) const {
    for (unsigned index = 0; index < kNumPanelsMax; ++index)
        if (panelUi_[index].container && panelInputTarget(object, index) != PanelInputTarget::Other)
            return index;
    return kNumPanelsMax;
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    if (watched == this && event->type() == QEvent::Resize && workspace_)
        workspace_->Refresh();
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)
        updatePanelModifier(*static_cast<QKeyEvent *>(event));
    if (event->type() == QEvent::WindowDeactivate)
        rightControlPressed_ = false;
    if (event->type() == QEvent::FocusOut) {
        const unsigned panel = panelIndexFor(watched);
        if (panel < kNumPanelsMax && panelInputTarget(watched, panel) == PanelInputTarget::List)
            static_cast<QAbstractItemView *>(watched)->keyboardSearch({});
    }
    if (!panelProjectionDepth_ &&
        (event->type() == QEvent::FocusIn || event->type() == QEvent::MouseButtonPress ||
         event->type() == QEvent::MouseButtonDblClick || event->type() == QEvent::KeyPress)) {
        const unsigned panelIndex = panelIndexFor(watched);
        if (panelIndex < kNumPanelsMax && panelIndex != projectedPanel_ && app_.NumPanels == 2)
            activatePanelUi(panelIndex);
    }
    // FM.cpp WM_SETFOCUS delegates to the application owner.
    if (watched == this && event->type() == QEvent::FocusIn) {
        app_.SetFocusToLastItem();
        return true;
    }
    // Activation can be refused while an operation owns the source panel.
    // Classify against the resulting projection before delivering the event.
    const auto target = panelInputTarget(watched, projectedPanel_);
    if (target == PanelInputTarget::ListViewport &&
        (event->type() == QEvent::MouseButtonPress ||
         event->type() == QEvent::MouseButtonDblClick)) {
        const auto *mouse = static_cast<QMouseEvent *>(event);
        // Qt can emit doubleClicked for buttons that do not open files.
        if (event->type() == QEvent::MouseButtonDblClick && mouse->button() != Qt::LeftButton)
            return true;
        selectionClickModifiers_ = mouse->modifiers();
        auto *table = projectedUi().table;
        if (event->type() == QEvent::MouseButtonDblClick && watched == table->viewport() &&
            table->isEnabled() && !isPanelEditorFocused()) {
            const auto index = table->indexAt(mouse->position().toPoint());
            if (index.isValid()) {
                activateRow(index.row(), mouse->modifiers());
                return true;
            }
        }
    }
    if (target == PanelInputTarget::Address && event->type() == QEvent::ShortcutOverride) {
        const auto *key = static_cast<QKeyEvent *>(event);
        const auto mods = key->modifiers();
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter ||
            key->key() == Qt::Key_Escape) {
            event->accept();
            return true;
        }
        // Deliver the edit subclass's keys before application QAction shortcuts.
        if (!(mods & (Qt::MetaModifier | Qt::AltModifier)) &&
            ((key->key() == Qt::Key_W && mods.testFlag(Qt::ControlModifier)) ||
             (key->key() == Qt::Key_F9 && mods == Qt::NoModifier))) {
            event->accept();
            return true;
        }
    }
    const bool list = target == PanelInputTarget::List || target == PanelInputTarget::ListViewport;
    if (list && event->type() == QEvent::ContextMenu &&
        static_cast<QContextMenuEvent *>(event)->reason() == QContextMenuEvent::Keyboard) {
        auto *view = static_cast<QAbstractItemView *>(projectedUi().viewStack->currentWidget());
        showItemContextMenu(projectedPanel_, view, QPoint(-1, -1));
        event->accept();
        return true;
    }
    if (list && !isPanelEditorFocused() && event->type() == QEvent::ShortcutOverride) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (overridePanelKey(*key)) {
            event->accept();
            return true;
        }
    }
    if (event->type() == QEvent::KeyPress &&
        handlePanelKey(target, static_cast<QKeyEvent *>(event)))
        return true;
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::updatePanelModifier(const QKeyEvent &key) {
    if (key.key() != Qt::Key_Control || key.isAutoRepeat())
        return;
    // Qt QPA supplies native virtual keys, not a portable left/right flag.
    // Cocoa maps Command to Qt Control; XCB supplies the XKB keysym.
#ifdef Q_OS_MACOS
    const bool right = key.nativeVirtualKey() == 0x36; // kVK_RightCommand
#elif defined(Q_OS_WIN)
    // Windows QPA prefixes extended scan codes with 0xE000.
    const bool right =
        key.nativeVirtualKey() == VK_RCONTROL ||
        (key.nativeVirtualKey() == VK_CONTROL && (key.nativeScanCode() & 0xff00) == 0xe000);
#else
    const bool right = key.nativeVirtualKey() == 0xffe4; // XKB_KEY_Control_R
#endif
    if (right)
        rightControlPressed_ = key.type() == QEvent::KeyPress;
}

bool MainWindow::overridePanelKey(const QKeyEvent &key) const {
    if (panelShortcut(key) || reservedPanelKey(key))
        return true;
    const auto mods = key.modifiers();
    if (mods.testFlag(Qt::MetaModifier))
        return false;
    const int k = QtPanelVirtualKey(key);
    const bool ctrl = mods.testFlag(Qt::ControlModifier);
    const bool alt = mods.testFlag(Qt::AltModifier);
    const bool shift = mods.testFlag(Qt::ShiftModifier);
    // Native keypad keys must reach the list without becoming row-digit actions.
    if (mods.testFlag(Qt::KeypadModifier) && (ctrl || alt))
        return true;
    // Only list shortcuts conflicting with QAction/Qt defaults need claiming.
    // CPanel::OnKeyDown remains the dispatcher; text editors keep their keys.
    if (k == Qt::Key_Return || k == Qt::Key_Enter || k == Qt::Key_Backspace ||
        k == Qt::Key_Delete || k == Qt::Key_Tab)
        return true;
    if (ctrl &&
        ((k >= Qt::Key_A && k <= Qt::Key_Z && QStringLiteral("ACNVWXR").contains(QChar(k))) ||
         (k >= Qt::Key_0 && k <= Qt::Key_9) || k == Qt::Key_PageDown || k == Qt::Key_PageUp))
        return true;
    if (k >= Qt::Key_0 && k <= Qt::Key_9 && alt)
        return true;
    if (k >= Qt::Key_F3 && k <= Qt::Key_F7 && ctrl)
        return true;
    if (!alt && !ctrl && k >= Qt::Key_F2 && k <= Qt::Key_F7)
        return true;
    if (k == Qt::Key_F9 && !alt && !ctrl && !shift)
        return true;
    if (alt && !ctrl && !shift && (k == Qt::Key_F1 || k == Qt::Key_F2 || k == Qt::Key_F12))
        return true;
    return mods.testFlag(Qt::KeypadModifier) && shift && !alt && !ctrl &&
           (k == Qt::Key_Plus || k == Qt::Key_Minus);
}

bool MainWindow::handlePanelKey(PanelInputTarget target, QKeyEvent *key) {
    if (target == PanelInputTarget::Other)
        return false;
    if (target == PanelInputTarget::Address) {
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter ||
            key->key() == Qt::Key_Escape) {
            if (auto *header = dynamic_cast<IQtPanelAddressCombo *>(projectedUi().pathCombo)) {
                const bool completed = app_.GetFocusedPanel().OnNotifyComboBoxEndEdit(
                    key->key() == Qt::Key_Escape ? QtAddressEditReason::Escape
                                                 : QtAddressEditReason::Return,
                    *header);
                if (!completed)
                    projectedUi().navigation->ResumeEditing();
                return true;
            }
        }
        const auto modifiers = key->modifiers();
        if (modifiers.testFlag(Qt::MetaModifier))
            return false;
        // The source address command focuses its own list. Qt may need to
        // wait for publication or use the unavailable-location control.
        if ((key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab) &&
            !modifiers.testFlag(Qt::AltModifier)) {
            workspace_->RequestListFocus(projectedPanel_);
            return true;
        }
        return app_.GetFocusedPanel().OnAddressEditKeyDown(
            {key->key() == Qt::Key_Backtab ? Qt::Key_Tab : key->key(),
             modifiers.testFlag(Qt::AltModifier), modifiers.testFlag(Qt::ControlModifier), false,
             modifiers.testFlag(Qt::ShiftModifier)},
            app_);
    }
    return !isPanelEditorFocused() && handlePanelListKey(key);
}

bool MainWindow::handlePanelListKey(QKeyEvent *key) {
    if (app_.QtOperations().closed() || app_.QtOperations().blocking() ||
        app_.QtOperations().callActive() || sourceArchiveProgress_ || projectedUi().scanningDisk ||
        projectedUi().scanningArchive || QApplication::activeModalWidget())
        return true;
    CPanel &panel = app_.GetFocusedPanel();
    // Original Panel.cpp::OnNotify suppresses LVN_KEYDOWN with other list
    // notifications while a source reload/update owns the provider.
    if (!panel.ShouldProcessNotifications())
        return true;
    if (QAction *action = panelShortcut(*key)) {
        if (action->isEnabled() && action->isVisible())
            action->trigger();
        return true;
    }
    if (reservedPanelKey(*key))
        return true;
    const auto mods = key->modifiers();
    const int virtualKey = QtPanelVirtualKey(*key);
    if (virtualKey == Qt::Key_Tab && app_.NumPanels == 2 && !mods.testFlag(Qt::MetaModifier)) {
        workspace_->RequestListFocus(1 - projectedPanel_, true);
        return true;
    }
    if (!mods.testFlag(Qt::MetaModifier) &&
        panel.OnListViewKeyDown({virtualKey, mods.testFlag(Qt::AltModifier),
                                 mods.testFlag(Qt::ControlModifier), false,
                                 mods.testFlag(Qt::ShiftModifier)},
                                app_))
        return true;
    // WM_CHAR slash routes belong to CMyListView, outside OnKeyDown.
    if ((key->key() == Qt::Key_Backslash || key->key() == Qt::Key_Slash) &&
        !(mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        app_.ExecuteCommand(IDM_OPEN_ROOT_FOLDER);
        return true;
    }
    if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
        if (mods.testFlag(Qt::MetaModifier))
            return false;
        activateItems(mods);
        return true;
    }
    if ((virtualKey == Qt::Key_Plus || virtualKey == Qt::Key_Minus) &&
        (!mods.testFlag(Qt::KeypadModifier) || mods.testFlag(Qt::MetaModifier)))
        return false;
    bool rightCtrl = rightControlPressed_ && mods.testFlag(Qt::ControlModifier);
#ifdef Q_OS_WIN
    // Qt queues notifications; GetKeyState can already reflect a later key-up.
    rightCtrl = mods.testFlag(Qt::ControlModifier) &&
                (rightControlPressed_ || (::GetKeyState(VK_RCONTROL) & 0x8000));
#endif
    const bool handled = panel.OnKeyDown({virtualKey, mods.testFlag(Qt::AltModifier),
                                          mods.testFlag(Qt::ControlModifier), rightCtrl,
                                          mods.testFlag(Qt::ShiftModifier)},
                                         app_);
    if (key->key() != Qt::Key_Control && key->key() != Qt::Key_Shift && key->key() != Qt::Key_Alt)
        refreshActions();
    // Qt otherwise moves focus again after the source OnTab callback.
    return handled || virtualKey == Qt::Key_Tab;
}
