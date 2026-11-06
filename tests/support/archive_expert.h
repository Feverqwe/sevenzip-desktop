#pragma once

#include "support/widget_dialogs.h"
#include "ui/controls/qtdisclosure.h"
#include <QApplication>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QScopeGuard>
#include <QtTest>

// Visit the actual modal editor through the same button as the user. Failed
// assertions still dismiss the editor so the owning form can unwind normally.
inline bool EditArchiveExpert(QWidget &form, bool apply,
                              const std::function<void(QDialog &)> &edit) {
    auto *section = form.findChild<QtDisclosure *>("archiveAdditionalSection");
    auto *button = form.findChild<QPushButton *>("archiveExpertOptions");
    if (!section || !button)
        return false;
    section->setExpanded(true);
    WidgetDialogFixture dialogs;
    bool visited = false;
    dialogs.respond(form, [&](QDialog &editor) {
        if (visited || editor.objectName() != "archiveExpertDialog")
            return;
        visited = true;
        const auto dismiss = qScopeGuard([&] {
            if (editor.isVisible())
                editor.reject();
        });
        // Finish first-show fitting before simulating a user resize or opening
        // another modal child in the scenario.
        QApplication::processEvents();
        edit(editor);
        if (apply && !QTest::currentTestFailed())
            editor.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    });
    button->click();
    return visited;
}
