#include "7zip/UI/FileManager/resourceGui.h"
#include "Windows/ErrorMsg.h"
#include "archive/callbacks/IFileExtractCallback.h"
#include "dialogs/overwrite/OverwriteDialog.h"
#include "dialogs/password/PasswordDialog.h"
#include "filemanager/window/mainwindow.h"
#include "filemanager/window/qtapppresentation.h"
#include "localization/qtlangcontrols.h"
#include "platform/trash/qttrashservice.h"
#include "ui/progress/ProgressDialog2.h"
#include <QCoreApplication>
#include <QScopedValueRollback>

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
        projectedUi().archivePassword = ToQString(dialog.Password);
        backend_->providePassword(projectedUi().archivePassword);
    });
    connect(backend_, &ArchiveBackend::outputReceived, this, [this](const QString &line) {
        if (!app_.QtOperations().closed()) {
            if (progress_ && (line.startsWith(QStringLiteral("ERROR:"), Qt::CaseInsensitive) ||
                              line.startsWith(QStringLiteral("WARNING:"), Qt::CaseInsensitive)))
                progress_->Sync.AddError_Message(line.toStdWString().c_str());
        }
    });
    connect(backend_, &ArchiveBackend::finished, this, [this](bool ok, const QString &message) {
        backendFinished(ok, message, backend_->lastResult());
    });
}

void MainWindow::backendFinished(bool ok, const QString &message, HRESULT result,
                                 bool messageDisplayed) {
    if (app_.QtOperations().closed())
        return;
    QtAppOperationState::CallScope call(app_.QtOperations());
    if (sourceArchiveProgress_)
        return; // Source worker owns completion until its modal lifecycle joins.
    if (startPendingArchiveMove(ok))
        return;
    QString resultMessage = message;
    if (archiveMove_ && archiveMove_->stage == ArchiveMoveStage::Removing)
        resultMessage =
            ok ? QCoreApplication::translate("mainwindow_backendevents",
                                             "Items moved out of archive.")
               : QCoreApplication::translate("mainwindow_backendevents",
                                             "Files extracted but not deleted from archive. ") +
                     message;
    archiveMove_.reset();
    if (!ok)
        projectedUi().archivePassword.clear();
    const bool hadProgress = progress_;
    const bool canceled = result == E_ABORT;
    if (hadProgress && !ok && !canceled)
        progress_->Sync.FinalMessage.ErrorMessage.Message = ToUString(resultMessage);
    closeProgressDialog(true);
    if (projectedUi().state == State::Loading && !ok)
        showDirectory(projectedUi().returnDirectory, false);
    else if (projectedUi().state == State::Operating)
        setState(previousState_ == State::Viewing ? State::Viewing : State::Filesystem);
    appPresentation_->CopyFinished();
    statusBar()->showMessage(resultMessage);
    if (!ok && !hadProgress && !messageDisplayed && !canceled)
        QMessageBox::warning(this, "Sevenzip", resultMessage);
}

bool MainWindow::startPendingArchiveMove(bool ok) {
    if (archiveMove_ && archiveMove_->stage == ArchiveMoveStage::Extracting && ok) {
        // Closing progress can deliver callbacks that clear pending state.
        // Keep an owning snapshot of the accepted deletion request.
        const PendingArchiveMove request = *archiveMove_;
        archiveMove_->stage = ArchiveMoveStage::Removing;
        closeProgressDialog();
        auto &panel = app_.Panels[request.source];
        CSelectedState selected;
        HRESULT result;
        {
            QScopedValueRollback<bool> progressOwner(sourceArchiveProgress_, true);
            result =
                panel.DeleteItems(false, *trashService_, selected, &request.indices, false).Result;
        }
        app_.QtOperations().End();
        renderBoundFolder();
        panel.RestoreLoadedSelection(selected);
        backendFinished(result == S_OK, QString::fromWCharArray(HResultToMessage(result)), result,
                        result != S_OK);
        return true;
    }
    return false;
}

void MainWindow::closeProgressDialog(bool finished) {
    if (!progress_)
        return;
    CProgressDialog *completed = progress_;
    progress_ = nullptr;
    completed->AttachTransport(nullptr);
    // Source progress is destroyed before OnCopy refreshes the title. Qt keeps
    // the completed dialog through queued close/error presentation; it must no
    // longer overwrite the application owner's restored title on destruction.
    completed->MainWindow = nullptr;
    progressTransport_.reset();
    if (finished) {
        completed->setObjectName("completedProgressDialog");
        connect(completed, &QDialog::finished, completed, &QObject::deleteLater);
        completed->ProcessWasFinished();
        // ArchiveBackend and Qt workers finish on the GUI event loop. Deliver
        // the source close/final-message transition before commands become
        // available for the next operation.
        QCoreApplication::sendPostedEvents(completed, QEvent::MetaCall);
    } else {
        // QDialog::close routes through the source reject/cancel confirmation.
        // An already completed stage must be dismissed without a new prompt.
        completed->done(0);
        completed->deleteLater();
    }
}
