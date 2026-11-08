#include "filemanager/window/qtapppresentation.h"
#include "filemanager/qtfilemanagerdrag.h"
#include "filemanager/views/qtworkspacefiletable.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtfileclipboard.h"
#include "filemanager/window/qtnavigationbar.h"
#include "filemanager/window/qtwindowworkspace.h"
#include "filemanager/window/qtworkspaceapplication.h"
#include "fileops/native/Dialog.h"
#include <QApplication>
#include <QPointer>

void QtAppPresentation::Initialize(CApp &app) {
    // Preserve the existing single-panel preferences when introducing the
    // original per-panel keys. Explicit source keys always take precedence.
    QSettings key(window_.toolbarSettingsPath(), QSettings::IniFormat);
    if (!key.contains("ListMode")) {
        CListMode modes;
        bool ok = false;
        const int mode = key.value("view/mode", 3).toInt(&ok);
        if (ok && mode == 0)
            modes.Panels[app.LastFocusedPanel < kNumPanelsMax ? app.LastFocusedPanel : 0] = 0;
        modes.Save(window_.toolbarSettingsPath());
    }
    editMenu_ = window_.createMenus();
}

bool QtAppPresentation::CreatePanel(CPanel &panel, unsigned index) {
    if (index >= kNumPanelsMax || window_.panelUi_[index].container)
        return false;
    const unsigned previous = window_.projectedPanel_;
    const bool hadPrevious = window_.panelUi_[previous].container;
    window_.projectedPanel_ = index;
    panel.AttachColumnDefaultsPresentation(QtWorkspaceFileTable::DefaultColumns);
    window_.app_.SetFocusedPanel(index);
    if (!window_.panelUi_[index].filesystemScanner)
        window_.connectFilesystemScanner(index);
    panel.LoadFullPath();
    panel.AttachViewModePresentation([window = QPointer<MainWindow>(&window_), index](UInt32 mode) {
        if (window)
            window->withPanelProjection(index, [window, mode] { window->setViewMode(int(mode)); });
    });
    window_.createFilePanel(editMenu_, index);
    if (!panel.PanelCreated) {
        panel._showDots = window_.fileListSettings_.ShowDots;
        SetListSettings(index, window_.fileListSettings_);
    }
    panel.AttachAddressFocus(
        [navigation = window_.panelUi_[index].navigation] { navigation->Edit(); });
    panel.AttachFileClipboard([window = QPointer<MainWindow>(&window_), index](unsigned command) {
        if (!window)
            return;
        window->withPanelProjection(index, [window, command] {
            if (command == 2)
                window->clipboard_->Paste();
            else
                window->clipboard_->Copy(command == 0);
        });
    });
    panel.AttachInvalidationPresentation(
        [window = QPointer<MainWindow>(&window_), source = &panel, index] {
            if (!window)
                return;
            if (&window->app_.Panels[index] == source)
                window->withPanelProjection(index, [window] { window->redrawSourceProperties(); });
        });
    panel.AttachStatusPresentation([window = QPointer<MainWindow>(&window_), source = &panel,
                                    index](const CPanelStatus &status) {
        if (window && &window->app_.Panels[index] == source)
            window->showPanelStatus(index, status);
    });
    // OnCreate refreshes immediately after this service returns, before the
    // next panel is bound. Install its load/publication owner with its controls.
    panel.AttachReloadPresentation([window = QPointer<MainWindow>(&window_), source = &panel,
                                    index](const CSelectedState &selection) {
        if (!window)
            return E_ABORT;
        if (&window->app_.Panels[index] != source)
            return E_ABORT;
        return window->reloadPanelPresentation(index, selection);
    });
    panel.AttachArchivePublication(
        [window = QPointer<MainWindow>(&window_)](const QString &archive, const CPanel *updated) {
            if (auto *application = QtWorkspaceApplication::Find())
                application->ArchivePublished(archive, updated);
            else if (window)
                window->ArchivePublished(archive, updated);
        });
    if (window_.drag_)
        window_.drag_->AttachPanel(index);
    if (hadPrevious && index != previous) {
        window_.projectedPanel_ = previous;
        window_.app_.SetFocusedPanel(previous);
    }
    return true;
}

void QtAppPresentation::RestoreWidgetFocus(QWidget *widget) {
    if (!window_.app_.QtOperations().closed() && widget && widget->isVisible() &&
        widget->isEnabled() && !QApplication::activeModalWidget())
        widget->setFocus(Qt::OtherFocusReason);
}

CPanelCallback *QtAppPresentation::PanelCallback(unsigned) {
    // Source callbacks, including folder synchronization, belong to
    // CApp::m_PanelCallbackImp. This service supplies Qt presentation only.
    return nullptr;
}

bool QtAppPresentation::AllowPanelFocusChange(unsigned index) const {
    // Native focus can return to the visible pane during a modal source call.
    return !window_.panelProjectionDepth_ || window_.projectedPanel_ == index;
}

bool QtAppPresentation::AllowPanelTitleChange(unsigned index) const {
    return index == window_.statusPanel_;
}

void QtAppPresentation::PrepareFolderChange(unsigned index) {
    window_.withPanelProjection(index, [this] {
        window_.cancelListPublication();
        window_.setState(window_.projectedUi().state);
    });
}

bool QtAppPresentation::OpenFolderInPeer(unsigned index, std::optional<UInt32> item) {
    window_.workspace_->OpenFolderInPeer(index, item);
    return true;
}

bool QtAppPresentation::OpenBoundFolderInPeer(unsigned source, IFolderFolder *folder,
                                              const UString &fallbackPath) {
    const unsigned peer = 1 - source;
    window_.withPanelProjection(peer, [&] {
        window_.workspace_->Navigate([&] {
            if (!folder)
                return window_.navigateTo(ToQString(fallbackPath), true);
            window_.workspace_->ProtectNavigation();
            auto &destination = window_.app_.Panels[peer];
            destination.CloseOpenFolders();
            destination.SetNewFolder(folder);
            window_.projectedUi().locationUnavailable = false;
            return window_.renderBoundFolder();
        });
    });
    return true;
}

void QtAppPresentation::SetListSettings(unsigned index, const CFmSettings &settings) {
    if (index >= kNumPanelsMax)
        return;
    window_.fileListSettings_ = settings;
}

void QtAppPresentation::ShowPanel(unsigned index, bool visible) {
    if (index < kNumPanelsMax && window_.panelUi_[index].container)
        window_.panelUi_[index].container->setVisible(visible);
}
void QtAppPresentation::MoveSubWindows() {
    if (window_.layout())
        window_.layout()->activate();
    if (window_.panelSplitter_)
        window_.panelSplitter_->UpdateSourceLayout();
}
