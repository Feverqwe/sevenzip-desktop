#pragma once
#include "support/window_activation.h"
#include <QCheckBox>
#include <QDialog>
#include <QLineEdit>
#include <QSignalSpy>
#include <QtTest>

namespace SecretVisibility {
// A real focus transfer catches deselection before toggled; button.click() does not.
inline void verify(QDialog &dialog, QLineEdit &edit, QCheckBox &show) {
    dialog.show();
    QVERIFY(WindowActivation::waitForActive(dialog));
    const QString draft = QStringLiteral("  пароль 密碼 with spaces  ");
    for (bool keyboard : {false, true}) {
        for (int selection : {0, 1, -1}) {
            show.setChecked(false);
            edit.setText(draft);
            edit.setFocus();
            QTRY_VERIFY(edit.hasFocus());
            if (selection)
                edit.setSelection(selection > 0 ? 2 : 6, selection * 4);
            else
                edit.setCursorPosition(4);
            const int cursor = edit.cursorPosition();
            const int start = edit.selectionStart();
            const int length = edit.selectionLength();
            QSignalSpy changed(&edit, &QLineEdit::textChanged);
            for (bool revealed : {true, false}) {
                if (keyboard) {
                    show.setFocus(Qt::TabFocusReason);
                    QTRY_VERIFY(show.hasFocus());
                    QTest::keyClick(&show, Qt::Key_Space);
                    QVERIFY(show.hasFocus());
                } else
                    QTest::mouseClick(&show, Qt::LeftButton, Qt::NoModifier,
                                      QPoint(8, show.height() / 2));
                QCOMPARE(edit.echoMode(), revealed ? QLineEdit::Normal : QLineEdit::Password);
                QCOMPARE(edit.text(), draft);
                QCOMPARE(edit.cursorPosition(), cursor);
                QCOMPARE(edit.selectionStart(), start);
                QCOMPARE(edit.selectionLength(), length);
            }
            QCOMPARE(changed.count(), 0);
        }
    }
}
} // namespace SecretVisibility
