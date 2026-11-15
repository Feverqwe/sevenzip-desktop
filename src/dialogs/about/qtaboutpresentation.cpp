// Qt presentation of the About dialog; native provenance is in AboutDialog.cpp.
#include "7zip/MyVersion.h"
#include "dialogs/about/AboutDialog.h"
#include "localization/qtlangbuttonbox.h"
#include "localization/qtlangcontrols.h"
#include "ui/controls/qtdisclosure.h"
#include "ui/controls/qtmetadatarows.h"
#include "ui/controls/qtsourcesummary.h"
#include "ui/dialogs/qtdialoglayout.h"
#include "ui/theme/qtcommandicons.h"
#include "ui/theme/qtsemanticcolors.h"
#include <QDesktopServices>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QSysInfo>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

void CAboutDialog::BuildPresentation() {
    auto *buttons = new QtLangButtonBox(QDialogButtonBox::Ok, this);
    buttons->setObjectName("aboutActions");
    buttons->SetAcceptText("DialogRedesign", QT_TRANSLATE_NOOP("DialogRedesign", "Close"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    auto *layout = QtCreateDialogLayout(*this, *buttons, QtDialogContent::Scrollable);
    layout->setSpacing(12);
    BuildIdentity(*layout);
    BuildUpstream(*layout);
    BuildDetails(*layout);
    auto *close = buttons->button(QDialogButtonBox::Ok);
    close->setDefault(true);
    close->setFocus();
}

void CAboutDialog::BuildIdentity(QVBoxLayout &layout) {
    identity_ = new QtSourceSummary(QtCommandSymbol(QtCommandIcon::Archive), "Sevenzip", {}, this);
    identity_->setObjectName("aboutIdentity");
    identity_->SetIdentityPresentation();
    layout.addWidget(identity_);
    auto *description = new QtWrappingLabel(this);
    description->setObjectName("aboutDescription");
    description->setWordWrap(true);
    description->setTextFormat(Qt::PlainText);
    QtBindLocalText(
        description, "AboutDialog",
        QT_TRANSLATE_NOOP("AboutDialog", "An independent file manager based on 7‑Zip."));
    layout.addWidget(description);
    auto *project = new QPushButton(this);
    project->setObjectName("aboutProjectHomepage");
    QtBindLocalText(project, "AboutDialog", QT_TRANSLATE_NOOP("AboutDialog", "Project website"));
    connect(project, &QPushButton::clicked, this,
            [] { QDesktopServices::openUrl(QUrl("https://github.com/Feverqwe/7zip-qt-port")); });
    layout.addWidget(project, 0, Qt::AlignLeft);
}

void CAboutDialog::BuildUpstream(QVBoxLayout &layout) {
    auto *upstream = new QFrame(this);
    upstream->setObjectName("aboutUpstream");
    upstream->setAccessibleName("7-Zip");
    QtSemanticColors::styleSurface(*upstream, 10);
    auto *content = new QVBoxLayout(upstream);
    content->setContentsMargins(16, 16, 16, 16);
    for (unsigned id : {IDT_ABOUT_VERSION, IDT_ABOUT_DATE}) {
        auto *label = new QtWrappingLabel(upstream);
        label->setObjectName(QString("aboutText%1").arg(id));
        label->setProperty("sourceControlID", id);
        label->setWordWrap(true);
        label->setTextFormat(Qt::PlainText);
        if (id == IDT_ABOUT_VERSION) {
            QFont font = label->font();
            font.setBold(true);
            label->setFont(font);
        }
        content->addWidget(label);
    }
    auto *copyright = new QtWrappingLabel(MY_COPYRIGHT, upstream);
    copyright->setObjectName("aboutUpstreamCopyright");
    copyright->setTextFormat(Qt::PlainText);
    copyright->setWordWrap(true);
    content->insertWidget(2, copyright);
    layout.addWidget(upstream);
}

void CAboutDialog::BuildDetails(QVBoxLayout &layout) {
    auto *details = new QtMetadataRows(this);
    const auto add = [&](const char *caption, const QString &value, const QString &name) {
        auto *label = details->AddRow("AboutDialog", caption, ValueOrUnknown(value), name);
        buildDetails_.append({value, label});
    };
    add(QT_TRANSLATE_NOOP("AboutDialog", "Architecture"), QSysInfo::buildCpuArchitecture(),
        "aboutArchitecture");
    add(QT_TRANSLATE_NOOP("AboutDialog", "Qt version"), QString::fromLatin1(qVersion()),
        "aboutQtVersion");
    add(QT_TRANSLATE_NOOP("AboutDialog", "Revision"), {}, "aboutRevision");
    auto *section = new QtDisclosure(details, this);
    section->setObjectName("aboutBuildDetails");
    QtBindLocalText(section->button(), "AboutDialog",
                    QT_TRANSLATE_NOOP("AboutDialog", "Build information"));
    layout.addWidget(section);
}
