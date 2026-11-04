#include "7zip/PropID.h"
#include "7zip/UI/FileManager/resource.h"
#include "7zip/UI/FileManager/resourceGui.h"
#include "7zip/UI/GUI/CompressDialogRes.h"
#include "7zip/UI/GUI/ExtractRes.h"
#include "Windows/PropVariant.h"
#include "archive/hashing/HashCalc.h"
#include "archive/resources/qtarchivefileresource.h"
#include "archive/resources/qtarchiveproperties.h"
#include "dialogs/messages/MessagesDialog.h"
#include "filemanager/window/mainwindow.h"
#include "gui/UpdateGUI.h"
#include "gui/resource2.h"
#include "localization/qtlangcontrols.h"
#include "platform/shell/qtshellarchiveservices.h"
#include <QScopeGuard>
#include <QScopedValueRollback>

void MainWindow::calculateHash(const QString &method) {
    if (app_.QtOperations().closed())
        return;
    QtAppOperationState::CallScope call(app_.QtOperations());
    const CPanel &panel = app_.GetFocusedPanel();
    const bool filesystemNamespace = projectedUi().state == State::Namespace &&
                                     panel.Is_IO_FS_Folder() && panel.DoesItSupportOperations();
    if ((projectedUi().state != State::Filesystem && projectedUi().state != State::Viewing &&
         !filesystemNamespace) ||
        sourceOperatedIndices().IsEmpty())
        return;
    if (canLaunchGuiFilesystemOperation() && !panel.GetFsPath().IsEmpty()) {
        const auto indices = sourceOperatedIndices();
        UStringVector paths;
        FOR_VECTOR(i, indices)
        paths.Add(panel.GetItemRelPath(indices[i]));
        launchGuiHash(paths, ToUString(method), panel.GetFsPath(), !panel.GetFlatMode());
        return;
    }
    const auto previousState = projectedUi().state;
    setState(State::Operating);
    HRESULT result;
    {
        // The immutable catalog must outlive its borrowed hash-provider interfaces.
        const auto catalog = backend_->codecCatalog();
        CExternalCodecs providers;
        QScopedValueRollback<bool> progressOwner(sourceArchiveProgress_, true);
        result = backend_->loadHashProviders(providers);
        if (result == S_OK)
            result = app_.CalculateCrc2(ToUString(method), &providers);
    }
    if (!app_.QtOperations().closed()) {
        setState(previousState);
        if (result != S_OK && result != E_ABORT)
            app_.GetFocusedPanel().MessageBox_Error_HRESULT(result);
    }
}

void MainWindow::extractArchives() { runArchiveBatchCommand(false); }

void MainWindow::runArchiveBatchCommand(bool test) {
    if (app_.QtOperations().closed() || app_.QtOperations().active())
        return;
    QtAppOperationState::CallScope call(app_.QtOperations());
    auto &panel = app_.GetFocusedPanel();
    QtShellArchiveServices::TestSink independentTest;
    QtShellArchiveServices::ExtractSink independentExtract;
    if (test && canLaunchGuiFilesystemOperation())
        independentTest = [this](const UStringVector &paths, bool hashMode) {
            return launchGuiArchiveTest(paths, hashMode);
        };
    if (!test && canLaunchGuiFilesystemOperation())
        independentExtract = [this](const UStringVector &paths, const UString &folder,
                                    bool showDialog, bool elimDup, UInt32 zone) {
            return launchGuiArchiveExtract(paths, folder, showDialog, elimDup, zone);
        };
    QtShellArchiveServices services(
        *backend_, this,
        {.Test = std::move(independentTest), .Extract = std::move(independentExtract)},
        toolbarSettingsPath());
    if (!test && !panel.GetParentFolders().IsEmpty()) {
        app_.ExtractArchives(services); // Original archive branch delegates to OnCopy.
        return;
    }
    const auto previous = projectedUi().state;
    const unsigned source = app_.GetFocusedPanelIndex();
    const QPointer<QWidget> focus = focusWidget();
    const auto restore = qScopeGuard([&] {
        if (!app_.QtOperations().closed()) {
            activatePanelUi(source);
            setState(previous);
            activateWindow();
            if (focus && focus->isEnabled())
                focus->setFocus();
        }
    });
    setState(State::Operating);
    QScopedValueRollback<bool> progressOwner(sourceArchiveProgress_, true);
    if (test)
        app_.TestArchives(&services);
    else
        app_.ExtractArchives(services);
}

void MainWindow::createDialog() {
    if (app_.QtOperations().closed())
        return;
    const QPointer<QWidget> focus = focusWidget();
    const auto restoreFocus = qScopeGuard([&] {
        if (!app_.QtOperations().closed() && focus && focus->isVisible() && focus->isEnabled())
            focus->setFocus(Qt::OtherFocusReason);
    });
    QtAppOperationState::CallScope call(app_.QtOperations());
    if (projectedUi().state != State::Filesystem && projectedUi().state != State::Viewing)
        return;
    const auto previousState = projectedUi().state;
    const unsigned sourceIndex = app_.GetFocusedPanelIndex();
    CPanel &panel = app_.GetFocusedPanel();
    CSelectedState selection;
    panel.SaveSelectedState(selection);
    bool dialogShown = false;
    const auto catalog = backend_->codecCatalog();
    QtShellArchiveServices::CompressSink independent;
    if (canLaunchGuiFilesystemOperation())
        independent = [this](const UString &folder, const UString &name, const UString &type,
                             bool addExtension, const UStringVector &paths, bool email,
                             bool showDialog, bool /* waitFinish */) {
            return launchGuiArchiveCompress(folder, name, type, addExtension, paths, email,
                                            showDialog);
        };
    QtShellArchiveServices services(
        *backend_, this,
        {.Compress = std::move(independent),
         .Modal =
             [&](CCompressDialog &source, const CObjectVector<NWildcard::CCensorPath> &paths,
                 QWidget *parent) {
                 dialogShown = true;
                 return CreateArchiveDialog::PresentSource(source, paths, catalog, parent);
             }},
        toolbarSettingsPath());
    setState(State::Operating);
    {
        QScopedValueRollback<bool> progressOwner(sourceArchiveProgress_, true);
        panel.AddToArchive(services);
    }
    if (!app_.QtOperations().closed()) {
        activatePanelUi(sourceIndex);
        setState(previousState);
        if (dialogShown && services.Result() == S_OK)
            refreshCurrent(&selection);
    }
}

void MainWindow::testArchive() {
    if (app_.QtOperations().closed())
        return;
    QtAppOperationState::CallScope call(app_.QtOperations());
    if (projectedUi().state != State::Viewing) {
        runArchiveBatchCommand(true);
        return;
    }
    auto &panel = app_.GetFocusedPanel();
    auto *folder = dynamic_cast<CAgentFolder *>(panel.GetFolder());
    auto *resource =
        folder ? dynamic_cast<QtArchiveFileResource *>(folder->GetEngineResource()) : nullptr;
    if (resource && resource->HasNativeRarReader() &&
        !ArchiveUsesNativeItemIndexes(folder->GetArchiveProperties())) {
        QMessageBox::warning(
            this, QtLangText(IDS_PROGRESS_TESTING),
            QCoreApplication::translate("mainwindow_archivecommands",
                                        "Source extraction is unavailable for this archive."));
        return;
    }
    if (resource && ArchiveHasExtractService(folder->GetArchiveProperties()) &&
        (resource->HasDirectInput() || resource->UsesNativeMetadata() ||
         backend_->hasListedSourceArchive(projectedUi().archive))) {
        setState(State::Operating);
        {
            QScopedValueRollback<bool> progressOwner(sourceArchiveProgress_, true);
            app_.TestArchives();
        }
        setState(State::Viewing);
        return;
    }
    previousState_ = projectedUi().state;
    setState(State::Operating);
    begin(QtLangText(IDS_PROGRESS_TESTING), projectedUi().archive);
    backend_->test(projectedUi().archive, projectedUi().archivePassword.toQString());
}
