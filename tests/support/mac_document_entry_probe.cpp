#include "filemanager/views/filetable.h"
#include "filemanager/window/mainwindow.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QTimer>

namespace {
class StartupObservation final : public QObject {
  public:
    QElapsedTimer elapsed;
    qint64 firstShowMs = -1, readyMs = -1, previousTickMs = -1, maximumGapMs = 0;
    bool wrongDocumentShown = false;
    explicit StartupObservation(QObject *parent) : QObject(parent) { elapsed.start(); }
    bool eventFilter(QObject *object, QEvent *event) override {
        auto *window = dynamic_cast<MainWindow *>(object);
        if (!window || event->type() != QEvent::Show)
            return false;
        if (firstShowMs < 0)
            firstShowMs = previousTickMs = elapsed.elapsed();
        const QString expected = qEnvironmentVariable("SEVENZIP_NATIVE_STARTUP_MEMBER");
        auto *table = window->findChild<FileTable *>("archiveTable");
        if (!expected.isEmpty() && (!table || table->rowCount() != 1 ||
                                    table->model()->index(0, 0).data().toString() != expected))
            wrongDocumentShown = true;
        return false;
    }
    void tick() {
        if (firstShowMs < 0)
            return;
        const qint64 now = elapsed.elapsed();
        maximumGapMs = qMax(maximumGapMs, now - previousTickMs);
        previousTickMs = now;
    }
};

bool WriteStartupReport(bool timedOut, StartupObservation &observation) {
    const QString expected = qEnvironmentVariable("SEVENZIP_NATIVE_STARTUP_MEMBER");
    int windows = 0, archives = 0;
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        auto *window = dynamic_cast<MainWindow *>(widget);
        if (!window || !window->isVisible())
            continue;
        ++windows;
        auto *table = window->findChild<FileTable *>("archiveTable");
        if (!expected.isEmpty() && table && table->rowCount() == 1 &&
            table->model()->index(0, 0).data().toString() == expected)
            ++archives;
    }
    if (!timedOut && (windows == 0 || (!expected.isEmpty() && archives == 0)))
        return false;
    if (observation.readyMs < 0)
        observation.readyMs = observation.elapsed.elapsed();
    if (!timedOut && observation.elapsed.elapsed() - observation.readyMs < 250)
        return false; // Observe event-loop delivery after the first exposure too.
    QFile report(qEnvironmentVariable("SEVENZIP_NATIVE_STARTUP_REPORT"));
    if (report.open(QIODevice::WriteOnly))
        report.write(
            QJsonDocument(QJsonObject{{"windows", windows},
                                      {"archives", archives},
                                      {"wrongDocumentShown", observation.wrongDocumentShown},
                                      {"firstShowMs", observation.firstShowMs},
                                      {"readyMs", observation.readyMs},
                                      {"maximumGapAfterShowMs", observation.maximumGapMs},
                                      {"timedOut", timedOut},
                                      {"arguments", QJsonArray::fromStringList(qApp->arguments())},
                                      {"executable", qApp->applicationFilePath()}})
                .toJson());
    return true;
}

void ObserveDocumentStartup() {
    if (qEnvironmentVariableIsEmpty("SEVENZIP_NATIVE_STARTUP_REPORT"))
        return;
    auto *application = QCoreApplication::instance();
    auto *observation = new StartupObservation(application);
    application->installEventFilter(observation);
    auto *observer = new QTimer(application);
    QObject::connect(observer, &QTimer::timeout, application, [observer, observation] {
        observation->tick();
        if (QThread::currentThread()->loopLevel() != 0 && WriteStartupReport(false, *observation)) {
            observer->stop();
            QCoreApplication::quit();
        }
    });
    observer->start(10);
    QTimer::singleShot(15000, application, [observer, observation] {
        if (observer->isActive()) {
            WriteStartupReport(true, *observation);
            observer->stop();
            QCoreApplication::quit();
        }
    });
}
} // namespace

// Observe the real src/main.cpp entry, including its pre-desktop initialization.
Q_COREAPP_STARTUP_FUNCTION(ObserveDocumentStartup)
int SevenzipDesktopMain(int argc, char **argv);
int main(int argc, char **argv) { return SevenzipDesktopMain(argc, argv); }
