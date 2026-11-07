#include "Common/StringConvert.h"
#include "dialogs/copy/CopyDialog.h"
#include "filemanager/window/qtexternalfiletask_p.h"
#include "filemanager/window/qtlocalfiletask.h"
#include "localization/qtlangbuttonbox.h"
#include "localization/qtlangcontrols.h"
#include "platform/temp/qtownedtempidentity.h"
#include "platform/temp/qtownedtempregistry.h"
#include "ui/controls/qtmetadatarows.h"
#include "ui/controls/qtsourcesummary.h"
#include "ui/dialogs/qtdialoglayout.h"
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QStyle>
#include <QVBoxLayout>

void QtExternalFileTask::Refresh() {
    if (state_->status) {
        const char *text =
            state_->publishing
                ? QT_TR_NOOP("The archive is being updated. The temporary copy is in use.")
            : state_->message ? state_->message
            : state_->stopped ? QT_TR_NOOP("Observation stopped. The external program may still be "
                                           "running. The temporary copy was kept.")
            : state_->known   ? QT_TR_NOOP("The external program is still running.")
            : state_->observation == QtExternalObservation::Completed
                ? QT_TR_NOOP("The external program finished. The temporary copy was kept.")
            : state_->observation == QtExternalObservation::Failed
                ? QT_TR_NOOP("Process observation failed. The temporary copy is protected.")
            : !state_->target && !state_->known && !state_->published
                ? QT_TR_NOOP(
                      "This retained copy can be saved separately. Its source cannot be updated.")
                : QT_TR_NOOP("The external program's completion is unknown. The temporary copy is "
                             "protected.");
        QtBindLocalText(state_->status, "QtExternalFileTask", text);
    }
    if (state_->confirmed)
        state_->confirmed->setVisible(state_->target && !state_->known && !state_->published &&
                                      state_->observation != QtExternalObservation::Completed);
    if (state_->check) {
        state_->check->setVisible(state_->target.has_value());
        state_->check->setEnabled(state_->target && !state_->stopped &&
                                  !state_->launcher.Exiting() && !state_->known &&
                                  !state_->updating && !state_->exports && !state_->published &&
                                  (state_->observation == QtExternalObservation::Completed ||
                                   (state_->confirmed && state_->confirmed->isChecked())));
    }
    if (state_->retainedText) {
        QStringList names;
        for (const auto &path : state_->retained)
            names.append(QFileInfo(path).fileName());
        state_->retainedText->setText(names.join('\n'));
        state_->retainedText->setToolTip(state_->retained.join('\n'));
        state_->retainedRows->setVisible(!state_->retained.isEmpty());
    }
    if (state_->save)
        state_->save->setEnabled(!state_->updating && !state_->exports);
}

void QtExternalFileTask::Show() {
    if (!state_->dialog) {
        auto *dialog = new QDialog;
        state_->dialog = dialog;
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setObjectName("externalFileDetails");
        QtBindLocalText(
            dialog, "QtExternalFileTask",
            state_->stopped && !state_->target
                ? QT_TRANSLATE_NOOP("QtExternalFileTask", "Retained file copy")
                : QT_TRANSLATE_NOOP("QtExternalFileTask", "File in external application"));
        auto *actions = new QtLangButtonBox(QDialogButtonBox::Close, dialog);
        auto *save = actions->addButton(QString(), QDialogButtonBox::ActionRole);
        save->setObjectName("externalFileSaveCopy");
        state_->save = save;
        QtBindLocalText(save, "QtExternalFileTask",
                        QT_TRANSLATE_NOOP("QtExternalFileTask", "Save copy…"));
        auto *check = actions->addButton(QString(), QDialogButtonBox::ActionRole);
        check->setObjectName("externalFileCheck");
        state_->check = check;
        QtBindLocalText(check, "QtExternalFileTask",
                        QT_TRANSLATE_NOOP("QtExternalFileTask", "Check changes"));
        auto *body = QtCreateDialogLayout(*dialog, *actions, QtDialogContent::Scrollable);
        auto *source =
            new QtSourceSummary(qApp->style()->standardIcon(QStyle::SP_FileIcon),
                                QFileInfo(ToQString(state_->file.RelPath)).fileName(),
                                state_->prefix + '\n' + ToQString(state_->file.RelPath), dialog);
        source->setObjectName("externalFileIdentity");
        body->addWidget(source);
        auto *metadata = new QtMetadataRows(dialog);
        const auto path = ToQString(fs2us(state_->file.FilePath));
        auto *copy = metadata->AddRow("QtExternalFileTask",
                                      QT_TRANSLATE_NOOP("QtExternalFileTask", "Temporary copy"),
                                      QFileInfo(path).fileName(), "externalFilePath");
        copy->setToolTip(path);
        body->addWidget(metadata);
        auto *retained = new QtMetadataRows(dialog);
        state_->retainedRows = retained;
        state_->retainedText =
            retained->AddRow("QtExternalFileTask",
                             QT_TRANSLATE_NOOP("QtExternalFileTask", "Retained archive copies"), {},
                             "externalFileRetained");
        body->addWidget(retained);
        auto *status = new QtWrappingLabel(dialog);
        status->setTextFormat(Qt::PlainText);
        status->setWordWrap(true);
        state_->status = status;
        status->setObjectName("externalFileStatus");
        body->addWidget(status);
        auto *confirmed = new QCheckBox(dialog);
        confirmed->setObjectName("externalFileConfirmedClosed");
        state_->confirmed = confirmed;
        QtBindLocalText(confirmed, "QtExternalFileTask",
                        QT_TRANSLATE_NOOP("QtExternalFileTask",
                                          "I saved and closed the file in the external program"));
        body->addWidget(confirmed);
        connect(confirmed, &QCheckBox::toggled, this, &QtExternalFileTask::Refresh);
        connect(check, &QPushButton::clicked, this, [this] { CheckChanges(); });
        connect(save, &QPushButton::clicked, this, [this, dialog] { SaveCopy(dialog); });
        connect(actions, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
        dialog->resize(520, 420);
    }
    Refresh();
    if (state_->dialog->isMinimized())
        state_->dialog->showNormal();
    state_->dialog->show();
    state_->dialog->raise();
    state_->dialog->activateWindow();
}

void QtExternalFileTask::SaveCopy(QWidget *parent, bool fromUpdate) {
    if ((state_->updating && !fromUpdate) || state_->exports)
        return;
    const auto owner = shared_from_this();
    auto lease = InputLease();
    if (!lease)
        return;
    const QString source = ToQString(fs2us(state_->file.FilePath));
    const auto available = [&] {
        return QtOwnedTempMatchesIdentity(QFileInfo(source).absolutePath(),
                                          state_->directoryIdentity) &&
               !QtOwnedTempIdentity(source).isEmpty() && QFileInfo(source).isFile();
    };
    if (!available()) {
        state_->message =
            QT_TR_NOOP("The temporary file is unavailable. The archive was not updated.");
        Refresh();
        return;
    }
    QString destination = QDir::homePath();
    if (state_->target) {
        const QFileInfo folder(QFileInfo(state_->target->physicalPath()).absolutePath());
        if (folder.isDir() && folder.isReadable())
            destination = folder.absoluteFilePath();
    }
    CCopyDialog dialog(false, destination, {source}, parent);
    const QFileInfo sourceInfo(source);
    dialog.SetSourceDetails(sourceInfo.absolutePath(),
                            sourceInfo.exists() && sourceInfo.size() >= 0
                                ? std::optional<quint64>(sourceInfo.size())
                                : std::nullopt);
    dialog.ConfigureDestination(
        {sourceInfo.absolutePath(), sourceInfo.fileName(), {}, false, [](const QString &) {
             return true;
         }});
    const bool alreadyStopped = state_->launcher.Exiting();
    state_->question = &dialog;
    const int answer = dialog.exec();
    state_->question = nullptr;
    if (answer != QDialog::Accepted || (!alreadyStopped && state_->launcher.Exiting()))
        return;
    if (!available()) {
        state_->message =
            QT_TR_NOOP("The temporary file is unavailable. The archive was not updated.");
        Refresh();
        return;
    }
    destination = dialog.destination();
    const bool direct = dialog.usesDestinationName() ||
                        (!destination.endsWith('/') && !QFileInfo(destination).isDir());
    auto *worker = new FileTransferOperation(
        {ToQString(fs2us(state_->file.FilePath))}, destination, false, nullptr, {},
        direct ? FileTransferOperation::DestinationKind::DirectPath
               : FileTransferOperation::DestinationKind::Directory);
    auto *task = new QtLocalFileTask(*worker, tr("Save copy"), destination);
    ++state_->exports;
    Refresh();
    connect(task, &QtLocalFileTask::transferCompleted, this,
            [owner, lease = std::move(lease)](bool) mutable {
                lease.reset();
                --owner->state_->exports;
                owner->Refresh();
            });
    task->Start();
}

int QtExternalFileTask::AskForUpdate() {
    QDialog question;
    state_->question = &question;
    question.setObjectName("externalUpdatePrompt");
    QtBindLocalText(&question, "QtExternalFileTask",
                    QT_TRANSLATE_NOOP("QtExternalFileTask", "Save changes to archive?"));
    auto *actions = new QtLangButtonBox(QDialogButtonBox::NoButton, &question);
    auto *later = actions->addButton(QString(), QDialogButtonBox::RejectRole);
    later->setObjectName("externalUpdateLater");
    QtBindLocalText(later, "QtExternalFileTask", QT_TRANSLATE_NOOP("QtExternalFileTask", "Later"));
    auto *update = actions->addButton(QString(), QDialogButtonBox::AcceptRole);
    update->setObjectName("externalUpdateCommit");
    QtBindLocalText(update, "QtExternalFileTask",
                    QT_TRANSLATE_NOOP("QtExternalFileTask", "Update archive"));
    auto *save = actions->addButton(QString(), QDialogButtonBox::ActionRole);
    save->setObjectName("externalUpdateSaveCopy");
    QtBindLocalText(save, "QtExternalFileTask",
                    QT_TRANSLATE_NOOP("QtExternalFileTask", "Save copy…"));
    update->setAutoDefault(false);
    save->setAutoDefault(false);
    later->setDefault(true);
    later->setFocus();
    auto *body = QtCreateDialogLayout(question, *actions);
    body->addWidget(new QtSourceSummary(qApp->style()->standardIcon(QStyle::SP_FileIcon),
                                        QFileInfo(ToQString(state_->file.RelPath)).fileName(),
                                        state_->prefix + '\n' + ToQString(state_->file.RelPath),
                                        &question));
    connect(update, &QPushButton::clicked, &question, &QDialog::accept);
    connect(save, &QPushButton::clicked, &question, [&question] { question.done(2); });
    connect(actions, &QDialogButtonBox::rejected, &question, &QDialog::reject);
    question.resize(480, question.sizeHint().height());
    const int decision = question.exec();
    state_->question = nullptr;
    return decision;
}
