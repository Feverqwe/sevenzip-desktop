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
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "localization/qtlanguageservices.h"
#include "platform/associations/qtassociatedfileservices.h"
#include "platform/trash/qttrashservice.h"

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
    folderManager_ = new QtArchiveFolderManager(*backend_);
    associatedFileServices_ = std::make_unique<QtAssociatedFileServices>(
        fileOpener_,
        [this](CPanel &panel, const QtArchiveUpdateBody &body) {
            return runExternalArchiveUpdate(panel, body);
        },
        [this] { return canHandleExternalUpdate(); });
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
    initializeFileManagerWindow(startup);
    connect(backend_, &ArchiveBackend::codecCatalogReady, this,
            [this](bool, const QString &message) {
                refreshActions();
                if (!message.isEmpty())
                    statusBar()->showMessage(message);
            });
    if (!backend) {
        backend_->discoverCodecCatalog();
    }
}

void MainWindow::createFileManagerWindow(const QtFileManagerStartup *startup, bool panelsDefined,
                                         UInt32 splitterPos) {
    const unsigned focusedPanel = app_.LastFocusedPanel;
    renameController_ = std::make_unique<QtRenameController>(*this);
    guiOutputRefresh_ = std::make_unique<QtGuiOutputRefresh>(
        [this] {
            const auto &operations = app_.QtOperations();
            return !operations.closed() && !operations.active() && !operations.callActive();
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
        if (!exitAction_->isEnabled())
            QMetaObject::invokeMethod(
                this, [this] { commands_->RefreshActions(); }, Qt::QueuedConnection);
    });
    programFileMenu_ = std::make_unique<QtProgramFileMenu>(*this);
    drag_ = std::make_unique<QtFileManagerDrag>(*this);
    for (unsigned index = 0; index < kNumPanelsMax; ++index)
        app_.Panels[index].AttachInvalidationPresentation([this, index] {
            if (index == app_.GetFocusedPanelIndex())
                redrawSourceProperties();
        });
    installEventFilter(this);
    if (focusedPanel != projectedPanel_)
        activatePanelUi(focusedPanel);
    app_.SetFocusedPanel(focusedPanel);
    appPresentation_->RequestInitialFocus(focusedPanel);
    if (layout())
        layout()->activate();
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
    filesystemScanner_->cancel();
    listProjection_->cancel();
    scanningDisk_ = false;
    scanningArchive_ = false;
    panelUi_[projectedPanel_].pendingSelection.reset();
    panelUi_[projectedPanel_].pendingViewport.reset();
}
