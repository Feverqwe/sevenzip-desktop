#include "dialogs/taskresult/qttaskresultdialog.h"
#include "localization/qtlangbuttonbox.h"
#include "localization/qtlangcontrols.h"
#include "ui/dialogs/qtdialoglayout.h"
#include "ui/lifecycle/qttaskpresentation.h"
#include "ui/reports/qttextreport.h"
#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

QtTaskResultDialog::QtTaskResultDialog(const QtOperationReview &review) {
    setObjectName("independentTaskResultDialog");
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    QtTaskPresentation::Watch(*this, QtOperationPhase::Reviewing);
    QtBindLocalText(this, "QtTaskResultDialog",
                    QT_TRANSLATE_NOOP("QtTaskResultDialog", "Operation result"));
    auto *actions = new QtLangButtonBox(QDialogButtonBox::Close, this);
    actions->setObjectName("taskResultActions");
    auto *copy = actions->addButton(tr("Copy"), QDialogButtonBox::ActionRole);
    copy->setObjectName("taskResultCopy");
    QtBindLocalText(copy, "QtTaskResultDialog", QT_TRANSLATE_NOOP("QtTaskResultDialog", "Copy"));
    auto *body = QtCreateDialogLayout(*this, *actions);
    auto *summary = new QtWrappingLabel(this);
    summary->setObjectName("taskResultSummary");
    summary->setTextFormat(Qt::PlainText);
    summary->setWordWrap(true);
    QtBindLocalText(
        summary, "QtTaskResultDialog",
        QT_TRANSLATE_NOOP("QtTaskResultDialog", "The operation result could not be verified."));
    body->addWidget(summary);
    auto *report = new QPlainTextEdit(this);
    report->setObjectName("taskResultReport");
    QtConfigureTextReport(*report, font());
    report->setMinimumSize(160, 100);
    // Keep native diagnostic text intact; no parsing of localized messages or
    // untrusted paths. The selected inputs are values, never provider handles.
    QStringList lines{review.Result.Error};
    if (!review.Job.Paths.isEmpty()) {
        lines.append(QString());
        lines.append(tr("Selected items"));
        lines.append(review.Job.Paths);
    }
    if (!review.Job.ArchivePath.isEmpty()) {
        lines.append(QString());
        lines.append(tr("Requested archive"));
        lines.append(review.Job.ArchivePath);
    }
    if (!review.Job.Destination.isEmpty()) {
        lines.append(QString());
        lines.append(tr("Requested destination"));
        lines.append(review.Job.Destination);
    }
    report->setPlainText(lines.join('\n'));
    body->addWidget(report, 1);
    copy->setEnabled(!report->toPlainText().isEmpty());
    connect(copy, &QPushButton::clicked, this, [report] {
        const auto text = report->toPlainText();
        if (!text.isEmpty())
            QApplication::clipboard()->setText(text);
    });
    connect(actions, &QDialogButtonBox::rejected, this, &QDialog::reject);
    resize(600, 380);
}
