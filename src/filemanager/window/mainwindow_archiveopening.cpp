#include "7zip/PropID.h"
#include "7zip/UI/FileManager/resourceGui.h"
#include "7zip/UI/GUI/ExtractRes.h"
#include "Common/StringConvert.h"
#include "Windows/PropVariant.h"
#include "archive/password/archivepassword.h"
#include "archive/resources/archiveinputpath.h"
#include "archive/resources/qtarchivefileresource.h"
#include "filemanager/FormatUtils.h"
#include "filemanager/panels/qtpanellocationtransaction.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtapppresentation.h"
#include "filemanager/window/qtwindowworkspace.h"
#include "localization/qtlangcontrols.h"
#include "ui/progress/ProgressDialog2.h"
#include <QScopeGuard>
#include <QScopedValueRollback>
#include <QScrollBar>

bool MainWindow::canHandleExternalUpdate() const {
    return !app_.QtOperations().active() && !app_.QtOperations().closed() &&
           !sourceArchiveProgress_ && !backend_->busy() && !QApplication::activeModalWidget();
}

bool MainWindow::runExternalArchiveUpdate(CPanel &panel, const QtArchiveUpdateBody &body) {
    if (!canHandleExternalUpdate())
        return false;
    if (&app_.Panels[0] != &panel && &app_.Panels[1] != &panel)
        return false;
    QtAppOperationState::CallScope call(app_.QtOperations());
    const QPointer<QWidget> focused = focusWidget();
    std::optional<CSelectedState> selection;
    bool result;
    {
        QScopedValueRollback<bool> owner(sourceArchiveProgress_, true);
        app_.QtOperations().Begin(QtAppOperationState::Kind::Operation);
        const auto finish = qScopeGuard([&] { app_.QtOperations().End(); });
        result = body(selection);
    }
    if (selection)
        panel.RefreshListCtrl(*selection);
    refreshActions();
    // Qt can clear the window's remembered focus when its address control is
    // disabled during modal progress. Restore it after terminal publication.
    appPresentation_->RestoreWidgetFocus(focused);
    return result;
}

void MainWindow::openArchive(const QString &path) { openArchiveAs(path, {}); }

HRESULT MainWindow::openArchiveAs(const QString &path, const QString &type) {
    return workspace_->Navigate([&] { return openArchiveAsImpl(path, type); });
}

HRESULT MainWindow::openArchiveAsImpl(const QString &path, const QString &type) {
    if (app_.QtOperations().closed())
        return E_ABORT;
    workspace_->CancelListFocus();
    const QPointer<QWidget> focused = focusWidget();
    const auto restoreFocus = qScopeGuard([this, focused] {
        // Modal archive progress can clear Qt's remembered address focus.
        // Restore the selected control after the operation call scope ends.
        appPresentation_->RestoreWidgetFocus(focused);
    });
    QtAppOperationState::CallScope call(app_.QtOperations());
    if (projectedUi().state == State::Loading || projectedUi().state == State::Operating)
        return E_ABORT;
    const QFileInfo file(path);
    if (!IsArchiveFileOrPackage(file)) {
        showStatusMessage(
            QCoreApplication::translate("mainwindow_archiveopening", "File not found: ") + path);
        return E_INVALIDARG;
    }
    const auto pendingSelection = projectedUi().pendingSelection;
    const auto pendingViewport = projectedUi().pendingViewport;
    cancelListPublication();
    if (projectedUi().state == State::Filesystem)
        projectedUi().returnDirectory = projectedUi().directory;
    auto &panel = app_.GetFocusedPanel();
    CSelectedState previousSelection;
    if (pendingSelection)
        previousSelection = *pendingSelection;
    else
        panel.SaveSelectedState(previousSelection);
    const QPointer<QAbstractItemView> previousView =
        qobject_cast<QAbstractItemView *>(projectedUi().viewStack->currentWidget());
    QPoint previousScroll;
    if (pendingViewport && pendingViewport->view == previousView &&
        pendingViewport->directory == projectedUi().directory)
        previousScroll = pendingViewport->scrollPosition;
    else if (previousView)
        previousScroll = {previousView->horizontalScrollBar()->value(),
                          previousView->verticalScrollBar()->value()};
    QString message;
    HRESULT result;
    const PROPID previousSort = panel.GetSortID();
    const bool previousAscending = panel._ascending;
    const UInt32 previousMode = panel.GetListViewMode();
    {
        result = tryOpenPhysicalArchive(file, type, message);
        if (result == S_OK) {
            setState(State::Viewing);
        } else {
            panel._sortID = previousSort;
            panel._ascending = previousAscending;
            panel.SetListViewMode(previousMode);
            renderBoundFolder();
            restoreListSelection(previousSelection);
        }
    }
    if (result == E_ABORT)
        message = QCoreApplication::translate("mainwindow_archiveopening", "Operation canceled.");
    else if (message.isEmpty() && result != S_OK)
        message = ToQString(HResultToMessage(result));
    showStatusMessage(message);
    if (result != S_OK && result != E_ABORT) {
        QMessageBox warning(QMessageBox::Warning, QStringLiteral("Sevenzip"), message,
                            QMessageBox::Ok, this);
        warning.setTextFormat(Qt::PlainText);
        warning.exec();
    }
    // Acknowledging the error is part of this transition. Only later input
    // should supersede the restored source's scroll position.
    if (result != S_OK && previousView)
        workspace_->RestoreViewPosition(panel, *previousView, previousScroll);
    return panel.IsArcFolder() ? result : result == S_OK ? S_FALSE : result;
}

HRESULT MainWindow::tryOpenPhysicalArchive(const QFileInfo &file, const QString &type,
                                           QString &message) {
    const QScopedValueRollback<bool> pending(archiveLocationPending_, true);
    const auto completion = connect(backend_, &ArchiveBackend::finished, this,
                                    [&message](bool ok, const QString &text) {
                                        if (!ok)
                                            message = text;
                                    });
    const auto disconnectCompletion = qScopeGuard([&] { disconnect(completion); });
    setState(State::Loading);
    QScopedValueRollback<bool> source(sourceArchiveProgress_, true);
    auto &panel = app_.GetFocusedPanel();
    QtPanelLocationTransaction location(panel);
    // Projection retains the candidate provider. Release it before rollback
    // closes candidate libraries, including when binding or rendering throws.
    const auto cancelProjection = qScopeGuard([this] { cancelListPublication(); });
    COpenResult opened;
    HRESULT result = panel.BindToPath(ToUString(file.absoluteFilePath()), ToUString(type), opened);
    if (!opened.ArchiveIsOpened && !opened.ErrorMessage.IsEmpty())
        message = ToQString(opened.ErrorMessage);
    // A successful native fallback to the containing folder is still a refusal
    // of this archive-only request.
    if (result == S_OK && !opened.ArchiveIsOpened) {
        result = S_FALSE;
        if (message.isEmpty())
            message = ToQString(MyFormatNew(opened.Encrypted ? IDS_CANT_OPEN_ENCRYPTED_ARCHIVE
                                                             : IDS_CANT_OPEN_ARCHIVE,
                                            ToUString(file.absoluteFilePath())));
    }
    if (result == S_OK) {
        result = renderBoundFolder();
        if (result == S_OK && projectedUi().scanningArchive) {
            setState(State::Loading);
            result = projectedUi().listProjection->waitForFinished();
        }
        if (result != S_OK)
            message = ToQString(HResultToMessage(result));
    }
    setState(State::Loading);
    if (result == S_OK)
        location.Commit();
    else {
        cancelListPublication();
        location.Rollback();
    }
    return result;
}

HRESULT MainWindow::renderBoundFolder(bool updateAddress) {
    if (app_.QtOperations().closed())
        return E_ABORT;
    QtAppOperationState::CallScope call(app_.QtOperations());
    auto &panel = app_.GetFocusedPanel();
    const auto selection = panel.TakeRefreshSelection();
    panel.LoadFullPath();
    if (panel.IsRootFolder() || panel.IsFSDrivesFolder() || panel.IsNetFolder() ||
        panel.IsAltStreamsFolder()) {
        const HRESULT loaded = renderRootFolder(updateAddress);
        if (loaded != S_OK)
            return loaded;
        if (selection) {
            panel.RestoreLoadedSelection(*selection);
            refreshActions();
        }
        return S_OK;
    }
    if (!panel.IsArcFolder()) {
        const QString directory = panel.GetFolder()
                                      ? QDir::cleanPath(ToQString(GetFolderPath(panel.GetFolder())))
                                      : projectedUi().returnDirectory;
        const HRESULT loaded =
            showDirectory(directory.isEmpty() ? projectedUi().returnDirectory : directory, false,
                          panel.GetFolder() && panel.IsFSFolder(), updateAddress);
        if (loaded == S_OK && selection)
            restoreListSelection(*selection);
        refreshActions();
        return loaded;
    }
    auto *folder = dynamic_cast<CAgentFolder *>(panel.GetFolder());
    if (!folder)
        return E_NOTIMPL;
    auto *resource = dynamic_cast<QtArchiveFileResource *>(folder->GetEngineResource());
    if (!resource)
        return E_NOTIMPL;
    projectedUi().archive = resource->EnginePath;
    const auto &parents = panel.GetParentFolders();
    projectedUi().archivePassword = parents.IsEmpty() ? ArchivePassword() : parents.Back().Password;
    projectedUi().archiveFolder = QDir::fromNativeSeparators(ToQString(GetFolderPath(folder)));
    if (projectedUi().archiveFolder.endsWith('/'))
        projectedUi().archiveFolder.chop(1);
    projectedUi().displayArchive =
        QDir::fromNativeSeparators(ToQString(panel.GetCurrentFolderPrefix()));
    projectedUi().displayArchive.chop(
        projectedUi().archiveFolder.size() +
        (projectedUi().archiveFolder.isEmpty() || projectedUi().archiveFolder.endsWith(':') ? 1
                                                                                            : 2));
    // Keep operation guards until rows and native selection agree. The enum
    // here selects Qt presentation; the app owner still blocks commands.
    projectedUi().state = State::Viewing;
    const HRESULT loaded = renderArchiveFolder(updateAddress);
    if (loaded == S_OK && selection)
        restoreListSelection(*selection);
    setState(State::Viewing);
    return loaded;
}

void MainWindow::goBack() { workspace_->Travel(-1); }

void MainWindow::goParent() {
    workspace_->Navigate([this] { return goParentImpl(); });
}

HRESULT MainWindow::goParentImpl() {
    if (app_.QtOperations().closed())
        return E_ABORT;
    QtAppOperationState::CallScope call(app_.QtOperations());
    auto &panel = app_.GetFocusedPanel();
    if (projectedUi().state != State::Viewing && projectedUi().state != State::Filesystem &&
        (projectedUi().state != State::Namespace || panel.IsRootFolder()))
        return S_FALSE;
    if (!panel.GetFolder())
        return S_FALSE;
    const QPointer<QWidget> focused = focusWidget();
    const bool restoreListFocus =
        focused &&
        (focused == projectedUi().table || projectedUi().table->isAncestorOf(focused) ||
         focused == projectedUi().iconList || projectedUi().iconList->isAncestorOf(focused));
    const unsigned panelIndex = app_.LastFocusedPanel;
    if (!panel.IsArcFolder() || !GetFolderPath(panel.GetFolder()).IsEmpty())
        workspace_->RetainNavigationFolder();
    const State previousState = projectedUi().state;
    setState(State::Operating);
    HRESULT result;
    {
        QScopedValueRollback<bool> source(sourceArchiveProgress_, true);
        result = panel.OpenParentFolder();
    }
    if (result == E_ABORT)
        setState(previousState);
    else if (const auto published = workspace_->NavigationPublication())
        result = *published;
    else if (!projectedUi().scanningDisk && !projectedUi().scanningArchive)
        result = renderBoundFolder();
    // Win32 keeps list focus while Parent refreshes its rows. Qt can clear it
    // when disabling that view; use the existing publication/input guard.
    if (restoreListFocus)
        workspace_->RequestListFocus(panelIndex);
    else
        appPresentation_->RestoreWidgetFocus(focused);
    return result;
}

QString MainWindow::archiveDisplayPath() const {
    if (projectedUi().displayArchive.isEmpty())
        return projectedUi().archiveFolder;
    return projectedUi().displayArchive +
           (projectedUi().archiveFolder.isEmpty()
                ? QString()
                : QStringLiteral(" / ") + projectedUi().archiveFolder);
}
