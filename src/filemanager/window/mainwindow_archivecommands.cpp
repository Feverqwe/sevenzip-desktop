#include "7zip/PropID.h"
#include "7zip/UI/FileManager/resource.h"
#include "7zip/UI/FileManager/resourceGui.h"
#include "7zip/UI/GUI/CompressDialogRes.h"
#include "7zip/UI/GUI/ExtractRes.h"
#include "Windows/PropVariant.h"
#include "archive/hashing/HashCalc.h"
#include "archive/resources/qtarchivebatchextract.h"
#include "archive/resources/qtarchivefileresource.h"
#include "archive/resources/qtarchiveproperties.h"
#include "archive/resources/qtextractpathpolicy.h"
#include "dialogs/messages/MessagesDialog.h"
#include "filemanager/window/mainwindow.h"
#include "gui/UpdateGUI.h"
#include "gui/resource2.h"
#include "localization/qtlangcontrols.h"
#include "platform/shell/qtshellarchiveservices.h"
#include <QScopeGuard>
#include <QScopedValueRollback>

namespace {
QString PrepareExtractionDestination(ArchiveBackend &backend, QtArchiveFileResource &resource,
                                     const QString &destination, const QStringList &names,
                                     const ExtractionOptions &options, const QString &archiveFolder,
                                     bool nativeIndexes, QWidget *parent) {
    const auto output = backend.prepareExtractionDestination(
        destination, names, options, nativeIndexes, archiveFolder,
        nativeIndexes && resource.UsesNativeMetadata() ? resource.NativeArchiveLink()->GetArc()
                                                       : nullptr);
    if (!output) {
        QMessageBox::warning(parent, QtLangText(IDS_PROGRESS_EXTRACTING), output.error());
        return {};
    }
    return *output;
}

HRESULT ExtractPanelItems(CPanel &panel, CAgentFolder &folder, const QString &output,
                          const QString &password, const ExtractionOptions &options,
                          const CRecordVector<UInt32> &indices, bool explicitIndices,
                          UStringVector &messages, std::optional<QtArchiveExtractStatus> &status) {
    CCopyToOptions sourceOptions;
    QtArchiveExtractReport report;
    report.Purpose = options.Purpose;
    const auto captureStatus = qScopeGuard([&] { status = std::move(report.Status); });
    CQtExtractPathPolicy pathPolicy(output);
    CExtractNtOptions ntOptions;
    ntOptions.NtSecurity = options.NtSecurity;
    ntOptions.QtPathPolicy = &pathPolicy;
    QScopedValueRollback<const CExtractNtOptions *> extractOptions(folder.QtExtractNtOptions,
                                                                   &ntOptions);
    sourceOptions.extractMode = options.pathMode != ExtractionOptions::PathMode::CurrentPaths;
    sourceOptions.folder = ToUString(QDir::toNativeSeparators(output));
    sourceOptions.pathMode = options.pathMode == ExtractionOptions::PathMode::NoPaths
                                 ? NExtract::NPathMode::kNoPaths
                             : options.pathMode == ExtractionOptions::PathMode::AbsolutePaths
                                 ? NExtract::NPathMode::kAbsPaths
                                 : NExtract::NPathMode::kFullPaths;
    // F5/F6 retains CCopyToOptions' registry-zone default. The explicit Extract
    // dialog has its own options.
    if (!explicitIndices)
        sourceOptions.NeedRegistryZone = false;
    sourceOptions.showErrorMessages = true;
    switch (options.overwriteMode) {
    case ExtractionOptions::OverwriteMode::Ask:
        break;
    case ExtractionOptions::OverwriteMode::OverwriteExisting:
        sourceOptions.overwriteMode = NExtract::NOverwriteMode::kOverwrite;
        break;
    case ExtractionOptions::OverwriteMode::SkipExisting:
    case ExtractionOptions::OverwriteMode::RejectExisting:
        sourceOptions.overwriteMode = NExtract::NOverwriteMode::kSkip;
        break;
    case ExtractionOptions::OverwriteMode::RenameExtracted:
        sourceOptions.overwriteMode = NExtract::NOverwriteMode::kRename;
        break;
    case ExtractionOptions::OverwriteMode::RenameExisting:
        sourceOptions.overwriteMode = NExtract::NOverwriteMode::kRenameExisting;
        break;
    }
    if (password.isEmpty())
        return panel.CopyTo(sourceOptions, indices, &messages, &report);
    bool usePassword = true;
    UString sourcePassword = ToUString(password);
    return panel.CopyTo(sourceOptions, indices, &messages, usePassword, sourcePassword, nullptr,
                        &report);
}
} // namespace

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

void MainWindow::extractTo(const QString &destination, bool onlySelected, const QString &password,
                           const ExtractionOptions &options) {
    if (app_.QtOperations().closed())
        return;
    QtAppOperationState::CallScope call(app_.QtOperations());
    if (projectedUi().state != State::Viewing || destination.isEmpty())
        return;
    const auto *folder = dynamic_cast<CAgentFolder *>(app_.GetFocusedPanel().GetFolder());
    IInArchive *metadata = folder ? folder->GetArchiveProperties() : nullptr;
    UInt32 count = 0;
    if (!metadata || metadata->GetNumberOfItems(&count) != S_OK || count == 0)
        return;
    QStringList names;
    if (onlySelected) {
        names = selectedArchivePaths();
        if (names.isEmpty())
            return;
    }
    if (extractWithSource(destination, onlySelected, password, options, names))
        return;
    previousState_ = projectedUi().state;
    setState(State::Operating);
    begin(QtLangText(IDS_PROGRESS_EXTRACTING), destination);
    backend_->extract(projectedUi().archive, destination, names,
                      password.isEmpty() ? projectedUi().archivePassword : password, options);
}

bool MainWindow::extractWithSource(const QString &destination, bool onlySelected,
                                   const QString &password, const ExtractionOptions &options,
                                   const QStringList &names,
                                   const CRecordVector<UInt32> *explicitIndices) {
    CPanel &panel = app_.GetFocusedPanel();
    auto *folder = projectedUi().state == State::Viewing
                       ? dynamic_cast<CAgentFolder *>(panel.GetFolder())
                       : nullptr;
    auto *resource =
        folder ? dynamic_cast<QtArchiveFileResource *>(folder->GetEngineResource()) : nullptr;
    const bool sourceAvailable =
        resource && (resource->HasDirectInput() || resource->UsesNativeMetadata() ||
                     backend_->hasListedSourceArchive(projectedUi().archive));
    const bool nativeIndexes =
        folder && ArchiveUsesNativeItemIndexes(folder->GetArchiveProperties());
    const bool extractAvailable =
        folder && ArchiveHasExtractService(folder->GetArchiveProperties());
    if (options.pathMode == ExtractionOptions::PathMode::CurrentPaths &&
        (!extractAvailable || !sourceAvailable)) {
        if (options.currentFolder.isEmpty())
            return false; // At archive root, the existing full-path transport is equivalent.
        QMessageBox::warning(
            this, QtLangText(IDS_COPYING),
            QCoreApplication::translate("mainwindow_archivecommands",
                                        "Source extraction is unavailable for this archive."));
        return true;
    }
    const bool nativeRar = resource && resource->HasNativeRarReader();
    if (nativeRar && !nativeIndexes) {
        QMessageBox::warning(
            this, QtLangText(IDS_PROGRESS_EXTRACTING),
            QCoreApplication::translate("mainwindow_archivecommands",
                                        "Source extraction is unavailable for this archive."));
        return true;
    }
    if (!extractAvailable || !sourceAvailable)
        return false;
    const bool scoped = onlySelected || !projectedUi().archiveFolder.isEmpty() ||
                        resource->HasDirectInput() || resource->UsesNativeMetadata() ||
                        !resource->FilesystemReadLimitations().isEmpty();
    CRecordVector<UInt32> indices;
    QStringList validationNames = names;
    if (scoped) {
        if (onlySelected)
            indices = explicitIndices ? *explicitIndices : sourceOperatedIndices();
        else {
            UInt32 count = 0;
            if (folder->GetNumberOfItems(&count) != S_OK)
                return true;
            for (UInt32 index = 0; index < count; ++index)
                indices.Add(index);
        }
        if (indices.IsEmpty())
            return true;
        validationNames = archivePathsForIndices(indices);
        if (validationNames.isEmpty())
            return true;
    }
    const QString output =
        PrepareExtractionDestination(*backend_, *resource, destination, validationNames, options,
                                     projectedUi().archiveFolder, nativeIndexes, this);
    if (output.isEmpty())
        return true;
    previousState_ = projectedUi().state;
    setState(State::Operating);
    QtArchiveBatchResult result;
    QString message;
    {
        QScopedValueRollback<bool> progressOwner(sourceArchiveProgress_, true);
        if (!scoped) {
            ExtractionOptions sourceOptions = options;
            // Validation above implements RejectExisting. If a file appears
            // afterwards, source Skip also preserves it without overwriting.
            if (sourceOptions.overwriteMode == ExtractionOptions::OverwriteMode::RejectExisting)
                sourceOptions.overwriteMode = ExtractionOptions::OverwriteMode::SkipExisting;
            result = RunQtArchiveBatchExtract(
                *backend_, {projectedUi().archive}, output,
                password.isEmpty() ? projectedUi().archivePassword : password, sourceOptions, this);
        } else {
            UStringVector messages;
            result.Result =
                ExtractPanelItems(panel, *folder, output, password, options, indices,
                                  explicitIndices != nullptr, messages, result.ExtractStatus);
            result.CallbackOK = messages.IsEmpty();
            result.MessageWasDisplayed = !messages.IsEmpty();
            if (!messages.IsEmpty())
                message = ToQString(messages[0]);
        }
    }
    const bool ok = result.Result == S_OK && result.CallbackOK;
    if (!scoped || !result.MessageWasDisplayed)
        message =
            ok ? QCoreApplication::translate("mainwindow_archivecommands", "Extraction completed.")
            : result.Result == E_ABORT
                ? QCoreApplication::translate("mainwindow_archivecommands", "Operation canceled.")
            : result.Result == S_OK
                ? QCoreApplication::translate("mainwindow_archivecommands",
                                              "Extraction completed with errors.")
                : QString::fromWCharArray(HResultToMessage(result.Result));
    backendFinished(ok, message, result.Result, result.MessageWasDisplayed, result.ExtractStatus);
    return true;
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
    backend_->test(projectedUi().archive, projectedUi().archivePassword);
}
