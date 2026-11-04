#include "mount/qtmountextractionprompt.h"
#include "ui/dialogs/qtdialoglayout.h"
#include <QDialogButtonBox>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>

QtMountExtractionPrompt::QtMountExtractionPrompt(const QString &source, const QString &path,
                                                 quint64 size, ArchiveMountDecisionPtr decision,
                                                 QWidget *parent, bool unverified)
    : QDialog(parent), decision_(std::move(decision)) {
    setObjectName("mountExtractionPrompt");
    setWindowTitle(unverified ? tr("Use unverified extracted data")
                              : tr("Extract files for this mount"));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    auto *layout = QtCreateDialogLayout(*this, *buttons);
    const auto addText = [this, layout](const QString &text) {
        auto *label = new QtWrappingLabel(text, this);
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        layout->addWidget(label);
    };
    addText(unverified ? tr("The reader reported damaged or incomplete data. Use the readable "
                            "output for this mount?")
                       : tr("Allow files from this mount to be extracted into the temporary cache "
                            "when they are read?"));
    addText(
        (unverified
             ? tr("%1\n%2\nAvailable data: %3\n\nData may be incomplete or have checksum errors. "
                  "This choice does not repair or verify it. The source remains read-only.")
             : tr("%1\nFirst requested file: %2\nSize: %3\n\nThis choice applies to all files in "
                  "this mount, including files extracted again after cache eviction. Cancel "
                  "disables extraction until the folder is mounted again. The source remains "
                  "read-only."))
            .arg(source, path, QLocale().formattedDataSize(qint64(size))));
    auto *extract =
        buttons->addButton(unverified ? tr("Use Unverified Data") : tr("Allow Extraction"),
                           QDialogButtonBox::AcceptRole);
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
    resize(520, sizeHint().height());
}
QtMountExtractionPrompt::~QtMountExtractionPrompt() { decision_->answer(false); }
