#include "7zip/UI/Common/ArchiveCommandLine.h"
#include "archive/backend/archivebackend.h"
#include "archive/codecs/archivecodelocation.h"
#include "gui/qtguicommand.h"
#include "operations/qtoperationendpoint.h"
#include "support/operation_dialogs.h"
#include "support/operation_kill.h"
#include "ui/progress/ProgressDialog2.h"
#include <QApplication>
#include <QDialog>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimer>
#include <cstdlib>

class Diagnostics final : public QObject {
    QPointer<ArchiveBackend> backend_;

  public:
    QStringList Messages;
    bool eventFilter(QObject *object, QEvent *event) override {
        if (auto *backend = qobject_cast<ArchiveBackend *>(object);
            backend && backend != backend_) {
            backend_ = backend;
            connect(backend, &ArchiveBackend::codecCatalogReady, this,
                    [this](bool ready, const QString &error) {
                        if (!ready)
                            Messages.append(error);
                    });
        }
        if (event->type() == QEvent::Hide)
            if (auto *dialog = dynamic_cast<CProgressDialog *>(object)) {
                NWindows::NSynchronization::CCriticalSectionLock lock(dialog->Sync._cs);
                for (unsigned i = 0; i < dialog->Sync.Messages.Size(); ++i)
                    Messages.append(QString::fromWCharArray(dialog->Sync.Messages[i]));
                Messages.append(
                    QString::fromWCharArray(dialog->Sync.FinalMessage.ErrorMessage.Message));
            }
        return false;
    }
};

bool WaitForBarrier(const QString &path) {
    if (path.isEmpty())
        return true;
    QEventLoop wait;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &wait, [&] {
        if (QFileInfo::exists(path))
            wait.quit();
    });
    poll.start(10);
    QTimer::singleShot(15000, &wait, &QEventLoop::quit);
    wait.exec();
    return QFileInfo::exists(path);
}

// Fixture-only synchronization around the production endpoint and native GUI
// dispatcher. No test environment switches enter the installed executable.
int main(int argc, char **argv) {
    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);
    const auto arguments = application.arguments();
    if (arguments.size() != 3 || arguments[1] != "--operation-channel")
        return 100;
    const QString barrier = qEnvironmentVariable("SEVENZIP_OPERATION_BARRIER");
    const QString report = qEnvironmentVariable("SEVENZIP_OPERATION_REPORT");
    Diagnostics diagnostics;
    application.installEventFilter(&diagnostics);
    QTimer dialogs;
    QObject::connect(&dialogs, &QTimer::timeout, &application, [&] {
        dialogs.stop();
        DriveOperationDialogs();
        dialogs.start(10);
    });
    dialogs.start(10);
    const int result = RunGUIOperationEndpoint(
        application, arguments[2],
        [&](const QtGUICommandSource &command, const QtGUIOutputSink &outputReady) {
            if (!WaitForBarrier(barrier))
                return 101;
            const auto terminal = qEnvironmentVariable("SEVENZIP_OPERATION_FORCE_EXIT");
            if (terminal == "crash")
                KillOperationProbe(QCoreApplication::applicationPid());
            if (terminal == "missing-report")
                std::_Exit(0);
            auto source = command;
            const auto profile = qEnvironmentVariable("SEVENZIP_OPERATION_BENCHMARK_PROFILE");
            if (!profile.isEmpty()) {
                source.Configure = [configure = command.Configure,
                                    profile](CArcCmdLineOptions &options) {
                    configure(options);
                    if (options.Command.CommandType != NCommandType::kBenchmark)
                        return;
                    // Bound real native Bench work through its ordinary source properties.
                    // Production defaults and the installed executable remain unchanged.
                    for (const auto &pair : {qMakePair("mt", "1"), qMakePair("d18", ""),
                                             qMakePair("timems", "50"), qMakePair("tic", "1")}) {
                        auto &property = options.Properties.AddNew();
                        property.Name = pair.first;
                        property.Value = pair.second;
                    }
                    if (profile == "failure" || profile == "total") {
                        auto &property = options.Properties.AddNew();
                        property.Name = "m";
                        property.Value =
                            profile == "failure" ? "unavailable-benchmark-method" : "CRC32";
                    }
                    options.NumIterations_Defined = true;
                    // Cancellation must interrupt unfinished work even when
                    // the GUI thread is scheduled after a short native pass.
                    options.NumIterations = profile == "normal-cancel" ? 1000 : 1;
                };
            }
            return RunSevenzipGUICommand(
                application, source, [&](const QString &path, QtGUIOutputKind kind) {
                    outputReady(path, kind);
                    const QString gate = qEnvironmentVariable("SEVENZIP_OPERATION_OUTPUT_BARRIER");
                    if (!gate.isEmpty()) {
                        QSaveFile ready(gate + ".ready");
                        if (ready.open(QIODevice::WriteOnly)) {
                            ready.write("ready");
                            ready.commit();
                        }
                        WaitForBarrier(gate);
                    }
                });
        });
    if (!report.isEmpty()) {
        QSaveFile detail(report + ".diagnostic");
        if (detail.open(QIODevice::WriteOnly)) {
            detail.write(
                QJsonDocument(
                    QJsonObject{{"engine", FindArchiveModule()},
                                {"archive", qEnvironmentVariable("SEVENZIP_OPERATION_ARCHIVE")},
                                {"exists", QFileInfo::exists(
                                               qEnvironmentVariable("SEVENZIP_OPERATION_ARCHIVE"))},
                                {"messages", diagnostics.Messages.join('\n')}})
                    .toJson());
            detail.commit();
        }
        QSaveFile output(report);
        if (!output.open(QIODevice::WriteOnly) || output.write(QByteArray::number(result)) < 0 ||
            !output.commit())
            return 102;
    }
    return result;
}
