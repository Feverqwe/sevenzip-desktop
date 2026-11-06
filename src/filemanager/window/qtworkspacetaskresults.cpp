#include "dialogs/taskresult/qttaskresultdialog.h"
#include "filemanager/window/qtworkspaceapplication.h"
#include "operations/qtoperationregistry.h"
#include "ui/lifecycle/qttaskpresentation.h"
#include <QApplication>
#include <QEvent>
#include <QTimer>

QtWorkspaceApplication::~QtWorkspaceApplication() {
    const auto dialogs = taskResults_;
    for (const auto &dialog : dialogs)
        delete dialog.data();
}

void QtWorkspaceApplication::InitializeTaskResults() {
    auto &registry = QtOperationRegistry::Application();
    connect(&registry, &QtOperationRegistry::reviewRequired, this,
            [this](const QUuid &id) { ShowTaskReview(id, false); });
    connect(&QtTaskPresentation::Application(), &QtTaskPresentation::changed, this,
            [this] { QTimer::singleShot(0, this, &QtWorkspaceApplication::PresentTaskReviews); });
    connect(qApp, &QApplication::focusChanged, this, [this] {
        for (const auto &review : QtOperationRegistry::Application().Reviews())
            if (!taskResults_.value(review.Id)) {
                QTimer::singleShot(0, this, &QtWorkspaceApplication::PresentTaskReviews);
                break;
            }
    });
    for (const auto &review : registry.Reviews())
        ShowTaskReview(review.Id, false);
}

void QtWorkspaceApplication::ShowTaskReview(const QUuid &id, bool activate) {
    // Never undo the user's minimize/hide choice on a later notification.
    if (!activate && taskResults_.value(id))
        return;
    if (!activate && !QtTaskPresentation::CanPresent()) {
        if (auto *modal = QApplication::activeModalWidget()) {
            if (reviewModal_ != modal) {
                reviewModal_ = modal;
                modal->installEventFilter(this);
                connect(modal, &QObject::destroyed, this, [this] {
                    QTimer::singleShot(0, this, &QtWorkspaceApplication::PresentTaskReviews);
                });
            }
        }
        return;
    }
    const QPointer<QWidget> previousWindow = QApplication::activeWindow();
    const QPointer<QWidget> previousFocus = QApplication::focusWidget();
    auto dialog = taskResults_.value(id);
    if (!dialog) {
        for (const auto &review : QtOperationRegistry::Application().Reviews())
            if (review.Id == id) {
                dialog = new QtTaskResultDialog(review);
                taskResults_.insert(id, dialog);
                connect(dialog, &QDialog::finished, this,
                        [id] { QtOperationRegistry::Application().Acknowledge(id); });
                connect(dialog, &QObject::destroyed, this, [this, id] { taskResults_.remove(id); });
                break;
            }
    }
    if (!dialog)
        return;
    if (activate)
        dialog->setAttribute(Qt::WA_ShowWithoutActivating, false);
    if (dialog->isMinimized())
        dialog->showNormal();
    else
        dialog->show();
    if (activate) {
        dialog->raise();
        dialog->activateWindow();
    } else if (previousWindow && previousWindow != dialog && previousWindow->isVisible() &&
               previousWindow->isEnabled()) {
        QApplication::setActiveWindow(previousWindow);
        if (previousFocus && previousFocus->isVisible() && previousFocus->isEnabled())
            previousFocus->setFocus(Qt::OtherFocusReason);
    }
}

void QtWorkspaceApplication::PresentTaskReviews() {
    for (const auto &review : QtOperationRegistry::Application().Reviews())
        ShowTaskReview(review.Id, false);
}

bool QtWorkspaceApplication::eventFilter(QObject *object, QEvent *event) {
    if (object == reviewModal_ && event->type() == QEvent::Hide) {
        object->removeEventFilter(this);
        reviewModal_.clear();
        QTimer::singleShot(0, this, &QtWorkspaceApplication::PresentTaskReviews);
    }
    return QObject::eventFilter(object, event);
}
