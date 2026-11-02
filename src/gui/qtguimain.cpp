#include "archive/backend/archivebackend.h"
#include "dialogs/compress/createarchivedialog.h"
#include "gui/qtguicommand.h"
#include "localization/LangUtils.h"
#include "operations/qtoperationendpoint.h"
#include "platform/shell/qtshellarchiveservices.h"
#include "settings/RegistryUtils.h"
#include <QApplication>
#include <QEventLoop>
#include <memory>

int RunSevenzipGUICommand(QApplication &application, const QtGUICommandSource &command,
                          const QtGUIOutputSink &outputReady) {
    // The helper owns sequential source modals, without a File Manager window.
    // Closing one modal must not terminate the next source workflow stage.
    application.setQuitOnLastWindowClosed(false);
    LoadLangOneTime();
    ArchiveBackend backend;
    std::unique_ptr<QtShellArchiveServices> services;
    const QtGUIServiceFactory factory = [&](bool hashOnly) -> QtShellArchiveServices & {
        // Main2 invokes the factory after Parse2 acknowledged its mapping.
        QEventLoop discovery;
        bool ready = false;
        QString error;
        QObject::connect(&backend, &ArchiveBackend::codecCatalogReady, &discovery,
                         [&](bool success, const QString &message) {
                             ready = success;
                             error = message;
                             discovery.quit();
                         });
        if (backend.discoverCodecCatalog())
            discovery.exec();
        if (!ready && !hashOnly)
            throw UString(error.toStdWString().c_str());
        services = std::make_unique<QtShellArchiveServices>(
            backend, nullptr,
            QtShellArchiveServices::Routes{
                .Modal =
                    [&backend](CCompressDialog &source,
                               const CObjectVector<NWildcard::CCensorPath> &paths,
                               QWidget *parent) {
                        return CreateArchiveDialog::PresentSource(source, paths,
                                                                  backend.codecCatalog(), parent);
                    },
                .OutputReady = outputReady},
            FmSettingsPath());
        return *services;
    };
    return SevenzipGUI(factory, command);
}

int RunSevenzipGUI(QApplication &application) {
    application.setQuitOnLastWindowClosed(false);
    const auto arguments = application.arguments();
    if (arguments.size() == 3 && arguments[1] == "--operation-channel")
        return RunGUIOperationEndpoint(
            application, arguments[2],
            [&](const QtGUICommandSource &command, const QtGUIOutputSink &outputReady) {
                return RunSevenzipGUICommand(application, command, outputReady);
            });
    return RunSevenzipGUICommand(application, {});
}
