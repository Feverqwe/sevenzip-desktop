#include "dialogs/taskresult/qttaskresultdialog.h"
#include "localization/qtlangbuttonbox.h"
#include "localization/qtlangcontrols.h"
#include "ui/dialogs/qtdialoglayout.h"
#include "ui/lifecycle/qttaskpresentation.h"
#include "ui/reports/qttextreport.h"
#include <QDialogButtonBox>
#include <QPlainTextEdit>
#include <QVBoxLayout>

namespace {
QString ReviewReportText(const QtOperationReview &review) {
    using Dialog = QtTaskResultDialog;
    const bool cancelled = review.Result.Status == QtOperationResult::Outcome::Cancelled;
    QStringList lines;
    if (cancelled)
        lines.append(Dialog::tr("The operation was cancelled."));
    if (!review.Result.Error.isEmpty())
        lines.append(review.Result.Error);
    if (!review.Job.Paths.isEmpty()) {
        lines.append(QString());
        lines.append(Dialog::tr("Selected items"));
        lines.append(review.Job.Paths);
    }
    if (!review.Job.ArchivePath.isEmpty()) {
        lines.append(QString());
        lines.append(Dialog::tr("Requested archive"));
        lines.append(review.Job.ArchivePath);
    }
    if (!review.Job.Destination.isEmpty()) {
        lines.append(QString());
        lines.append(Dialog::tr("Requested destination"));
        lines.append(review.Job.Destination);
    }
    if (!review.Result.ChangedDirectories.isEmpty()) {
        lines.append(QString());
        lines.append(Dialog::tr("Reported output folders"));
        lines.append(review.Result.ChangedDirectories);
    }
    if (!review.Result.ChangedArchives.isEmpty()) {
        lines.append(QString());
        lines.append(Dialog::tr("Reported output archives"));
        lines.append(review.Result.ChangedArchives);
    }
    return lines.join('\n');
}
} // namespace

QtTaskResultDialog::QtTaskResultDialog(const QtOperationReview &review)
    : QtTaskResultDialog(
          review.Result.Status == QtOperationResult::Outcome::Cancelled
              ? QT_TRANSLATE_NOOP("QtTaskResultDialog", "The operation was cancelled.")
              : QT_TRANSLATE_NOOP("QtTaskResultDialog",
                                  "The operation result could not be verified."),
          ReviewReportText(review)) {}

QtTaskResultDialog::QtTaskResultDialog(const char *summaryText, const QString &reportText,
                                       QWidget *parent)
    : QDialog(parent) {
    setObjectName("independentTaskResultDialog");
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    QtTaskPresentation::Watch(*this, QtOperationPhase::Reviewing);
    QtBindLocalText(this, "QtTaskResultDialog",
                    QT_TRANSLATE_NOOP("QtTaskResultDialog", "Operation result"));
    auto *actions = new QtLangButtonBox(QDialogButtonBox::Close, this);
    actions->setObjectName("taskResultActions");
    auto *body = QtCreateDialogLayout(*this, *actions);
    auto *summary = new QtWrappingLabel(this);
    summary->setObjectName("taskResultSummary");
    summary->setTextFormat(Qt::PlainText);
    summary->setWordWrap(true);
    QtBindLocalText(summary, "QtTaskResultDialog", summaryText);
    body->addWidget(summary);
    if (!reportText.isEmpty()) {
        auto *report = new QPlainTextEdit(this);
        report->setObjectName("taskResultReport");
        QtConfigureTextReport(*report, font());
        report->setMinimumSize(160, 100);
        // Keep native diagnostic text intact, including untrusted paths.
        report->setPlainText(reportText);
        body->addWidget(report, 1);
        resize(600, 380);
    }
    connect(actions, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
