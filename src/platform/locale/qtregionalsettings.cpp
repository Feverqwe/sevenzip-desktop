#include "platform/locale/qtregionalsettings.h"
#include <QCoreApplication>
#include <QEvent>
#include <QPointer>
#include <QThread>

void QtRegionalSettings::Observe(QObject &receiver, std::function<void()> refresh) {
    auto *application = QCoreApplication::instance();
    Q_ASSERT(application && QThread::currentThread() == application->thread());
    auto *settings = application->findChild<QtRegionalSettings *>();
    if (!settings)
        settings = new QtRegionalSettings(*application);
    connect(settings, &QtRegionalSettings::Changed, &receiver, std::move(refresh));
}

QtRegionalSettings::QtRegionalSettings(QObject &application) : QObject(&application) {
    pending_.setSingleShot(true);
    pending_.setInterval(0);
    connect(&pending_, &QTimer::timeout, this, &QtRegionalSettings::Changed);
    application.installEventFilter(this);
    native_ = QtCreateRegionalNotification([guard = QPointer<QtRegionalSettings>(this)] {
        if (guard)
            guard->QueueRefresh();
    });
}

void QtRegionalSettings::QueueRefresh() {
    QMetaObject::invokeMethod(this, [this] { pending_.start(); }, Qt::QueuedConnection);
}

bool QtRegionalSettings::eventFilter(QObject *object, QEvent *event) {
    if (object == QCoreApplication::instance() && event->type() == QEvent::LocaleChange)
        QueueRefresh();
    return QObject::eventFilter(object, event);
}

#if !defined(Q_OS_MACOS) && !defined(Q_OS_WIN)
QLocale QtSystemRegionalUnitsLocale() { return QLocale::system(); }
QString QtSystemRegionalNumber(quint64 value) {
    return QLocale::system().toString(qulonglong(value));
}
QString QtSystemRegionalNumber(double value, int precision) {
    return QLocale::system().toString(value, 'f', precision);
}
QString QtSystemRegionalDateTime(const QDateTime &local, QLocale::FormatType format) {
    return QLocale::system().toString(local, format);
}

std::unique_ptr<QtRegionalNotification> QtCreateRegionalNotification(std::function<void()>) {
    return {};
}
#endif
