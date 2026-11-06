// Qt presentation of the About dialog; native provenance is in AboutDialog.cpp.
#include "7zip/MyVersion.h"
#include "dialogs/about/AboutDialog.h"
#include "localization/qtlangbuttonbox.h"
#include "localization/qtlangcontrols.h"
#include "ui/controls/qtdisclosure.h"
#include "ui/controls/qtmetadatarows.h"
#include "ui/controls/qtresponsiverow.h"
#include "ui/controls/qtsourcesummary.h"
#include "ui/dialogs/qtdialoglayout.h"
#include "ui/theme/qtcommandicons.h"
#include "ui/theme/qtsemanticcolors.h"
#include <QApplication>
#include <QClipboard>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSysInfo>
#include <QToolButton>
#include <QVBoxLayout>

void CAboutDialog::BuildPresentation() {
    auto *buttons = new QtLangButtonBox(QDialogButtonBox::Ok, this);
    buttons->setObjectName("aboutActions");
    buttons->SetAcceptText("DialogRedesign", QT_TRANSLATE_NOOP("DialogRedesign", "Close"));
    auto *copy = buttons->addButton(QString(), QDialogButtonBox::ActionRole);
    copy->setObjectName("aboutCopyInformation");
    copy->setAutoDefault(false);
    QtBindLocalText(copy, "AboutDialog", QT_TRANSLATE_NOOP("AboutDialog", "Copy information"));
    connect(copy, &QPushButton::clicked, this,
            [this] { QApplication::clipboard()->setText(Information()); });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    auto *layout = QtCreateDialogLayout(*this, *buttons, QtDialogContent::Scrollable);
    layout->setSpacing(12);
    BuildIdentity(*layout);
    BuildUpstream(*layout);
    BuildDetails(*layout);
    BuildUrlNotice(*layout);
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
            [this] { OpenExternal(QUrl("https://github.com/Feverqwe/7zip-qt-port")); });
    licenseButton_ = new QPushButton(this);
    licenseButton_->setObjectName("aboutLicense");
    QtBindLocalText(licenseButton_, "AboutDialog",
                    QT_TRANSLATE_NOOP("AboutDialog", "Licenses and notices…"));
    connect(licenseButton_, &QPushButton::clicked, this, &CAboutDialog::ShowLicenses);
    layout.addWidget(new QtResponsiveRow({project, licenseButton_}, this));
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
    auto *homepage = new QPushButton(upstream);
    QtBindLocalText(homepage, "AboutDialog", QT_TRANSLATE_NOOP("AboutDialog", "7-Zip website"));
    homepage->setObjectName("aboutHomepage");
    homepage->setProperty("sourceControlID", IDB_ABOUT_HOMEPAGE);
    connect(homepage, &QPushButton::clicked, this,
            [this, homepage] { OnButtonClicked(IDB_ABOUT_HOMEPAGE, homepage); });
    content->addWidget(homepage, 0, Qt::AlignLeft);
    layout.addWidget(upstream);
}

void CAboutDialog::BuildDetails(QVBoxLayout &layout) {
    codecNotice_ = new QWidget(this);
    codecNotice_->setObjectName("aboutCodecNotice");
    auto *content = new QVBoxLayout(codecNotice_);
    content->setContentsMargins(0, 0, 0, 0);
    auto *message = new QtWrappingLabel(codecNotice_);
    message->setTextFormat(Qt::PlainText);
    message->setWordWrap(true);
    QtBindLocalText(message, "AboutDialog",
                    QT_TRANSLATE_NOOP("AboutDialog", "Some archive modules could not be loaded."));
    content->addWidget(message);
    auto *diagnostics = new QPushButton(codecNotice_);
    diagnostics->setObjectName("aboutCodecDetails");
    QtBindLocalText(diagnostics, "AboutDialog", QT_TRANSLATE_NOOP("AboutDialog", "Details…"));
    connect(diagnostics, &QPushButton::clicked, this, &CAboutDialog::ShowDiagnostics);
    content->addWidget(diagnostics, 0, Qt::AlignLeft);
    layout.addWidget(codecNotice_);
    auto *details = new QtMetadataRows(this);
    const auto add = [&](const char *caption, const QString &value, const QString &name) {
        auto *label = details->AddRow("AboutDialog", caption, ValueOrUnknown(value), name);
        buildDetails_.append({caption, value, label});
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

void CAboutDialog::BuildUrlNotice(QVBoxLayout &layout) {
    urlNotice_ = new QWidget(this);
    urlNotice_->setObjectName("aboutUrlNotice");
    auto *content = new QVBoxLayout(urlNotice_);
    content->setContentsMargins(0, 0, 0, 0);
    urlMessage_ = new QtWrappingLabel(urlNotice_);
    urlMessage_->setObjectName("aboutUrlMessage");
    urlMessage_->setTextFormat(Qt::PlainText);
    urlMessage_->setWordWrap(true);
    urlMessage_->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    content->addWidget(urlMessage_);
    auto *copyUrl = new QPushButton(urlNotice_);
    copyUrl->setObjectName("aboutCopyUrl");
    QtBindLocalText(copyUrl, "AboutDialog", QT_TRANSLATE_NOOP("AboutDialog", "Copy address"));
    connect(copyUrl, &QPushButton::clicked, this,
            [this] { QApplication::clipboard()->setText(failedUrl_); });
    content->addWidget(copyUrl, 0, Qt::AlignLeft);
    layout.addWidget(urlNotice_);
    urlNotice_->hide();
}
