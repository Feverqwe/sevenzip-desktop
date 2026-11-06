// Adapted from 7-Zip AboutDialog.cpp (0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Original versions, attribution and homepage command; project-owned Qt presentation.
#include "dialogs/about/AboutDialog.h"
#include "7zip/MyVersion.h"
#include "archive/codecs/LoadCodecs.h"
#include "dialogs/edit/EditDialog.h"
#include "dialogs/licenses/qtlicensesdialog.h"
#include "localization/qtlangcontrols.h"
#include "settings/ViewSettings.h"
#include "ui/controls/qtsourcesummary.h"
#include "ui/dialogs/qtdialoglayout.h"
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QEvent>
#include <QLabel>
#include <QPushButton>
#include <QTimer>

CAboutDialog::CAboutDialog(QWidget *parent, const CCodecs *codecs, OpenUrl openUrl)
    : QDialog(parent), openUrl_(std::move(openUrl)), codecs_(codecs) {
    setObjectName("aboutDialog");
    QtUseContextDialogModality(*this);
    if (!openUrl_)
        openUrl_ = [](const QUrl &url) { return QDesktopServices::openUrl(url); };
    BuildPresentation();
    OnInit();
    resize(QSize(520, sizeHint().height()).expandedTo(minimumSizeHint()));
    QtFitScrollableDialog(*this);
    initialPresentationSize_ = size();
}

void CAboutDialog::showEvent(QShowEvent *event) {
    QDialog::showEvent(event);
    if (!initialPresentation_)
        return;
    initialPresentation_ = false;
    QTimer::singleShot(0, this, [this] {
        // Fit after native fonts and wrapped labels have their final widths.
        // An explicit caller resize still takes precedence.
        if (isVisible() && size() == initialPresentationSize_)
            QtFitScrollableDialog(*this);
    });
}

QString CAboutDialog::ValueOrUnknown(const QString &value) const {
    return value.isEmpty() ? QCoreApplication::translate("AboutDialog", "Unknown") : value;
}

bool CAboutDialog::OnInit() {
    identity_->SetSource(
        "Sevenzip",
        QCoreApplication::translate("AboutDialog", "Version %1").arg(SEVENZIP_DESKTOP_VERSION));
    QtBindLocalText(this, "AboutDialog", QT_TRANSLATE_NOOP("AboutDialog", "About Sevenzip"));
    QtBindLocalText(findChild<QLabel *>(QString("aboutText%1").arg(IDT_ABOUT_VERSION)),
                    "AboutDialog", QT_TRANSLATE_NOOP("AboutDialog", "Based on 7-Zip %1"), false,
                    {MY_VERSION_CPU});
    QtBindLocalText(findChild<QLabel *>(QString("aboutText%1").arg(IDT_ABOUT_DATE)), "AboutDialog",
                    QT_TRANSLATE_NOOP("AboutDialog", "7-Zip release: %1"), false, {MY_DATE});
    if (codecs_) {
        UString message;
        codecs_->GetCodecsErrorMessage(message);
        codecMessage_ = ToQString(message);
    }
    codecNotice_->setVisible(!codecMessage_.isEmpty());
    for (const auto &detail : buildDetails_)
        detail.label->setText(ValueOrUnknown(detail.value));
    return true;
}

void CAboutDialog::changeEvent(QEvent *event) {
    QDialog::changeEvent(event);
    if (event->type() == QEvent::LanguageChange && codecNotice_) {
        identity_->SetSource(
            "Sevenzip",
            QCoreApplication::translate("AboutDialog", "Version %1").arg(SEVENZIP_DESKTOP_VERSION));
        for (const auto &detail : buildDetails_)
            detail.label->setText(ValueOrUnknown(detail.value));
    }
}

void CAboutDialog::ShowLicenses() {
    QtLicensesDialog dialog(this);
    dialog.exec();
    licenseButton_->setFocus();
}

void CAboutDialog::ShowDiagnostics() {
    CEditDialog dialog(this);
    QtUseContextDialogModality(dialog);
    dialog.Title =
        ToUString(QCoreApplication::translate("AboutDialog", "Archive module diagnostics"));
    dialog.Text = ToUString(codecMessage_);
    dialog.OnInit();
    dialog.exec();
}

QString CAboutDialog::Information() const {
    QStringList lines{identity_->accessibleName(), identity_->accessibleDescription(),
                      findChild<QLabel *>(QString("aboutText%1").arg(IDT_ABOUT_VERSION))->text(),
                      findChild<QLabel *>(QString("aboutText%1").arg(IDT_ABOUT_DATE))->text()};
    for (const auto &detail : buildDetails_)
        lines.append(QCoreApplication::translate("AboutDialog", detail.caption) + ": " +
                     ValueOrUnknown(detail.value));
    if (!codecMessage_.isEmpty())
        lines.append(codecMessage_);
    return lines.join('\n');
}

void CAboutDialog::OpenExternal(const QUrl &url) {
    if (openUrl_(url)) {
        failedUrl_.clear();
        urlNotice_->hide();
        return;
    }
    failedUrl_ = url.toString();
    QtBindLocalText(urlMessage_, "AboutDialog",
                    QT_TRANSLATE_NOOP("AboutDialog", "Could not open the browser. Address: %1"),
                    false, {failedUrl_});
    urlNotice_->show();
}

bool CAboutDialog::OnButtonClicked(unsigned buttonID, QWidget *buttonHWND) {
    Q_UNUSED(buttonHWND);
    if (buttonID != IDB_ABOUT_HOMEPAGE)
        return false;
    OpenExternal(QUrl("https://www.7-zip.org/"));
    return true;
}
