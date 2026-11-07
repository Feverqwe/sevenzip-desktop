#include "filemanager/window/mainwindow.h"
#include "7zip/UI/FileManager/resourceGui.h"
#include "Windows/PropVariantConv.h"
#include "archive/resources/qtarchivefoldermanager.h"
#include "filemanager/qtfilemanagercommands.h"
#include "filemanager/qtfilemanagerdrag.h"
#include "filemanager/qtfilemanagerstartup.h"
#include "filemanager/qtprogramfilemenu.h"
#include "filemanager/rename/qtrenamecontroller.h"
#include "filemanager/window/qtapppresentation.h"
#include "filemanager/window/qtexternalfiletask.h"
#include "filemanager/window/qtfileclipboard.h"
#include "filemanager/window/qtwindowworkspace.h"
#include "filemanager/window/qtworkspaceapplication.h"
#include "filemanager/window/qtworkspacetheme.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "localization/qtlanguageservices.h"
#include "platform/associations/qtassociatedfileservices.h"
#include "platform/locale/qtregionalsettings.h"
#include "platform/trash/qttrashservice.h"
#include "ui/controls/qtwindowstatusbar.h"

MainWindow::MainWindow(ArchiveBackend *backend, std::function<bool(const QUrl &)> fileOpener,
                       IQtTrashService *trashService, const QtFileManagerStartup *startup,
                       DeviceSelectionServices deviceServices)
    : deviceServices_(std::move(deviceServices)), fileOpener_(std::move(fileOpener)) {
    if (trashService)
        trashService_ = trashService;
    else {
        nativeTrashService_ = std::make_unique<QtNativeTrashService>();
        trashService_ = nativeTrashService_.get();
    }
    backend_ = backend ? backend : new ArchiveBackend(this);
    ReloadLang(toolbarSettingsPath());
    setWindowTitle("Sevenzip");
    resize(980, 620);
    statusStrip_ = new QtWindowStatusBar(this);
    setStatusBar(statusStrip_);
    QtWorkspaceTheme::statusBar(*statusStrip_);
    folderManager_ = new QtArchiveFolderManager(*backend_);
    associatedFileServices_ = std::make_unique<QtAssociatedFileServices>(
        fileOpener_,
        [this](CPanel &panel, const QtArchiveUpdateBody &body) {
            return runExternalArchiveUpdate(panel, body);
        },
        [this] { return canHandleExternalUpdate(); });
    associatedFileServices_->SetExternalSessionFactory(
        [this](CPanel &source, const CTempFileInfo &file, const QString &settings,
               const QString &prefix) {
            auto owner = QtExternalFileTask::Create(source, file, *backend_, settings, prefix);
            return owner;
        });
    for (auto &panel : app_.Panels) {
        panel.AttachFolderManager(folderManager_, toolbarSettingsPath());
        panel.AttachExternalFileServices(associatedFileServices_.get());
    }
    QSettings viewSettings(toolbarSettingsPath(), QSettings::IniFormat);
    const int timestampLevel =
        viewSettings.value(QStringLiteral("view/timestampLevel"), kTimestampPrintLevel_MIN).toInt();
    for (const int valid :
         {kTimestampPrintLevel_DAY, kTimestampPrintLevel_MIN, kTimestampPrintLevel_SEC,
          kTimestampPrintLevel_NTFS, kTimestampPrintLevel_NS}) {
        if (timestampLevel == valid)
            app_.SetTimestampLevel(timestampLevel);
    }
    g_Timestamp_Show_UTC = viewSettings.value(QStringLiteral("view/timestampUTC"), false).toBool();
    app_.AutoRefresh_Mode = viewSettings.value(QStringLiteral("view/autoRefresh"), true).toBool();
    app_.SetPanels_AutoRefresh_Mode();
    workspace_ = std::make_unique<QtWindowWorkspace>(*this);
    clipboard_ = std::make_unique<QtFileClipboard>(*this);
    initializeFileManagerWindow(startup);
    QtRegionalSettings::Observe(*this, [this] { refreshActions(); });
    connect(backend_, &ArchiveBackend::codecCatalogReady, this,
            [this](bool, const QString &message) {
                refreshActions();
                if (!message.isEmpty())
                    showStatusMessage(message);
            });
    if (!backend) {
        backend_->discoverCodecCatalog();
    }
    if (auto *application = QtWorkspaceApplication::Find())
        application->Register(*this);
}

void MainWindow::createFileManagerWindow(const QtFileManagerStartup *startup, bool panelsDefined,
                                         UInt32 splitterPos) {
    const unsigned focusedPanel = app_.LastFocusedPanel;
    statusPanel_ = focusedPanel;
    renameController_ = std::make_unique<QtRenameController>(*this);
    guiOutputRefresh_ = std::make_unique<QtGuiOutputRefresh>(
        [this] {
            const auto &operations = app_.QtOperations();
            return !operations.closed() && !operations.active() && !operations.callActive() &&
                   workspace_->CanRefreshPublishedArchives();
        },
        [this](unsigned index, const QtGuiOutputRefresh::Outputs &output) {
            refreshGuiOutputPanel(index, output);
        });
    appPresentation_ = std::make_unique<QtAppPresentation>(*this);
    connectFilesystemScanner();
    COpenResult opened;
    int widths[2] = {width() / 2, width() - width() / 2};
    const UString initialPath = startup         ? startup->FullPath
                                : panelsDefined ? UString()
                                                : ToUString(QDir::currentPath());
    const HRESULT result = app_.Create(this, initialPath, startup ? startup->ArcFormat : UString(),
                                       widths, startup && startup->NeedOpenArc, opened,
                                       *appPresentation_, toolbarSettingsPath());
    if (startup)
        CheckFileManagerStartup(result, *startup, opened);
    if (result != S_OK)
        throw result;
    commands_ = std::make_unique<QtFileManagerCommands>(*this);
    commands_->AttachActions();
    configureShortcuts();
    app_.QtOperations().SetCallFinishedCallback([this] {
        // Availability can be published while a synchronous source caller
        // still holds its close guard. Restore it after that caller unwinds.
        QMetaObject::invokeMethod(
            this,
            [this] {
                if (!app_.QtOperations().closed()) {
                    commands_->RefreshActions();
                    workspace_->Refresh();
                }
            },
            Qt::QueuedConnection);
    });
    programFileMenu_ = std::make_unique<QtProgramFileMenu>(*this);
    drag_ = std::make_unique<QtFileManagerDrag>(*this);
    installEventFilter(this);
    if (focusedPanel != projectedPanel_)
        activatePanelUi(focusedPanel);
    app_.SetFocusedPanel(focusedPanel);
    statusPanel_ = focusedPanel;
    appPresentation_->RequestInitialFocus(focusedPanel);
    if (layout())
        layout()->activate();
    workspace_->Refresh();
    panelSplitter_->Initialize(panelsDefined, splitterPos);
    QTimer::singleShot(0, this, [this] {
        appPresentation_->CompleteInitialFocusIfReady();
        panelSplitter_->UpdateSourceLayout();
    });
    autoRefreshTimer_ = new QTimer(this);
    autoRefreshTimer_->setInterval(1000);
    connect(autoRefreshTimer_, &QTimer::timeout, this, [this] { pollAutoRefresh(); });
    autoRefreshTimer_->start();
    refreshActions();
    connectBackend();
    reloadLangItems();
}

void MainWindow::cancelListPublication() {
    projectedUi().filesystemScanner->cancel();
    projectedUi().listProjection->cancel();
    projectedUi().scanningDisk = false;
    projectedUi().scanningArchive = false;
    panelUi_[projectedPanel_].pendingSelection.reset();
    panelUi_[projectedPanel_].pendingViewport.reset();
    if (workspace_)
        workspace_->NavigationCanceled();
}
