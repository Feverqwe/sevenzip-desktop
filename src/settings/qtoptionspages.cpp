#include "settings/qtoptionspages.h"
#include <QButtonGroup>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QRadioButton>
#include <QShortcut>

// The native property sheet activates the new page's first tab stop after
// Ctrl+Tab, rather than retaining a previous Qt child or focusing the tab bar.
QtOptionsPages::QtOptionsPages(QWidget *parent) : QTabWidget(parent) {
    const Qt::KeyboardModifier controls[] = {
        Qt::ControlModifier,
#ifdef Q_OS_MACOS
        Qt::MetaModifier, // Physical Control under Cocoa's default modifier mapping.
#endif
    };
    for (const auto control : controls)
        for (const bool reverse : {false, true}) {
            const auto shift = reverse ? Qt::ShiftModifier : Qt::NoModifier;
            auto *shortcut =
                new QShortcut(QKeySequence(QKeyCombination(control | shift, Qt::Key_Tab)), this);
            connect(shortcut, &QShortcut::activated, this, [this, shift] {
                // Window context includes the footer. Keep Qt's own
                // enabled/visible-page selection and our source focus rule.
                QKeyEvent event(QEvent::KeyPress, Qt::Key_Tab, Qt::ControlModifier | shift);
                keyPressEvent(&event);
            });
        }
}

void QtOptionsPages::focusInitialControl() {
    QWidget *page = currentWidget();
    if (page) {
        for (QWidget *control = page->nextInFocusChain(); control != page;
             control = control->nextInFocusChain()) {
            if (!page->isAncestorOf(control) || !control->isVisibleTo(page) ||
                !control->isEnabled() || !(control->focusPolicy() & Qt::TabFocus))
                continue;
            if (auto *radio = qobject_cast<QRadioButton *>(control);
                radio && radio->group() && radio->group()->exclusive()) {
                if (auto *checked = radio->group()->checkedButton();
                    checked && checked->isEnabled())
                    control = checked;
            }
            QPointer<QWidget> target(control);
            control->setFocus(Qt::OtherFocusReason);
            if (auto *edit = qobject_cast<QLineEdit *>(target.data()))
                edit->selectAll();
            return;
        }
    }
    for (const char *name : {"settingsNavigation", "settingsSectionChoice"})
        if (auto *navigation = parentWidget()->findChild<QListWidget *>(name);
            navigation && navigation->isVisible() && navigation->isEnabled()) {
            navigation->setFocus(Qt::TabFocusReason);
            return;
        }
}

void QtOptionsPages::keyPressEvent(QKeyEvent *event) {
    QPointer<QTabWidget> alive(this);
    const int before = currentIndex();
    const bool cycle = event->matches(QKeySequence::NextChild) ||
                       event->matches(QKeySequence::PreviousChild) ||
                       ((event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab) &&
                        (event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier)));
    QTabWidget::keyPressEvent(event); // Qt owns page selection and platform key bindings.
    if (alive && cycle && event->isAccepted() && currentIndex() != before)
        focusInitialControl();
}
