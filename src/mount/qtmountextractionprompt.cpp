#include "mount/qtmountextractionprompt.h"
#include "localization/qtlangbuttonbox.h"
#include "localization/qtlangcontrols.h"
#include "platform/locale/qtregionalsettings.h"
#include "ui/controls/qtmetadatarows.h"
#include "ui/controls/qtsourcesummary.h"
#include "ui/dialogs/qtdialoglayout.h"
#include "ui/formatting/qtregionalformat.h"
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

QtMountExtractionPrompt::QtMountExtractionPrompt(const QString &source, const QString &path,
                                                 quint64 size, ArchiveMountDecisionPtr decision,
                                                 QWidget *parent, bool unverified)
    : QDialog(parent), decision_(std::move(decision)) {
    setObjectName("mountExtractionPrompt");
    QtBindLocalText(this, "QtMountExtractionPrompt",
                    unverified ? QT_TR_NOOP("Use unverified extracted data")
                               : QT_TR_NOOP("Extract files for this mount"));
    auto *buttons = new QtLangButtonBox(QDialogButtonBox::Cancel, this);
    auto *layout = QtCreateDialogLayout(*this, *buttons, QtDialogContent::Scrollable);
    const auto addText = [this, layout](const char *text) {
        auto *label = new QtWrappingLabel(this);
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        QtBindLocalText(label, "QtMountExtractionPrompt", text);
        layout->addWidget(label);
    };
    addText(unverified
                ? QT_TR_NOOP("The reader reported damaged or incomplete data. Use the "
                             "readable output for this mount?")
                : QT_TR_NOOP("Allow files from this mount to be extracted into the temporary "
                             "cache when they are read?"));
    auto *summary = new QtSourceSummary(style()->standardIcon(QStyle::SP_FileIcon),
                                        QFileInfo(path).fileName(), source + '\n' + path, this);
    summary->setObjectName("mountExtractionSource");
    layout->addWidget(summary);
    auto *metadata = new QtMetadataRows(this);
    auto *amount = metadata->AddRow("QtMountExtractionPrompt",
                                    unverified ? QT_TR_NOOP("Available data") : QT_TR_NOOP("Size"),
                                    {}, "mountExtractionSize");
    const auto refreshSize = [amount, size] {
        amount->setText(QtRegionalFormat::bytes(size));
        amount->setToolTip(QtRegionalFormat::exactBytes(size));
    };
    refreshSize();
    QtRegionalSettings::Observe(*this, refreshSize);
    layout->addWidget(metadata);
    addText(unverified
                ? QT_TR_NOOP("Data may be incomplete or have checksum errors. This choice does "
                             "not repair or verify it. The source remains read-only.")
                : QT_TR_NOOP("This choice applies to all files in this mount, including files "
                             "extracted again after cache eviction. Cancel disables extraction "
                             "until the folder is mounted again. The source remains read-only."));
    auto *extract = buttons->addButton(QString(), QDialogButtonBox::AcceptRole);
    QtBindLocalText(extract, "QtMountExtractionPrompt",
                    unverified ? QT_TR_NOOP("Use Unverified Data")
                               : QT_TR_NOOP("Allow Extraction"));
    extract->setObjectName("extractForMountButton");
    extract->setAutoDefault(false);
    buttons->button(QDialogButtonBox::Cancel)->setDefault(true);
    QtUseContextDialogModality(*this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        decision_->answer(true);
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(this, &QDialog::finished, this, [this] { decision_->answer(false); });
    resize(520, 400);
    QtFitScrollableDialog(*this);
}
QtMountExtractionPrompt::~QtMountExtractionPrompt() { decision_->answer(false); }
