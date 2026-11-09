#include "ui/controls/qtwindowstatusbar.h"
#include "ui/controls/qtstatuslabel.h"
#include <QScopedValueRollback>

QtWindowStatusBar::QtWindowStatusBar(QWidget *parent) : QStatusBar(parent) {
    setObjectName("windowStatusBar");
    setSizeGripEnabled(false);
    summary_ = new QtStatusLabel(this);
    summary_->setObjectName("selectedItemsStatus");
    summary_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    addWidget(summary_, 1);
    size_ = new QLabel(this);
    size_->setObjectName("selectedSizeStatus");
    size_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    addPermanentWidget(size_);
    connect(this, &QStatusBar::messageChanged, this, [this](const QString &message) {
        // Keep the inherited QMainWindow/QStatusBar entry point usable for
        // startup diagnostics and other direct consumers.
        if (!rendering_ && context_)
            SetMessage(context_, message);
        size_->setVisible(message.isEmpty() && !loading_ && !size_->text().isEmpty());
    });
}

void QtWindowStatusBar::SetSummary(QObject *context, const QString &summary, const QString &size,
                                   const QString &exactSize, bool loading) {
    context_ = context;
    WatchContext(context);
    loading_ = loading;
    summary_->setText(summary);
    size_->setText(size);
    size_->setToolTip(exactSize);
    Render();
}

void QtWindowStatusBar::SetMessage(QObject *context, const QString &message, int timeout) {
    if (!context)
        return;
    WatchContext(context);
    feedback_.insert(context, {message, QDeadlineTimer(timeout > 0 ? timeout : 5000)});
    Render();
}

void QtWindowStatusBar::WatchContext(QObject *context) {
    if (!context || feedback_.contains(context))
        return;
    feedback_.insert(context, {});
    connect(context, &QObject::destroyed, this, [this, context] {
        feedback_.remove(context);
        Render();
    });
}

void QtWindowStatusBar::ClearMessageIf(QObject *context, const QString &message) {
    const auto found = feedback_.find(context);
    if (found == feedback_.end() || found->text != message)
        return;
    *found = {};
    Render();
}

void QtWindowStatusBar::Render() {
    const QScopedValueRollback<bool> rendering(rendering_, true);
    if (!context_) {
        summary_->clear();
        size_->clear();
        loading_ = false;
    }
    const auto found = feedback_.constFind(context_);
    if (!loading_ && found != feedback_.cend() && !found->expiry.hasExpired())
        QStatusBar::showMessage(found->text, int(found->expiry.remainingTime()));
    else
        QStatusBar::clearMessage();
    size_->setVisible(currentMessage().isEmpty() && !loading_ && !size_->text().isEmpty());
}
