#include "7zip/PropID.h"
#include "7zip/UI/FileManager/resourceGui.h"
#include "7zip/UI/GUI/ExtractRes.h"
#include "Common/StringConvert.h"
#include "Windows/PropVariant.h"
#include "archive/password/archivepassword.h"
#include "archive/resources/archiveinputpath.h"
#include "archive/resources/qtarchivefileresource.h"
#include "dialogs/messages/MessagesDialog.h"
#include "filemanager/FormatUtils.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtapppresentation.h"
#include "localization/qtlangcontrols.h"
#include "ui/progress/ProgressDialog2.h"
#include <QScopeGuard>
#include <QScopedValueRollback>

bool MainWindow::canHandleExternalUpdate() const {
    return !app_.QtOperations().active() && !app_.QtOperations().closed() &&
           !sourceArchiveProgress_ && !backend_->busy() && !QApplication::activeModalWidget();
}

bool MainWindow::runExternalArchiveUpdate(CPanel &panel, const QtArchiveUpdateBody &body) {
    if (!canHandleExternalUpdate())
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
    if (app_.QtOperations().closed())
        return E_ABORT;
    appPresentation_->CancelInitialFocus();
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
    if (!IsArchiveFileOrPackage(file) || file.isSymLink()) {
        statusBar()->showMessage(
            QCoreApplication::translate("mainwindow_archiveopening", "File not found: ") + path);
        return E_INVALIDARG;
    }
    cancelListPublication();
    if (projectedUi().state == State::Filesystem)
        projectedUi().returnDirectory = projectedUi().directory;
    auto &panel = app_.GetFocusedPanel();
    const UString fallback = projectedUi().state == State::Namespace
                                 ? GetFolderPath(panel.GetFolder())
                                 : ToUString(projectedUi().returnDirectory);
    QString message;
    HRESULT result;
    {
        const auto completion = connect(backend_, &ArchiveBackend::finished, this,
                                        [&message](bool, const QString &text) { message = text; });
        const auto disconnectCompletion = qScopeGuard([&] { disconnect(completion); });
        setState(State::Loading);
        QScopedValueRollback<bool> source(sourceArchiveProgress_, true);
        COpenResult opened;
        result = panel.BindToPath(ToUString(file.absoluteFilePath()), ToUString(type), opened);
        if (!opened.ArchiveIsOpened && !opened.ErrorMessage.IsEmpty())
            message = ToQString(opened.ErrorMessage);
        // BindToPath can successfully bind the containing filesystem folder
        // after its archive probe returns S_FALSE. An archive-only request
        // must retain that refusal rather than report "Archive opened".
        if (result == S_OK && !opened.ArchiveIsOpened) {
            result = S_FALSE;
            if (message.isEmpty())
                message = ToQString(MyFormatNew(opened.Encrypted ? IDS_CANT_OPEN_ENCRYPTED_ARCHIVE
                                                                 : IDS_CANT_OPEN_ARCHIVE,
                                                ToUString(file.absoluteFilePath())));
        }
    }
    if (!panel.IsArcFolder()) {
        COpenResult restored;
        panel.BindToPath(fallback, UString(), restored);
    }
    renderBoundFolder();
    if (result == E_ABORT)
        message = QCoreApplication::translate("mainwindow_archiveopening", "Operation canceled.");
    else if (message.isEmpty())
        message = result == S_OK
                      ? QCoreApplication::translate("mainwindow_archiveopening", "Archive opened.")
                      : ToQString(HResultToMessage(result));
    statusBar()->showMessage(message);
    if (result != S_OK && result != E_ABORT)
        QMessageBox::warning(this, QStringLiteral("Sevenzip"), message);
    return panel.IsArcFolder() ? result : result == S_OK ? S_FALSE : result;
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

void MainWindow::goBack() {
    if (projectedUi().state == State::Loading || projectedUi().state == State::Operating)
        return;
    if (projectedUi().state == State::Viewing ||
        (projectedUi().state == State::Namespace && !app_.GetFocusedPanel().IsRootFolder()))
        goParent();
    else if (!projectedUi().history.isEmpty()) {
        const QString previous = projectedUi().history.takeLast();
        if (previous.isEmpty())
            goRoot(false);
        else
            navigateTo(previous, true);
    }
}

void MainWindow::goParent() {
    if (app_.QtOperations().closed())
        return;
    QtAppOperationState::CallScope call(app_.QtOperations());
    auto &panel = app_.GetFocusedPanel();
    if (projectedUi().state != State::Viewing && projectedUi().state != State::Filesystem &&
        (projectedUi().state != State::Namespace || panel.IsRootFolder()))
        return;
    if (!panel.GetFolder())
        return;
    const QPointer<QWidget> focused = focusWidget();
    const bool restoreListFocus =
        focused &&
        (focused == projectedUi().table || projectedUi().table->isAncestorOf(focused) ||
         focused == projectedUi().iconList || projectedUi().iconList->isAncestorOf(focused));
    const unsigned panelIndex = app_.LastFocusedPanel;
    setState(State::Operating);
    {
        QScopedValueRollback<bool> source(sourceArchiveProgress_, true);
        panel.OpenParentFolder();
    }
    renderBoundFolder();
    // Win32 keeps list focus while Parent refreshes its rows. Qt can clear it
    // when disabling that view; use the existing publication/input guard.
    if (restoreListFocus)
        appPresentation_->RequestListFocus(panelIndex);
    else
        appPresentation_->RestoreWidgetFocus(focused);
    if (panel.IsRootFolder() && !projectedUi().history.isEmpty() &&
        projectedUi().history.last().isEmpty())
        projectedUi().history.removeLast(); // Parent consumed the virtual-root Back location.
}

QString MainWindow::archiveDisplayPath() const {
    if (projectedUi().displayArchive.isEmpty())
        return projectedUi().archiveFolder;
    return projectedUi().displayArchive +
           (projectedUi().archiveFolder.isEmpty()
                ? QString()
                : QStringLiteral(" / ") + projectedUi().archiveFolder);
}
