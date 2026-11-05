#include "ui/controls/qtsecretedit.h"
#include <QAbstractButton>
#include <QFocusEvent>

namespace {
struct EditPosition {
    int cursor, start, length;
    explicit EditPosition(const QLineEdit &edit)
        : cursor(edit.cursorPosition()), start(edit.selectionStart()),
          length(edit.selectionLength()) {}
    void Restore(QLineEdit &edit) const {
        if (cursor == edit.cursorPosition() && start == edit.selectionStart() &&
            length == edit.selectionLength())
            return;
        if (length) {
            const int anchor = cursor == start ? start + length : start;
            edit.setSelection(anchor, cursor - anchor);
        } else
            edit.setCursorPosition(cursor);
    }
};
} // namespace

QtSecretEdit::QtSecretEdit(QWidget *parent) : QLineEdit(parent) {
    setEchoMode(QLineEdit::Password);
}

void QtSecretEdit::BindRevealControl(QAbstractButton &control) {
    if (reveal_)
        disconnect(reveal_, &QAbstractButton::toggled, this, &QtSecretEdit::SetRevealed);
    reveal_ = &control;
    connect(&control, &QAbstractButton::toggled, this, &QtSecretEdit::SetRevealed);
    SetRevealed(control.isChecked());
}

void QtSecretEdit::SetRevealed(bool revealed) {
    const EditPosition position(*this);
    setEchoMode(revealed ? QLineEdit::Normal : QLineEdit::Password);
    position.Restore(*this);
}

void QtSecretEdit::focusOutEvent(QFocusEvent *event) {
    const EditPosition position(*this);
    QLineEdit::focusOutEvent(event);
    // Native focus-out may deselect before the checkbox's toggled signal arrives.
    // Keep the checkbox focused; only its associated edit retains the selection.
    if (reveal_ && reveal_->hasFocus())
        position.Restore(*this);
}
