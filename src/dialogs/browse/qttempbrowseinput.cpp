// Qt input bindings for BrowseDialog2; source provenance in BrowseDialog2.cpp.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "dialogs/browse/BrowseDialog2.h"
#include <QApplication>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>

void CBrowseDialog2::ConnectControls(QDialogButtonBox *buttons, QPushButton *refresh) {
    for (auto *button : findChildren<QPushButton *>()) {
        button->setAutoDefault(true);
        button->setDefault(false);
    }
    connect(_delete, &QPushButton::clicked, this, [this] {
        OnDelete();
        _list->setFocus();
    });
    connect(refresh, &QPushButton::clicked, this, [this] {
        Reload_WithErrorMessage();
        _list->setFocus();
    });
    connect(_parent, &QPushButton::clicked, this, [this] {
        OpenParentFolder();
        _list->setFocus();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(qApp, &QApplication::focusChanged, this, &CBrowseDialog2::OnFocusChanged);
    connect(_list, &QTreeWidget::itemDoubleClicked, this, [this] { OnItemEnter(); });
    connect(_list, &QTreeWidget::customContextMenuRequested, this, &CBrowseDialog2::OnContextMenu);
    connect(_list, &QTreeWidget::itemSelectionChanged, this,
            &CBrowseDialog2::UpdateDeleteAvailability);
    connect(_list->header(), &QHeaderView::sectionClicked, this,
            &CBrowseDialog2::ChangeSorting_and_Reload);
    auto *close = buttons->button(QDialogButtonBox::Close);
    setTabOrder(_delete, refresh);
    setTabOrder(refresh, _parent);
    setTabOrder(_parent, _pathEdit);
    setTabOrder(_pathEdit, _list);
    setTabOrder(_list, close);
    close->setFocus();
}
void CBrowseDialog2::OnFocusChanged(QWidget *, QWidget *current) {
    if (current == _list || current == _pathEdit) {
        // Run after Qt's focus/default restoration; source has no permanent
        // default button when a non-button control has focus.
        for (auto *button : findChildren<QPushButton *>())
            if (button->window() == this)
                button->setDefault(false);
    }
}

void CBrowseDialog2::keyPressEvent(QKeyEvent *event) {
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
        (!event->modifiers() || event->modifiers() == Qt::KeypadModifier)) {
        OnOK();
        event->accept();
        return;
    }
    QDialog::keyPressEvent(event);
}

bool CBrowseDialog2::eventFilter(QObject *object, QEvent *event) {
    if (object == _list && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (OnKeyDown(key))
            return true;
        switch (key->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
            OnItemEnter();
            return true;
        case Qt::Key_F5:
            // Retain the established Qt Refresh convenience after source Ctrl+F5.
            if (key->modifiers() == Qt::NoModifier) {
                Reload_WithErrorMessage();
                return true;
            }
            break;
        default:
            break;
        }
    }
    return QDialog::eventFilter(object, event);
}
