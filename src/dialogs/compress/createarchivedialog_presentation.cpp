#include "Windows/FileName.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "dialogs/browse/BrowseDialog.h"
#include "dialogs/compress/createarchivedialog.h"
#include "localization/qtlangbuttonbox.h"
#include "localization/qtlangcontrols.h"
#include "settings/ViewSettings.h"
#include "ui/dialogs/qtdialoglayout.h"
#include <QEvent>
#include <QProxyStyle>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>

namespace {
// Compact Add is a form: its scroll bars need a separate gutter, including
// when the native style normally flashes an auto-hiding overlay at startup.
class ArchiveFormScrollStyle final : public QProxyStyle {
  public:
    int styleHint(StyleHint hint, const QStyleOption *option, const QWidget *widget,
                  QStyleHintReturn *data) const override {
        if (hint == SH_ScrollBar_Transient)
            return false;
        return QProxyStyle::styleHint(hint, option, widget, data);
    }
    int pixelMetric(PixelMetric metric, const QStyleOption *option,
                    const QWidget *widget) const override {
        if (metric == PM_ScrollView_ScrollBarOverlap)
            return 0;
        return QProxyStyle::pixelMetric(metric, option, widget);
    }
};

class ArchiveFormScrollArea final : public QScrollArea {
  public:
    explicit ArchiveFormScrollArea(QWidget *parent) : QScrollArea(parent) {}

  protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        const bool result = QScrollArea::eventFilter(object, event);
        if (object == widget() && event->type() == QEvent::LayoutRequest) {
            // Native metrics and translated labels can settle after show.
            // Keep the gutter outside the form even when its minimum grows.
            setMinimumWidth(widget()->minimumSizeHint().width() +
                            verticalScrollBar()->sizeHint().width());
            if (window()->layout())
                window()->layout()->activate();
        }
        return result;
    }
};
} // namespace

QScrollArea *CreateArchiveDialog::createOptionsScrollArea(QWidget *parent) {
    auto *scroll = new ArchiveFormScrollArea(parent);
    scroll->setObjectName("archiveOptionsScrollArea");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *scrollStyle = new ArchiveFormScrollStyle;
    scrollStyle->setParent(scroll);
    scroll->verticalScrollBar()->setStyle(scrollStyle);
    scroll->horizontalScrollBar()->setStyle(scrollStyle);
    return scroll;
}

QPushButton *CreateArchiveDialog::createArchiveDestination(QVBoxLayout *windowLayout) {
    auto *archiveField = new QWidget(this);
    archiveField->setObjectName("archiveNameField");
    auto *fieldLayout = QtCreateDialogForm();
    archiveField->setLayout(fieldLayout);
    fieldLayout->setContentsMargins(0, 0, windowLayout->contentsMargins().left(), 0);
    archiveFolder_ = new QLineEdit(archiveField);
    archiveFolder_->setText(QDir::toNativeSeparators(archiveNameState_.DirPrefix));
    archiveFolder_->setObjectName("archiveFolder");
    connect(archiveFolder_, &QLineEdit::textChanged, this, [this](const QString &folder) {
        archiveNameState_.DirPrefix = QDir::fromNativeSeparators(folder);
    });
    auto *archiveRow = new QWidget(this);
    archiveRow->setObjectName("archivePathRow");
    auto *archiveLayout = new QHBoxLayout(archiveRow);
    archiveLayout->setContentsMargins(0, 0, 0, 0);
    archivePath_ = new QComboBox(this);
    archivePath_->setObjectName("archivePath");
    archivePath_->setEditable(true);
    archivePath_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    archivePath_->setMinimumContentsLength(20);
    archivePath_->setInsertPolicy(QComboBox::NoInsert);
    for (unsigned i = 0; i < m_RegistryInfo.ArcPaths.Size() && i < 20; i++)
        archivePath_->addItem(m_RegistryInfo.ArcPaths[i]);
    name_ = archivePath_->lineEdit();
    name_->setObjectName("archiveName");
    connect(name_, &QLineEdit::textChanged, this,
            [this](const QString &name) { archiveNameState_.m_ArchivePath = name; });
    archiveLayout->addWidget(archivePath_, 1);
    auto *folderRow = new QWidget(archiveField);
    auto *folderLayout = new QHBoxLayout(folderRow);
    folderLayout->setContentsMargins(0, 0, 0, 0);
    folderLayout->addWidget(archiveFolder_, 1);
    auto *browse = new QPushButton(folderRow);
    browse->setObjectName("archiveBrowse");
    browse->setAttribute(Qt::WA_LayoutUsesWidgetRect);
    QtBindLocalText(browse, "ArchiveRedesign", QT_TRANSLATE_NOOP("ArchiveRedesign", "Browse…"));
    folderLayout->addWidget(browse);
    archiveRow->setFocusProxy(archivePath_);
    auto *archiveLabel = new QLabel("&Archive:", this);
    archiveLabel->setObjectName("archiveLabel");
    archiveLabel->setBuddy(archivePath_);
    archiveLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    auto *folderLabel = new QLabel(archiveField);
    folderLabel->setObjectName("archiveFolderLabel");
    folderLabel->setBuddy(archiveFolder_);
    QtBindLocalText(folderLabel, "ArchiveRedesign",
                    QT_TRANSLATE_NOOP("ArchiveRedesign", "Destination folder"));
    QtBindLocalText(archiveFolder_, "ArchiveRedesign",
                    QT_TRANSLATE_NOOP("ArchiveRedesign", "Destination folder"), true);
    fieldLayout->addRow(archiveLabel, archiveRow);
    fieldLayout->addRow(folderLabel, folderRow);
    windowLayout->addWidget(archiveField);
    QWidget::setTabOrder(archivePath_, archiveFolder_);
    QWidget::setTabOrder(archiveFolder_, browse);
    return browse;
}

void CreateArchiveDialog::configureFormMetrics() {
#ifdef Q_OS_MACOS
    for (auto *check : findChildren<QCheckBox *>())
        check->setMinimumHeight(check->sizeHint().height());
#endif
    // Keep the value row steady when the memory figures use one or two lines.
    memoryUsage_->setMinimumHeight(2 * memoryUsage_->fontMetrics().lineSpacing());
    for (auto *label : findChildren<QLabel *>())
        label->setWordWrap(true);
}

void CreateArchiveDialog::createArchiveForm(QVBoxLayout *layout, QVBoxLayout *windowLayout) {
    auto *basic = new QWidget(layout->parentWidget());
    auto *form = QtCreateDialogForm();
    basic->setLayout(form);
    auto *browse = createArchiveDestination(windowLayout);
    format_ = new QComboBox(basic);
    format_->setObjectName("archiveFormat");
    populateArchiveFormats();
    if (!codecs_) {
        const int savedFormat = format_->findText(m_RegistryInfo.ArcType, Qt::MatchFixedString);
        format_->setCurrentIndex(savedFormat >= 0 ? savedFormat : 0);
    }
    if (codecs_ && codecs_->result() != S_OK)
        format_->setToolTip(codecs_->error());
    configureArchiveName(format_->currentText());
    archiveNameState_.SetArchiveName(suggestedArchiveName_);
    name_->setText(archiveNameState_.m_ArchivePath);
    {
        updateMode_ = new QComboBox(this);
        updateMode_->setObjectName("archiveUpdateMode");
        updateMode_->addItem("Add and replace files", int(NCompressDialog::NUpdateMode::kAdd));
        updateMode_->addItem("Update and add files", int(NCompressDialog::NUpdateMode::kUpdate));
        updateMode_->addItem("Freshen existing files", int(NCompressDialog::NUpdateMode::kFresh));
    }
    pathMode_ = new QComboBox(this);
    pathMode_->setObjectName("archivePathMode");
    pathMode_->addItem("Relative pathnames", int(NWildcard::k_RelatPath));
    pathMode_->addItem("Full pathnames", int(NWildcard::k_FullPath));
    pathMode_->addItem("Absolute paths", int(NWildcard::k_AbsPath));
    layout->addWidget(basic);
    createEncryptionOptions(layout);
    createOptionSections(layout, form);
    for (auto *combo : findChildren<QComboBox *>())
        connect(combo, &QComboBox::currentTextChanged, this,
                &CreateArchiveDialog::updateRedesignSummary);
    for (auto *edit : findChildren<QLineEdit *>())
        connect(edit, &QLineEdit::textChanged, this, &CreateArchiveDialog::updateRedesignSummary);
    for (auto *check : findChildren<QCheckBox *>())
        connect(check, &QCheckBox::toggled, this, &CreateArchiveDialog::updateRedesignSummary);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString resolved = archivePath();
        UString draft = ToUString(QDir::toNativeSeparators(name_->text()));
        draft.Trim();
        const UString leaf(draft.Ptr(unsigned(draft.ReverseFind_PathSepar() + 1)));
        const bool directoryDraft = !name_->text().contains(QChar::Null) &&
                                    (leaf.IsEmpty() || leaf.IsEqualTo(".") || leaf.IsEqualTo(".."));
        const bool nameIncludesPath = NWindows::NFile::NName::GetRootPrefixSize(draft) != 0 ||
                                      NWindows::NFile::NName::FindSepar(draft) >= 0;
        const QFileInfo target(resolved);
        const QString initial = resolved.isEmpty() ? archiveNameState_.StartDirPrefix
                                : directoryDraft   ? target.absoluteFilePath()
                                                   : target.absolutePath();
        UString folder;
        if (MyBrowseForFolder(
                this,
                ToUString(QCoreApplication::translate("ArchiveRedesign", "Destination folder")),
                ToUString(initial), folder)) {
            archiveFolder_->setText(QDir::toNativeSeparators(ToQString(folder)));
            if (directoryDraft) {
                name_->clear();
                name_->setFocus();
            } else if (nameIncludesPath) {
                const QString filename =
                    resolved.isEmpty()
                        ? QFileInfo(QDir::fromNativeSeparators(ToQString(draft))).fileName()
                        : target.fileName();
                if (!filename.isEmpty())
                    name_->setText(filename);
            }
        }
    });
    connect(archivePath_, &QComboBox::activated, this, [this](int index) {
        CCompressDialog path = archiveNameState_;
        const QString selectedPath = archivePath_->itemText(index);
        path.Format = format_->currentText();
        path.ArcPath_WasChanged(selectedPath);
        format_->setCurrentText(path.Format);
        path.SetArcPathFields(selectedPath);
        archiveNameState_.DirPrefix = path.DirPrefix;
        archiveFolder_->setText(QDir::toNativeSeparators(path.DirPrefix));
        name_->setText(path.m_ArchivePath);
    });
}

void CreateArchiveDialog::createFooter(QVBoxLayout *layout) {
    auto *buttons = new QtLangButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons_ = buttons;
    buttons->SetAcceptText("ArchiveRedesign",
                           QT_TRANSLATE_NOOP("ArchiveRedesign", "Create archive"));
    layout->addWidget(buttons_);
    updateProtectionControls();

    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons_, &QDialogButtonBox::accepted, this, &CreateArchiveDialog::validateAndAccept);
}
