// Adapted from 7-Zip AboutDialog.cpp (0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Original versions and attribution; project-owned Qt presentation.
#include "dialogs/about/AboutDialog.h"
#include "7zip/MyVersion.h"
#include "localization/qtlangcontrols.h"
#include "ui/controls/qtsourcesummary.h"
#include "ui/dialogs/qtdialoglayout.h"
#include <QEvent>
#include <QLabel>
#include <utility>

CAboutDialog::CAboutDialog(QWidget *parent) : QDialog(parent) {
    setObjectName("aboutDialog");
    QtUseContextDialogModality(*this);
    BuildPresentation();
    OnInit();
    ensurePolished();
    resize(QSize(520, sizeHint().height()).expandedTo(minimumSizeHint()));
    initialPresentationSize_ = size();
}

void CAboutDialog::showEvent(QShowEvent *event) {
    // The child layouts are ready, but Qt hasn't mapped the native window yet.
    if (std::exchange(initialPresentation_, false) && size() == initialPresentationSize_)
        QtFitScrollableDialog(*this);
    QDialog::showEvent(event);
}

QString CAboutDialog::ValueOrUnknown(const QString &value) const {
    return value.isEmpty() ? QCoreApplication::translate("AboutDialog", "Unknown") : value;
}

bool CAboutDialog::OnInit() {
    identity_->SetSource("Sevenzip", {});
    identity_->SetDescription(
        QCoreApplication::translate("AboutDialog", "Version %1").arg(SEVENZIP_DESKTOP_VERSION));
    QtBindLocalText(this, "AboutDialog", QT_TRANSLATE_NOOP("AboutDialog", "About Sevenzip"));
    QtBindLocalText(findChild<QLabel *>(QString("aboutText%1").arg(IDT_ABOUT_VERSION)),
                    "AboutDialog", QT_TRANSLATE_NOOP("AboutDialog", "Based on 7-Zip %1"), false,
                    {MY_VERSION_CPU});
    QtBindLocalText(findChild<QLabel *>(QString("aboutText%1").arg(IDT_ABOUT_DATE)), "AboutDialog",
                    QT_TRANSLATE_NOOP("AboutDialog", "7-Zip release: %1"), false, {MY_DATE});
    for (const auto &detail : buildDetails_)
        detail.label->setText(ValueOrUnknown(detail.value));
    return true;
}

void CAboutDialog::changeEvent(QEvent *event) {
    QDialog::changeEvent(event);
    if (event->type() == QEvent::LanguageChange && identity_) {
        identity_->SetDescription(
            QCoreApplication::translate("AboutDialog", "Version %1").arg(SEVENZIP_DESKTOP_VERSION));
        for (const auto &detail : buildDetails_)
            detail.label->setText(ValueOrUnknown(detail.value));
    }
}
