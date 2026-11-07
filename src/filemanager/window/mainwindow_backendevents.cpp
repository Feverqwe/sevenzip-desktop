#include "7zip/UI/FileManager/resourceGui.h"
#include "Windows/ErrorMsg.h"
#include "archive/callbacks/IFileExtractCallback.h"
#include "archive/password/archivepassword.h"
#include "dialogs/overwrite/OverwriteDialog.h"
#include "dialogs/password/PasswordDialog.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtapppresentation.h"
#include "localization/qtlangcontrols.h"
#include "platform/trash/qttrashservice.h"
#include "ui/progress/ProgressDialog2.h"
#include <QCoreApplication>
#include <QScopedValueRollback>
#include <utility>

void MainWindow::connectBackend() {
    connect(backend_, &ArchiveBackend::overwriteRequired, this,
            [this](const OverwriteQuestion &question) {
                if (app_.QtOperations().closed() || sourceArchiveProgress_) {
                    backend_->provideOverwriteAnswer(NOverwriteAnswer::kCancel);
                    return;
                }
                COverwriteDialog dialog(this);
                overwriteDialog_ = &dialog;
                dialog.SetQuestion(question);
                connect(backend_, &ArchiveBackend::dismissOverwriteRequested, &dialog,
                        &QDialog::reject, Qt::QueuedConnection);
                const int answer = NOverwriteDialog::MapButtonResult(dialog.Create(this));
                overwriteDialog_ = nullptr;
                backend_->provideOverwriteAnswer(answer);
            });
    connect(backend_, &ArchiveBackend::passwordRequired, this, [this] {
        if (app_.QtOperations().closed() || sourceArchiveProgress_)
            return;
        CPasswordDialog dialog(this);
        dialog.OnInit();
        if (dialog.exec() != QDialog::Accepted) {
            backend_->cancel();
            return;
        }
        projectedUi().archivePassword = ArchivePassword(dialog.Password);
        backend_->providePassword(projectedUi().archivePassword.toQString());
    });
    connect(backend_, &ArchiveBackend::outputReceived, this, [this](const QString &line) {
        if (!app_.QtOperations().closed()) {
            if (auto *progress = progressDialog();
                progress && (line.startsWith(QStringLiteral("ERROR:"), Qt::CaseInsensitive) ||
                             line.startsWith(QStringLiteral("WARNING:"), Qt::CaseInsensitive)))
                progress->Sync.AddError_Message(line.toStdWString().c_str());
        }
    });
    connect(backend_, &ArchiveBackend::finished, this, [this](bool ok, const QString &message) {
        backendFinished(ok, message, backend_->lastResult(), false, backend_->lastExtractStatus());
    });
}

void MainWindow::backendFinished(bool ok, const QString &message, HRESULT result,
                                 bool messageDisplayed,
                                 std::optional<QtArchiveExtractStatus> extraction,
                                 const QStringList &diagnostics) {
    if (app_.QtOperations().closed())
        return;
    QtAppOperationState::CallScope call(app_.QtOperations());
    if (sourceArchiveProgress_)
        return; // Source worker owns completion until its modal lifecycle joins.
    QString resultMessage = message;
    QStringList terminalDiagnostics = diagnostics;
    bool reviewAfterPublication = false;
    const auto move = std::move(archiveMove_);
    reviewAfterPublication =
        move && (!ok || !extraction || !extraction->Complete || !terminalDiagnostics.isEmpty());
    if (move && ok) {
        if (!extraction || !extraction->Complete)
            resultMessage = QCoreApplication::translate(
                "mainwindow_backendevents",
                "Source items were kept because complete extraction could not be confirmed.");
        else {
            QtArchiveUpdateResult removed{E_FAIL, std::nullopt};
            {
                QScopedValueRollback<bool> progressOwner(sourceArchiveProgress_, true);
                closeProgressDialog();
                withPanelProjection(move->source, [&] {
                    auto &panel = app_.Panels[move->source];
                    CSelectedState selection;
                    removed = panel.RemoveItemsAfterMove(move->indices, selection, false);
                    app_.QtOperations().End();
                    renderBoundFolder();
                    panel.RestoreLoadedSelection(selection);
                });
            }
            result = removed.Result;
            ok = result == S_OK;
            reviewAfterPublication = !ok;
            terminalDiagnostics = removed.Diagnostics;
            messageDisplayed = !ok; // The composed owner presents after publication below.
            if (ok)
                resultMessage = QCoreApplication::translate("mainwindow_backendevents",
                                                            "Items moved out of archive.");
            else {
                resultMessage =
                    removed.Published == false
                        ? QCoreApplication::translate(
                              "mainwindow_backendevents",
                              "Files extracted but not deleted from archive. ")
                        : QCoreApplication::translate(
                              "mainwindow_backendevents",
                              "Files were extracted, but source removal did not finish cleanly. "
                              "Check the source archive. ");
                resultMessage += QString::fromWCharArray(HResultToMessage(result));
            }
        }
    }
    if (!ok)
        projectedUi().archivePassword.clear();
    auto *progress = progressDialog();
    const bool hadProgress = progress != nullptr;
    const bool canceled = result == E_ABORT;
    if (progress) {
        progress->DeferResultReview = reviewAfterPublication;
        if (reviewAfterPublication) {
            FOR_VECTOR(i, progress->Sync.Messages)
            terminalDiagnostics.append(ToQString(progress->Sync.Messages[i]));
        }
        progress->SetOperationResult(result, (extraction && !extraction->Complete) ||
                                                 (move && ok && !extraction));
    }
    if (hadProgress && !ok && !canceled)
        progress->Sync.FinalMessage.ErrorMessage.Message = ToUString(resultMessage);
    closeProgressDialog(true);
    if (projectedUi().state == State::Loading && !ok)
        showDirectory(projectedUi().returnDirectory, false);
    else if (projectedUi().state == State::Operating)
        setState(previousState_ == State::Viewing ? State::Viewing : State::Filesystem);
    appPresentation_->CopyFinished();
    showStatusMessage(resultMessage);
    if (reviewAfterPublication) {
        terminalDiagnostics.prepend(resultMessage);
        CProgressDialog::PresentResult(this, {.title = QtLangText(IDS_MOVING),
                                              .result = !ok && result == S_OK ? E_FAIL : result,
                                              .diagnostics = terminalDiagnostics,
                                              .source = move->sourceLocation,
                                              .destination = move->destination,
                                              .partial = extraction && !extraction->Complete});
    } else if (!ok && !hadProgress && !messageDisplayed && !canceled)
        QMessageBox::warning(this, "Sevenzip", resultMessage);
}

void MainWindow::closeProgressDialog(bool finished) {
    const auto progress = std::move(progress_);
    if (!progress || !progress->dialog)
        return;
    CProgressDialog *completed = progress->dialog;
    completed->AttachTransport(nullptr);
    // Source progress is destroyed before OnCopy refreshes the title. Qt keeps
    // the completed dialog through queued close/error presentation; it must no
    // longer overwrite the application owner's restored title on destruction.
    completed->MainWindow = nullptr;
    progress->transport.reset();
    if (!progress->dialog)
        return;
    if (finished) {
        completed->setObjectName("completedProgressDialog");
        connect(completed, &QDialog::finished, completed, &QObject::deleteLater);
        completed->ProcessWasFinished();
        // ArchiveBackend and Qt workers finish on the GUI event loop. Deliver
        // the source close/final-message transition before commands become
        // available for the next operation.
        if (progress->dialog)
            QCoreApplication::sendPostedEvents(completed, QEvent::MetaCall);
    } else {
        // QDialog::close routes through the source reject/cancel confirmation.
        // An already completed stage must be dismissed without a new prompt.
        completed->done(0);
        if (progress->dialog)
            completed->deleteLater();
    }
}
