#include "7zip/UI/FileManager/resource.h"
#include "filemanager/panels/qtnativepanelkeys.h"
#include "filemanager/panels/qtpaneladdresscombo.h"
#include "filemanager/panels/qtpanelkey.h"
#include "filemanager/window/mainwindow.h"
#ifdef Q_OS_WIN
#include <windows.h>
#endif

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
        ((k >= Qt::Key_A && k <= Qt::Key_Z && QStringLiteral("ACNVWXRZ").contains(QChar(k))) ||
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
    return mods.testFlag(Qt::KeypadModifier) &&
           (k == Qt::Key_Plus || k == Qt::Key_Minus || k == Qt::Key_Asterisk);
}

bool MainWindow::handlePanelKey(QObject *watched, QKeyEvent *key) {
    const bool address = watched == path_ || watched == pathCombo_;
    const bool list = watched == table_ || watched == table_->viewport() || watched == iconList_ ||
                      (iconList_ && watched == iconList_->viewport());
    if (!address && !list)
        return false;
    if (list && isPanelEditorFocused())
        return false;
    if (address) {
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter ||
            key->key() == Qt::Key_Escape) {
            if (auto *header = dynamic_cast<IQtPanelAddressCombo *>(pathCombo_)) {
                app_.GetFocusedPanel().OnNotifyComboBoxEndEdit(key->key() == Qt::Key_Escape
                                                                   ? QtAddressEditReason::Escape
                                                                   : QtAddressEditReason::Return,
                                                               *header);
                return true;
            }
        }
        const auto modifiers = key->modifiers();
        if (modifiers.testFlag(Qt::MetaModifier))
            return false;
        return app_.GetFocusedPanel().OnAddressEditKeyDown(
            {key->key() == Qt::Key_Backtab ? Qt::Key_Tab : key->key(),
             modifiers.testFlag(Qt::AltModifier), modifiers.testFlag(Qt::ControlModifier), false,
             modifiers.testFlag(Qt::ShiftModifier)},
            app_);
    }
    return handlePanelListKey(key);
}

bool MainWindow::handlePanelListKey(QKeyEvent *key) {
    if (app_.QtOperations().closed() || app_.QtOperations().blocking() ||
        app_.QtOperations().callActive() || sourceArchiveProgress_ || scanningDisk_ ||
        scanningArchive_ || QApplication::activeModalWidget())
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
    if (!mods.testFlag(Qt::MetaModifier) &&
        panel.OnListViewKeyDown({virtualKey, mods.testFlag(Qt::AltModifier),
                                 mods.testFlag(Qt::ControlModifier), false,
                                 mods.testFlag(Qt::ShiftModifier)},
                                app_))
        return true;
    const bool arrow = key->key() == Qt::Key_Up || key->key() == Qt::Key_Down ||
                       key->key() == Qt::Key_Left || key->key() == Qt::Key_Right;
    if (panel._mySelectMode && mods.testFlag(Qt::ShiftModifier) && arrow &&
        !selectionShiftSequence_)
        panel.BeginShiftSelection();
    if (key->key() == Qt::Key_Shift || (mods.testFlag(Qt::ShiftModifier) && arrow))
        selectionShiftSequence_ = true;
    else if (!mods.testFlag(Qt::ShiftModifier))
        selectionShiftSequence_ = false;
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
    if ((!mods.testFlag(Qt::KeypadModifier) || mods.testFlag(Qt::MetaModifier)) &&
        (virtualKey == Qt::Key_Plus || virtualKey == Qt::Key_Minus ||
         virtualKey == Qt::Key_Asterisk))
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
    if (panel._mySelectMode && mods.testFlag(Qt::ShiftModifier) && arrow &&
        (key->key() == Qt::Key_Down || !mods.testFlag(Qt::AltModifier))) {
        // Complete this key's Qt focus movement before applying the source
        // range. A queued timer can otherwise observe a later key's focus.
        const QPointer<MainWindow> window(this);
        const QPointer<CPanel> owner(&panel);
        if (viewStack_->currentWidget() == table_)
            static_cast<FileTable *>(table_)->handleDefaultKeyPress(key);
        else
            static_cast<FileIconList *>(iconList_)->handleDefaultKeyPress(key);
        if (window && owner && !app_.QtOperations().closed()) {
            owner->OnShiftSelectMessage();
            refreshActions();
        }
        return true;
    }
    if (key->key() != Qt::Key_Control && key->key() != Qt::Key_Shift && key->key() != Qt::Key_Alt)
        refreshActions();
    // Qt otherwise moves focus again after the source OnTab callback.
    return handled || key->key() == Qt::Key_Tab;
}
