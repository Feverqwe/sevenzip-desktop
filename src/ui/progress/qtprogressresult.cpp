#include "ui/lifecycle/qttaskpresentation.h"
#include "ui/progress/ProgressDialog2.h"

void CProgressDialog::PresentResult(QWidget *parent, const ResultPresentation &request) {
    auto *dialog = new CProgressDialog(parent, UString(request.title.toStdWString().c_str()));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::WindowModal);
    dialog->SetOperationResult(request.result, request.partial);
    dialog->SetLocations(request.source, request.destination);
    for (const QString &message : request.diagnostics)
        dialog->Sync.AddError_Message(UString(message.toStdWString().c_str()));
    dialog->OnInit();
    QtTaskPresentation::SetPhase(*dialog, QtOperationPhase::Reviewing);
    dialog->show();
    dialog->ProcessWasFinished();
}
