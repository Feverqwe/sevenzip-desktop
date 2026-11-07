#pragma once
#include "filemanager/window/qtnavigationbar.h"
#include <QAction>
#include <QToolBar>
#include <QtTest>

namespace PanelKeys {
inline QWidget *navigationFor(QWidget *address) {
    for (auto *widget = address; widget; widget = widget->parentWidget())
        if (dynamic_cast<QtNavigationBar *>(widget))
            return widget;
    return address;
}
inline void focusAddress(QWidget *address) {
    if (auto *navigation = dynamic_cast<QtNavigationBar *>(navigationFor(address)))
        navigation->Edit();
    else
        address->setFocus();
}
inline void command(QWidget *view, Qt::Key key, Qt::KeyboardModifiers modifiers) {
    QTest::keyClick(view, key, modifiers);
    // A synthetic keyClick supplies modifiers on both key events. Release the
    // modifier keys too, as a physical chord does, before the next interaction.
    if (modifiers.testFlag(Qt::ShiftModifier))
        QTest::keyRelease(view, Qt::Key_Shift);
    if (modifiers.testFlag(Qt::ControlModifier))
        QTest::keyRelease(view, Qt::Key_Control);
    if (modifiers.testFlag(Qt::AltModifier))
        QTest::keyRelease(view, Qt::Key_Alt);
}
inline void openSelection(QWidget *view) {
#ifdef Q_OS_MACOS
    command(view, Qt::Key_Down, Qt::ControlModifier);
#else
    QTest::keyClick(view, Qt::Key_Return);
#endif
}

inline void formerPermanentDeleteShortcut(QWidget *view) {
#ifdef Q_OS_MACOS
    PanelKeys::command(view, Qt::Key_Backspace, Qt::ControlModifier | Qt::ShiftModifier);
#else
    QTest::keyClick(view, Qt::Key_Delete, Qt::ShiftModifier);
#endif
}
} // namespace PanelKeys
