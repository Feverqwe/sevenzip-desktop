#include "7zip/UI/FileManager/resource.h"
#include "7zip/UI/FileManager/resourceGui.h"
#include "7zip/UI/GUI/ExtractRes.h"
#include "archive/agent/Agent.h"
#include "archive/resources/qtarchivetransferbackup.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtapppresentation.h"
#include "localization/qtlangcontrols.h"
#include "platform/trash/qttrashservice.h"
#include "ui/progress/ProgressDialog2.h"
#include <QScopedValueRollback>

void MainWindow::transferFromArchive(const QString &destination, bool move, bool copyToSame,
                                     unsigned sourceIndex, const CRecordVector<UInt32> &indices) {
    const QStringList paths = archivePathsForIndices(indices);
    if (paths.isEmpty())
        return;
    ExtractionOptions options;
    options.pathMode = ExtractionOptions::PathMode::CurrentPaths;
    options.currentFolder = projectedUi().archiveFolder;
    options.overwriteMode = ExtractionOptions::OverwriteMode::Ask;
    options.Purpose = move ? QtArchiveExtractPurpose::Move : QtArchiveExtractPurpose::Copy;
    if (move)
        archiveMove_ = std::make_unique<PendingArchiveMove>(
            sourceIndex, indices, ToQString(app_.Panels[sourceIndex].GetCurrentFolderPrefix()),
            destination);
    const bool handled = extractWithSource(destination, true, {}, options, paths, &indices);
    if (handled && projectedUi().state == State::Operating)
        return; // Asynchronous completion owns the pending transfer.
    if (!handled && !copyToSame &&
        (!move || (QFileInfo(destination).isDir() && !QFileInfo(destination).isSymLink()))) {
        previousState_ = projectedUi().state;
        setState(State::Operating);
        begin(QtLangText(move ? IDS_MOVING : IDS_PROGRESS_EXTRACTING), projectedUi().archive,
              destination);
        // Source refusal permits fallback only at the archive root. Retain
        // Move's full-path transport and Copy's explicit extraction options.
        if (move) {
            ExtractionOptions fallback;
            fallback.Purpose = QtArchiveExtractPurpose::Move;
            backend_->extract(projectedUi().archive, destination, paths,
                              projectedUi().archivePassword.toQString(), fallback);
        } else
            backend_->extract(projectedUi().archive, destination, paths,
                              projectedUi().archivePassword.toQString(), options);
        return;
    }
    // A synchronous result or refused launch has no pending continuation.
    // Clear it before an error dialog can deliver another GUI callback.
    archiveMove_.reset();
    if (!handled) {
        if (copyToSame)
            QMessageBox::warning(
                this, QtLangText(IDS_COPYING),
                QCoreApplication::translate("mainwindow_fileoperations",
                                            "Source extraction is unavailable for this archive."));
        else
            QMessageBox::warning(
                this, "Sevenzip",
                QCoreApplication::translate("mainwindow_fileoperations",
                                            "The destination folder is unavailable."));
    }
}

void MainWindow::refreshArchiveTransferDestination(unsigned index, const CSelectedState &selection,
                                                   ArchiveTransferReload reload) {
    withPanelProjection(index, [&] {
        if (reload == ArchiveTransferReload::ReopenArchive)
            refreshCurrent(&selection);
        else
            app_.Panels[index].RefreshListCtrl(selection);
        appPresentation_->CopyDestinationRefreshed(index);
    });
    // As with independent GUI outputs, retain ReOpen's modal guards through
    // projection restoration, then publish the resulting idle/scan state.
    if (reload == ArchiveTransferReload::ReopenArchive)
        setState(projectedUi().state);
}

bool MainWindow::transferBetweenArchivePanels(const QString &destination, bool move,
                                              const CRecordVector<UInt32> &indices) {
    if (app_.NumPanels != 2 || projectedUi().state != State::Viewing)
        return false;
    const unsigned sourceIndex = app_.GetFocusedPanelIndex();
    const unsigned destinationIndex = 1 - sourceIndex;
    CPanel &source = app_.Panels[sourceIndex];
    CPanel &target = app_.Panels[destinationIndex];
    if (panelUi_[destinationIndex].state != State::Viewing ||
        QDir::cleanPath(destination) != QDir::cleanPath(ToQString(target.GetFsPath())))
        return false;
    if (source.GetParentFolders().Size() != 1 || target.GetParentFolders().Size() != 1 ||
        QDir::cleanPath(projectedUi().archive) ==
            QDir::cleanPath(panelUi_[destinationIndex].archive) ||
        target.IsThereReadOnlyFolder()) {
        QMessageBox::warning(
            this, QtLangText(IDS_COPYING),
            QCoreApplication::translate("mainwindow_archive_transfer",
                                        "Transfer between these archives is unavailable."));
        return true;
    }
    if (indices.IsEmpty())
        return true;
    const QString sourceLocation = ToQString(source.GetCurrentFolderPrefix());
    const QString destinationLocation = ToQString(target.GetCurrentFolderPrefix());
    CSelectedState destinationSelection;
    target.SaveSelectedState(destinationSelection);
    UStringVector messages;
    QStringList terminalDiagnostics;
    HRESULT result;
    QtArchiveTransferResult transfer{E_FAIL, {}};
    std::unique_ptr<QtArchiveTransferBackup> rollback;
    const QString destinationArchive = panelUi_[destinationIndex].archive;
    if (move) {
        auto saved = backend_->saveArchiveTransferDestination(destinationArchive);
        if (saved)
            rollback = std::move(*saved);
    }
    if (move && !rollback) {
        QMessageBox::warning(
            this, QtLangText(IDS_MOVING),
            QCoreApplication::translate("mainwindow_archive_transfer",
                                        "Could not save destination archive for rollback."));
        return true;
    }
    enum class Outcome { Stopped, Updated, Retained, RolledBack, RollbackFailed };
    Outcome outcome = Outcome::Stopped;
    CSelectedState selected;
    // CopyFrom can replace the peer's archive indices. Until both panels have
    // published their new rows, chrome must not query the old peer selection.
    QScopedValueRollback<bool> progressOwner(sourceArchiveProgress_, true);
    {
        transfer = app_.CopyArchiveToArchive(sourceIndex, indices, &messages);
        result = transfer.Result;
        if (result == S_OK && messages.IsEmpty()) {
            outcome = Outcome::Updated;
            if (move) {
                const auto removed = source.DeleteItems(false, *trashService_, selected, &indices,
                                                        true, QtDeleteResultReview::Caller);
                terminalDiagnostics = removed.Diagnostics;
                result = removed.Result;
                if (result != S_OK) {
                    if (removed.Published == false) {
                        const bool restored = rollback->restore([&target] {
                            auto *folder = dynamic_cast<CAgentFolder *>(target.GetFolder());
                            return folder && folder->CloseArchive() == S_OK;
                        });
                        outcome = restored ? Outcome::RolledBack : Outcome::RollbackFailed;
                    } else
                        outcome = Outcome::Retained;
                }
            }
        }
    }
    // All native work (including Move rollback) is settled before publication.
    if (app_.QtOperations().kind() == QtAppOperationState::Kind::Operation)
        app_.QtOperations().End();
    const bool retainedUpdate = outcome == Outcome::Updated || outcome == Outcome::Retained;
    if (retainedUpdate && move) {
        renderBoundFolder();
        source.RestoreLoadedSelection(selected);
        appPresentation_->CopySourceRefreshed(sourceIndex);
    }
    if (outcome != Outcome::Stopped)
        refreshArchiveTransferDestination(destinationIndex, destinationSelection,
                                          retainedUpdate ? ArchiveTransferReload::BoundFolder
                                                         : ArchiveTransferReload::ReopenArchive);
    if (outcome == Outcome::RollbackFailed) {
        terminalDiagnostics.prepend(QCoreApplication::translate(
            "mainwindow_archive_transfer",
            "Could not roll back destination archive. Check both archives."));
        CProgressDialog::PresentResult(this, {.title = QtLangText(IDS_MOVING),
                                              .result = E_FAIL,
                                              .diagnostics = terminalDiagnostics,
                                              .source = sourceLocation,
                                              .destination = destinationLocation,
                                              .partial = true});
        return true;
    }
    if (outcome == Outcome::Retained) {
        const QString message = QCoreApplication::translate(
            "mainwindow_archive_transfer",
            "Source removal did not finish cleanly. The destination copy was retained; check "
            "both archives.");
        showStatusMessage(message);
        terminalDiagnostics.prepend(message);
        CProgressDialog::PresentResult(this, {.title = QtLangText(IDS_MOVING),
                                              .result = result,
                                              .diagnostics = terminalDiagnostics,
                                              .source = sourceLocation,
                                              .destination = destinationLocation,
                                              .partial = true});
        return true;
    }
    if (outcome == Outcome::RolledBack) {
        terminalDiagnostics.prepend(QCoreApplication::translate(
            "mainwindow_archive_transfer", "The destination archive was restored."));
        CProgressDialog::PresentResult(this, {.title = QtLangText(IDS_MOVING),
                                              .result = result,
                                              .diagnostics = terminalDiagnostics,
                                              .source = sourceLocation,
                                              .destination = destinationLocation});
        return true;
    }
    if (outcome == Outcome::Updated) {
        showStatusMessage(move ? QCoreApplication::translate("mainwindow_archive_transfer",
                                                             "Items moved between archives.")
                               : QCoreApplication::translate("mainwindow_archive_transfer",
                                                             "Items copied into the archive."));
    } else if (result != S_OK || !messages.IsEmpty()) {
        FOR_VECTOR(i, messages)
        terminalDiagnostics.append(ToQString(messages[i]));
        if (!transfer.Error.isEmpty())
            terminalDiagnostics.append(transfer.Error);
        CProgressDialog::PresentResult(this, {.title = QtLangText(move ? IDS_MOVING : IDS_COPYING),
                                              .result = result,
                                              .diagnostics = terminalDiagnostics,
                                              .source = sourceLocation,
                                              .destination = destinationLocation,
                                              .partial = result == S_OK});
    }
    return true;
}
