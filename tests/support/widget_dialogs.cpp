#include "support/widget_dialogs.h"
#include <QApplication>
#include <QEvent>
#include <QPointer>
#include <QScopeGuard>
#include <QThread>
#include <QTimer>
#include <QWindow>

WidgetDialogFixture::WidgetDialogFixture()
    : previous_(QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs)) {
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    qApp->installEventFilter(this);
}

WidgetDialogFixture::~WidgetDialogFixture() {
    qApp->removeEventFilter(this);
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, previous_);
}

bool WidgetDialogFixture::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::Show)
        if (auto *dialog = qobject_cast<QDialog *>(watched)) {
            if (!shown_.contains(dialog))
                connect(dialog, &QObject::destroyed, this, [this, dialog] {
                    shown_.remove(dialog);
                    answering_.remove(dialog);
                });
            const quint64 generation = ++generation_;
            shown_.insert(dialog, {QThread::currentThread()->loopLevel(), generation});
            const QPointer<QDialog> liveDialog(dialog);
            // A shown window-modal result uses the existing event loop. It
            // becomes answerable after that show returns, without requiring exec().
            QTimer::singleShot(0, this, [this, liveDialog, generation] {
                auto shown = shown_.find(liveDialog);
                if (shown != shown_.end() && shown->generation == generation)
                    shown->settled = true;
            });
        }
    return QObject::eventFilter(watched, event);
}

bool WidgetDialogFixture::ready(QDialog &dialog) const {
    const auto shown = shown_.constFind(&dialog);
    if (shown == shown_.cend() || !dialog.isVisible() ||
        (QThread::currentThread()->loopLevel() <= shown->loop && !shown->settled) ||
        !dialog.windowHandle() || !dialog.windowHandle()->isExposed())
        return false;
    if (!dialog.isActiveWindow()) {
        dialog.raise();
        dialog.activateWindow();
        return false;
    }
    return true;
}

QDialog *WidgetDialogFixture::activeDialog() const {
    auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
    return dialog && ready(*dialog) ? dialog : nullptr;
}

void WidgetDialogFixture::respond(QObject &context, std::function<void(QDialog &)> callback) {
    auto *timer = new QTimer(&context);
    connectResponder(*timer, &context, [this, timer, callback = std::move(callback)] {
        if (auto *dialog = activeDialog()) {
            timer->stop();
            const QPointer<QTimer> liveTimer(timer);
            callback(*dialog);
            if (liveTimer)
                liveTimer->deleteLater();
        }
    });
    timer->start(5);
}

void WidgetDialogFixture::connectResponder(QTimer &timer, QObject *context,
                                           std::function<void()> callback) {
    const QPointer<QTimer> liveTimer(&timer);
    const QPointer<WidgetDialogFixture> fixture(this);
    connect(
        &timer, &QTimer::timeout, context,
        [fixture, liveTimer, callback = std::move(callback)] {
            if (!fixture || !liveTimer || !liveTimer->isActive())
                return;
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (dialog && fixture->answering_.contains(dialog))
                return;
            if (dialog)
                fixture->answering_.insert(dialog);
            const auto release = qScopeGuard([fixture, dialog] {
                if (fixture)
                    fixture->answering_.remove(dialog);
            });
            callback();
        },
        Qt::QueuedConnection);
}
