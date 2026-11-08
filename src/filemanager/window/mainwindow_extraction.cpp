// Open-archive extraction presentation and source transport integration.
// Native extraction stays behind CPanel::CopyTo / ArchiveBackend.
#include "7zip/UI/FileManager/resource.h"
#include "7zip/UI/FileManager/resourceGui.h"
#include "7zip/UI/GUI/ExtractRes.h"
#include "archive/password/archivepassword.h"
#include "archive/resources/qtarchivebatchextract.h"
#include "archive/resources/qtarchiveextractoptions.h"
#include "archive/resources/qtarchivefileresource.h"
#include "archive/resources/qtarchiveproperties.h"
#include "archive/resources/qtextractpathpolicy.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtapppresentation.h"
#include "localization/qtlangcontrols.h"
#include "ui/progress/ProgressDialog2.h"
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
        QMessageBox warning(QMessageBox::Warning, QtLangText(IDS_PROGRESS_EXTRACTING),
                            output.error(), QMessageBox::Ok, parent);
        warning.setTextFormat(Qt::PlainText);
        warning.exec();
        return {};
    }
    return *output;
}

HRESULT ExtractPanelItems(CPanel &panel, CAgentFolder &folder, const QString &output,
                          const QString &password, const ExtractionOptions &options,
                          const CRecordVector<UInt32> &indices, bool explicitIndices,
                          UStringVector &messages, std::optional<QtArchiveExtractStatus> &status,
                          bool &callbackOK) {
    CCopyToOptions sourceOptions;
    QtArchiveExtractReport report;
    report.Purpose = options.Purpose;
    const auto captureStatus = qScopeGuard([&] {
        status = std::move(report.Status);
        callbackOK = !report.HadErrors;
    });
    const auto &parents = panel.GetParentFolders();
    CQtExtractPathPolicy pathPolicy(
        output,
        parents.IsEmpty() ? QStringList{} : QStringList{ToQString(parents.Front().VirtualPath)});
    CExtractNtOptions ntOptions;
    ntOptions.NtSecurity = options.NtSecurity;
    ntOptions.QtPathPolicy = &pathPolicy;
    // Permit sibling targets such as node_modules/.bin/tsc -> ../typescript/bin/tsc.
    // The native checks still reject traversal outside the output tree and links
    // through another link; QtPathPolicy validates the final destination as well.
    ntOptions.SymLinks_DangerousLevel = 9;
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
    sourceOptions.overwriteMode =
        QtArchiveOverwriteMode(options.overwriteMode).value_or(NExtract::NOverwriteMode::kSkip);
    if (password.isEmpty())
        return panel.CopyTo(sourceOptions, indices, &messages, &report);
    bool usePassword = true;
    NativePassword sourcePassword(password);
    return panel.CopyTo(sourceOptions, indices, &messages, usePassword, sourcePassword, nullptr,
                        &report);
}
} // namespace

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
    begin(QtLangText(IDS_PROGRESS_EXTRACTING), projectedUi().archive, destination);
    progress_->filesystemOutput = destination;
    backend_->extract(projectedUi().archive, destination, names,
                      password.isEmpty() ? projectedUi().archivePassword.toQString() : password,
                      options);
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
    if (output.isEmpty()) {
        if (explicitIndices)
            appPresentation_->CopyFinished(true);
        return true;
    }
    previousState_ = projectedUi().state;
    setState(State::Operating);
    QtArchiveBatchResult result;
    QStringList diagnostics;
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
                password.isEmpty() ? projectedUi().archivePassword.toQString() : password,
                sourceOptions, this);
            diagnostics = result.Diagnostics;
        } else {
            UStringVector messages;
            result.Result = ExtractPanelItems(panel, *folder, output, password, options, indices,
                                              explicitIndices != nullptr, messages,
                                              result.ExtractStatus, result.CallbackOK);
            result.MessageWasDisplayed =
                options.Purpose == QtArchiveExtractPurpose::Copy && !messages.IsEmpty();
            FOR_VECTOR(i, messages)
            diagnostics.append(ToQString(messages[i]));
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
    backendFinished(ok, message, result.Result, result.MessageWasDisplayed, result.ExtractStatus,
                    diagnostics, output);
    return true;
}
