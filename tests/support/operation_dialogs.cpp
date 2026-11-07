#include "support/operation_dialogs.h"
#include "7zip/UI/FileManager/ProgressDialog2Res.h"
#include "dialogs/compress/createarchivedialog.h"
#include "dialogs/extract/ExtractDialog.h"
#include "ui/lifecycle/qttaskpresentation.h"
#include "ui/support/layout_inspection.h"
#include <QApplication>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QTimer>

// Public widget interactions in the fixture process only. Production 7zG owns
// these same source dialogs; no automation flags are added to its entrypoint.
void DriveOperationDialogs() {
    const QString picker = qEnvironmentVariable("SEVENZIP_OPERATION_BROWSE_HOLD");
    if (!picker.isEmpty() && QFileInfo::exists(picker))
        for (auto *widget : QApplication::topLevelWidgets())
            if (auto *dialog = qobject_cast<QFileDialog *>(widget))
                dialog->reject();
    const QString report = qEnvironmentVariable("SEVENZIP_OPERATION_UI_REPORT");
    if (!report.isEmpty()) {
        QJsonArray widgets;
        for (auto *widget : QApplication::topLevelWidgets()) {
            if (!widget->isVisible())
                continue;
            QJsonArray buttons;
            for (auto *button : widget->findChildren<QPushButton *>())
                buttons.append(QJsonObject{{"name", button->objectName()},
                                           {"text", button->text()},
                                           {"enabled", button->isEnabled()},
                                           {"visible", button->isVisible()}});
            widgets.append(QJsonObject{{"class", widget->metaObject()->className()},
                                       {"name", widget->objectName()},
                                       {"title", widget->windowTitle()},
                                       {"buttons", buttons}});
        }
        QSaveFile file(report);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(QJsonDocument(widgets).toJson());
            file.commit();
        }
    }
    for (auto *widget : QApplication::topLevelWidgets()) {
        if (!widget->isVisible())
            continue;
        if (auto *modal = QApplication::activeModalWidget(); modal && modal != widget)
            continue;
        const QString windowReport = qEnvironmentVariable("SEVENZIP_OPERATION_WINDOW_REPORT");
        const bool draft =
            qobject_cast<CExtractDialog *>(widget) || qobject_cast<CreateArchiveDialog *>(widget);
        const bool hashResult = widget->objectName() == "checksumOperationResult";
        if (!windowReport.isEmpty() && (draft || hashResult)) {
            if (draft && !picker.isEmpty() && QFileInfo::exists(picker + ".start") &&
                !widget->property("fixtureBrowseStarted").toBool()) {
                widget->setProperty("fixtureBrowseStarted", true);
                if (auto *browse = widget->findChild<QPushButton *>("extractBrowseButton"))
                    QTimer::singleShot(0, browse, &QPushButton::click);
            }
            const auto text = qEnvironmentVariable("SEVENZIP_OPERATION_DRAFT_TEXT");
            if (draft && !text.isEmpty() && !widget->property("fixtureDraftText").toBool()) {
                widget->setProperty("fixtureDraftText", true);
                if (auto *field = widget->findChild<QComboBox *>("extractDestination"))
                    field->setEditText(text);
                if (auto *field = widget->findChild<QLineEdit *>("archiveName"))
                    field->setText(text);
            }
            QString draftValue;
            if (auto *field = widget->findChild<QComboBox *>("extractDestination"))
                draftValue = field->currentText();
            if (auto *field = widget->findChild<QLineEdit *>("archiveName"))
                draftValue = field->text();
            if (picker.isEmpty() && !widget->property("fixtureMinimized").toBool()) {
                widget->setProperty("fixtureMinimized", true);
                widget->showMinimized();
            }
            QSaveFile file(windowReport);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(QJsonDocument(QJsonObject{{"minimized", widget->isMinimized()},
                                                     {"name", widget->objectName()},
                                                     {"draftValue", draftValue}})
                               .toJson());
                file.commit();
            }
            if (QFileInfo::exists(qEnvironmentVariable("SEVENZIP_OPERATION_RELEASE_WINDOW"))) {
                auto *dialog = qobject_cast<QDialog *>(widget);
                if (draft)
                    dialog->reject();
                else
                    dialog->accept();
            }
            continue;
        }
        if (auto *extract = qobject_cast<CExtractDialog *>(widget)) {
            const auto report = qEnvironmentVariable("SEVENZIP_OPERATION_DIALOG_REPORT");
            if (!report.isEmpty()) {
                QSaveFile file(report);
                if (file.open(QIODevice::WriteOnly)) {
                    file.write(
                        QJsonDocument(QJsonObject{{"title", extract->windowTitle()}}).toJson());
                    file.commit();
                }
                const auto tag = qEnvironmentVariable("SEVENZIP_OPERATION_DIALOG_TAG");
                if (!tag.isEmpty())
                    for (const QSize size : {QSize(700, 390), QSize(600, 340)}) {
                        extract->resize(size);
                        QApplication::processEvents();
                        UiInspection::writeReport(*extract,
                                                  tag + '-' + QString::number(size.width()));
                    }
            }
            const auto decision = qEnvironmentVariable("SEVENZIP_OPERATION_EXTRACT_DECISION");
            if (decision == "cancel") {
                QTimer::singleShot(0, extract, &QDialog::reject);
            } else if (decision == "accept") {
                const QString destination = qEnvironmentVariable("SEVENZIP_OPERATION_DESTINATION");
                if (!destination.isEmpty()) {
                    extract->findChild<QCheckBox *>("extractNameEnable")->setChecked(false);
                    extract->findChild<QComboBox *>("extractDestination")->setEditText(destination);
                }
                QTimer::singleShot(0, extract, &QDialog::accept);
            }
        } else if (auto *create = qobject_cast<CreateArchiveDialog *>(widget)) {
            const QString nameReport =
                qEnvironmentVariable("SEVENZIP_OPERATION_CREATE_NAME_REPORT");
            if (!nameReport.isEmpty()) {
                QSaveFile file(nameReport);
                if (file.open(QIODevice::WriteOnly)) {
                    file.write(
                        QJsonDocument(
                            QJsonObject{
                                {"name", create->findChild<QLineEdit *>("archiveName")->text()},
                                {"format",
                                 create->findChild<QComboBox *>("archiveFormat")->currentText()}})
                            .toJson());
                    file.commit();
                }
            }
            const QString decision = qEnvironmentVariable("SEVENZIP_OPERATION_CREATE_DECISION");
            if (decision == "cancel")
                QTimer::singleShot(0, create, &QDialog::reject);
            else if (decision == "accept") {
                const QString format = qEnvironmentVariable("SEVENZIP_OPERATION_CREATE_FORMAT");
                if (!format.isEmpty()) {
                    auto *selector = create->findChild<QComboBox *>("archiveFormat");
                    selector->setCurrentIndex(selector->findText(format, Qt::MatchFixedString));
                }
                const QString target = qEnvironmentVariable("SEVENZIP_OPERATION_CREATE_TARGET");
                if (!target.isEmpty()) {
                    create->findChild<QLineEdit *>("archiveFolder")
                        ->setText(QFileInfo(target).absolutePath());
                    create->findChild<QLineEdit *>("archiveName")
                        ->setText(QFileInfo(target).fileName());
                }
                const QString password = qEnvironmentVariable("SEVENZIP_OPERATION_CREATE_PASSWORD");
                if (!password.isEmpty()) {
                    create->findChild<QLineEdit *>("archivePassword")->setText(password);
                    create->findChild<QLineEdit *>("archivePasswordConfirmation")
                        ->setText(password);
                }
                const QString volume = qEnvironmentVariable("SEVENZIP_OPERATION_VOLUME");
                if (!volume.isEmpty())
                    create->findChild<QComboBox *>("archiveVolumeSize")->setEditText(volume);
                auto *ok = create->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
                QTimer::singleShot(0, ok, &QPushButton::click);
            }
        } else if (hashResult) {
            auto *hash = qobject_cast<QDialog *>(widget);
            const QString report = qEnvironmentVariable("SEVENZIP_OPERATION_HASH_REPORT");
            if (!report.isEmpty()) {
                const auto *list = hash->findChild<QPlainTextEdit *>("taskResultReport");
                QJsonObject result{{"text", list->toPlainText()}};
                const QString tag = qEnvironmentVariable("SEVENZIP_OPERATION_HASH_UI_TAG");
                if (!tag.isEmpty()) {
                    bool geometry = true;
                    const auto *buttons = hash->findChild<QDialogButtonBox *>("taskResultActions");
                    for (int width : {540, 420}) {
                        hash->resize(width, 360);
                        QApplication::processEvents();
                        geometry &=
                            UiInspection::fullyVisible(*list, *hash) &&
                            UiInspection::fullyVisible(*buttons, *hash) &&
                            UiInspection::separate(*list, *buttons, *hash) &&
                            UiInspection::writeReport(*hash, tag + '-' + QString::number(width));
                    }
                    result.insert("geometry", geometry);
                }
                QSaveFile file(report);
                if (file.open(QIODevice::WriteOnly)) {
                    file.write(QJsonDocument(result).toJson());
                    file.commit();
                }
            }
            QTimer::singleShot(0, hash, &QDialog::accept);
        } else if (widget->objectName() == "passwordDialog") {
            auto *dialog = qobject_cast<QDialog *>(widget);
            if (qEnvironmentVariableIsSet("SEVENZIP_OPERATION_CANCEL_PASSWORD"))
                QTimer::singleShot(0, dialog, &QDialog::reject);
            else if (qEnvironmentVariableIsSet("SEVENZIP_OPERATION_PASSWORD")) {
                dialog->findChild<QLineEdit *>("passwordEdit")
                    ->setText(qEnvironmentVariable("SEVENZIP_OPERATION_PASSWORD"));
                QTimer::singleShot(0, dialog, &QDialog::accept);
            }
        } else if (widget->objectName() == "overwriteDialog") {
            const auto button = qEnvironmentVariable("SEVENZIP_OPERATION_OVERWRITE");
            if (auto *choice = widget->findChild<QPushButton *>(button))
                QTimer::singleShot(0, choice, &QPushButton::click);
        } else if (auto *message = qobject_cast<QMessageBox *>(widget)) {
            QTimer::singleShot(0, message, [message] {
                if (auto *ok = message->button(QMessageBox::Ok))
                    ok->click();
                else if (qEnvironmentVariableIsSet("SEVENZIP_OPERATION_CANCEL_HASH") &&
                         message->button(QMessageBox::Yes))
                    message->button(QMessageBox::Yes)->click();
                else
                    message->accept();
            });
        } else if (widget->objectName() == "progressDialog") {
            auto *close = widget->findChild<QPushButton *>("IDCANCEL");
            if (close && close->property("languageID").toUInt() == IDS_CLOSE) {
                const QString review = qEnvironmentVariable("SEVENZIP_OPERATION_REVIEW_BARRIER");
                if (!review.isEmpty()) {
                    QSaveFile ready(review + ".ready");
                    if (ready.open(QIODevice::WriteOnly)) {
                        ready.write("ready");
                        ready.commit();
                    }
                    if (!QFileInfo::exists(review))
                        return;
                }
                QTimer::singleShot(0, close, &QPushButton::click);
            } else if (close && qEnvironmentVariableIsSet("SEVENZIP_OPERATION_CANCEL_HASH"))
                QTimer::singleShot(0, close, &QPushButton::click);
        }
        // Let the source slot unwind before inspecting another modal. A Close
        // underneath a final-message box starts a different cancellation dialog.
        return;
    }
}
