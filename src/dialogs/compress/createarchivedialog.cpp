#include "dialogs/compress/createarchivedialog.h"
#include "ui/dialogs/qtdialoglayout.h"

#include "7zip/UI/FileManager/resource.h"
#include "7zip/UI/GUI/CompressDialogRes.h"
#include "archive/callbacks/qtguimessages.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "dialogs/browse/BrowseDialog.h"
#include "dialogs/compress/CompressDialog.h"
#include "localization/qtlangbuttonbox.h"
#include "localization/qtlangcontrols.h"
#include "settings/ViewSettings.h"
#include "ui/controls/qtdisclosure.h"
#include "ui/controls/qtstatuslabel.h"
#include "ui/diagnostics/qtdiagnostictext.h"
#include "ui/dialogs/qtdialogscreenservices.h"
#include <QEvent>
#include <QProxyStyle>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QStyle>
#include <QToolButton>

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

CreateArchiveDialog::CreateArchiveDialog(const QString &directory,
                                         const QStringList &initialSources, QWidget *parent,
                                         std::shared_ptr<const ArchiveCodecCatalog> codecs,
                                         const CCompressDialog *sourceDialog)
    : QDialog(parent), codecs_(std::move(codecs)) {
    QtUseContextDialogModality(*this);
    setObjectName("createArchiveDialog");
    auto *layout = new QVBoxLayout(this);
    const int horizontalMargin = 16;
    const int verticalMargin = 16;
    // Put the scroll bar at the window edge; the source right margin belongs
    // between the form controls and that bar, not outside the scroll area.
    layout->setContentsMargins(horizontalMargin, verticalMargin, 0, verticalMargin);
    layout->setSpacing(16);
    auto *scroll = new ArchiveFormScrollArea(this);
    scroll->setObjectName("archiveOptionsScrollArea");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *scrollStyle = new ArchiveFormScrollStyle;
    scrollStyle->setParent(scroll);
    scroll->verticalScrollBar()->setStyle(scrollStyle);
    scroll->horizontalScrollBar()->setStyle(scrollStyle);
    auto *content = new QWidget(scroll);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, horizontalMargin, 0);
    contentLayout->setSpacing(16);
    contentLayout->setAlignment(Qt::AlignTop);
    m_RegistryInfo.Load(compressionSettingsPath());
    fixedSources_ = sourceDialog != nullptr;
    if (sourceDialog)
        archiveNameState_ = *sourceDialog;
    else
        archiveNameState_.DirPrefix = directory;
    archiveNameState_.StartDirPrefix = directory;
    prepareInitialSources(initialSources);
    createArchiveForm(contentLayout, layout);
    scroll->setWidget(content);
    layout->addWidget(scroll);
    createFooter(layout);
    buttons_->setContentsMargins(0, 0, horizontalMargin, 0);
    // Cocoa layout-item margins can extend a combo or group frame past its
    // allocated column/scroll area. Keep the complete source controls exposed.
    for (auto *combo : findChildren<QComboBox *>())
        combo->setAttribute(Qt::WA_LayoutUsesWidgetRect);
    for (auto *combo : {format_, level_, method_, dictionary_, wordSize_, solidBlock_, threads_,
                        memUse_, updateMode_, pathMode_, encryption_}) {
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(combo == memUse_ ? 6 : 10);
    }
    for (auto *group : findChildren<QGroupBox *>())
        group->setAttribute(Qt::WA_LayoutUsesWidgetRect);
#ifdef Q_OS_MACOS
    // Cocoa checkbox layout-item margins can extend a spanning form row
    // beyond its group and scroll viewport.
    for (auto *check : findChildren<QCheckBox *>())
        check->setAttribute(Qt::WA_LayoutUsesWidgetRect);
#endif
    // Source MY_CHECKBOX/PushButton controls participate in Tab traversal.
    // QMacStyle otherwise gives buttons only the restricted TabFocus policy.
    for (auto *button : findChildren<QAbstractButton *>())
        button->setFocusPolicy(Qt::StrongFocus);
    BindLanguage();
    if (fixedSources_)
        initializeSourceControls(sourceDialog->Info);
    updateRedesignSummary();
    stabilizeColumnWidths();
    // Size the source form to its controls rather than reserving the height
    // of the former file picker. Leave room for the native window frame and
    // taskbar; the options can scroll on a smaller screen, the footer cannot.
    const QMargins margins = layout->contentsMargins();
    const int width =
        qMax(640, content->minimumSizeHint().width() + margins.left() + margins.right());
    const int contentWidth =
        width - margins.left() - margins.right() - scroll->verticalScrollBar()->sizeHint().width();
    // Reparenting fields and revealing the summary can leave cached spacing
    // from construction. Recompute it before choosing the initial height.
    contentLayout->invalidate();
    const int contentHeight = contentLayout->hasHeightForWidth()
                                  ? contentLayout->heightForWidth(contentWidth)
                                  : content->sizeHint().height();
    const int height =
        contentHeight + findChild<QWidget *>("archiveNameField")->sizeHint().height() +
        buttons_->sizeHint().height() + 2 * layout->spacing() + margins.top() + margins.bottom();
    const int frameHeight = 2 * style()->pixelMetric(QStyle::PM_TitleBarHeight);
    const QSize available = screen()->availableGeometry().size() - QSize(0, frameHeight);
    resize(QSize(width, height).boundedTo(available).expandedTo(minimumSizeHint()));
    // The original modal's first editable combo receives initial dialog focus
    // and selects its text. Qt's scroll hierarchy needs an explicit adapter.
    name_->setFocus(Qt::OtherFocusReason);
    name_->selectAll();
}

void CreateArchiveDialog::stabilizeColumnWidths() {
    // Keep the value row steady when the memory figures use one or two lines.
    memoryUsage_->setMinimumHeight(2 * memoryUsage_->fontMetrics().lineSpacing());
    for (auto *label : findChildren<QLabel *>())
        label->setWordWrap(true);
    const int labelWidth = fontMetrics().averageCharWidth() * 24;
    for (auto *form : findChildren<QFormLayout *>()) {
        if (form->rowWrapPolicy() == QFormLayout::WrapAllRows)
            continue;
        form->setLabelAlignment(Qt::AlignLeading | Qt::AlignVCenter);
        form->setHorizontalSpacing(12);
        form->setVerticalSpacing(8);
        for (int row = 0; row < form->rowCount(); ++row)
            if (auto *item = form->itemAt(row, QFormLayout::LabelRole))
                if (auto *label = qobject_cast<QLabel *>(item->widget()))
                    label->setFixedWidth(labelWidth);
    }
    findChild<QLabel *>("archiveLabel")->setFixedWidth(labelWidth);
    findChild<QLabel *>("archiveFolderLabel")->setFixedWidth(labelWidth);
}

QWidget *CreateArchiveDialog::reserveControlSpace(QWidget *control) {
    // QFormLayout collapses hidden rows. Keep the source rectangle through a
    // visible slot whose box layout retains the hidden control's native size.
    auto *slot = new QWidget(control->parentWidget());
    auto *layout = new QVBoxLayout(slot);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto policy = control->sizePolicy();
    policy.setRetainSizeWhenHidden(true);
    control->setSizePolicy(policy);
    layout->addWidget(control);
    return slot;
}

void CreateArchiveDialog::createArchiveForm(QVBoxLayout *layout, QVBoxLayout *windowLayout) {
    auto *basic = new QWidget(layout->parentWidget());
    auto *form = new QFormLayout(basic);
    form->setContentsMargins(0, 0, 0, 0);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    auto *archiveField = new QWidget(this);
    archiveField->setObjectName("archiveNameField");
    auto *fieldLayout = new QGridLayout(archiveField);
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
    fieldLayout->addWidget(archiveRow, 0, 1);
    fieldLayout->addWidget(folderRow, 1, 1);
    auto *archiveLabel = new QLabel("&Archive:", this);
    archiveLabel->setObjectName("archiveLabel");
    archiveLabel->setBuddy(archivePath_);
    archiveLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    fieldLayout->addWidget(archiveLabel, 0, 0);
    auto *folderLabel = new QLabel(archiveField);
    folderLabel->setObjectName("archiveFolderLabel");
    folderLabel->setBuddy(archiveFolder_);
    QtBindLocalText(folderLabel, "ArchiveRedesign",
                    QT_TRANSLATE_NOOP("ArchiveRedesign", "Destination folder"));
    QtBindLocalText(archiveFolder_, "ArchiveRedesign",
                    QT_TRANSLATE_NOOP("ArchiveRedesign", "Destination folder"), true);
    fieldLayout->addWidget(folderLabel, 1, 0);
    fieldLayout->setHorizontalSpacing(12);
    fieldLayout->setVerticalSpacing(8);
    fieldLayout->setColumnStretch(1, 1);
    windowLayout->addWidget(archiveField);
    QWidget::setTabOrder(archivePath_, archiveFolder_);
    QWidget::setTabOrder(archiveFolder_, browse);
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
        updateMode_->addItem("Synchronize files", int(NCompressDialog::NUpdateMode::kSync));
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
        if (archivePath().isEmpty()) {
            ShowErrorMessageQt(this, QtDiagnosticText(QT_TRANSLATE_NOOP("SevenZipDiagnostics",
                                                                        "Incorrect archive path")));
            return;
        }
        UString folder;
        if (MyBrowseForFolder(
                this,
                ToUString(QCoreApplication::translate("ArchiveRedesign", "Destination folder")),
                ToUString(archiveFolder_->text()), folder))
            archiveFolder_->setText(QDir::toNativeSeparators(ToQString(folder)));
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
    auto *buttons = new QtLangButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Help, this);
    buttons_ = buttons;
    buttons->SetAcceptText("ArchiveRedesign",
                           QT_TRANSLATE_NOOP("ArchiveRedesign", "Create archive"));
    const auto showHelp = [this] {
        CCompressDialog dialog;
        dialog.OnHelp(this);
    };
    connect(buttons_, &QDialogButtonBox::helpRequested, this, showHelp);
    auto *help = new QShortcut(QKeySequence(Qt::Key_F1), this);
    connect(help, &QShortcut::activated, this, showHelp);
    layout->addWidget(buttons_);
    if (format_->currentIndex() < 0)
        buttons_->button(QDialogButtonBox::Ok)->setEnabled(false);

    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons_, &QDialogButtonBox::accepted, this, &CreateArchiveDialog::validateAndAccept);
}

QString CreateArchiveDialog::archivePath() const {
    if (archiveFolder_->text().contains(QChar::Null))
        return {};
    QString result;
    archiveNameState_.GetFinalPath_Smart(result);
    return result;
}

void CreateArchiveDialog::validateAndAccept() {
    accepted_.reset();
    if (password_) {
        const bool protect = password_->isEnabled();
        const auto error = CCompressDialog::CheckPassword(
            protect ? password_->text() : QString(),
            protect ? passwordConfirmation_->text() : QString(), showPassword_->isChecked(),
            format_->currentText(), GetEncryptionMethodSpec());
        if (error != CCompressDialog::kPasswordOK) {
            const UInt32 id = error == CCompressDialog::kPasswordUseAscii  ? IDS_PASSWORD_USE_ASCII
                              : error == CCompressDialog::kPasswordTooLong ? IDS_PASSWORD_TOO_LONG
                                                                           : IDS_PASSWORD_NOT_MATCH;
            ShowErrorMessageHwndResQt(this, id);
            return;
        }
    }
    if (!checkMemoryUsage())
        return;
    SaveOptionsInMem();
    const QString resolved = archivePath();
    if (resolved.isEmpty()) {
        // Original OnOK validates GetFinalPath_Smart only. Source scanning and
        // UpdateArchive own input errors and CreateComplexDir for new parents.
        const std::wstring message =
            QCoreApplication::translate("CreateArchiveDialog", "Incorrect archive path")
                .toStdWString();
        ShowErrorMessageQt(this, message.c_str());
        return;
    }
    if (!checkVolumeSizes())
        return;
    // Existing outputs are opened/validated by source UpdateArchive after OK,
    // just as for the original form. The dialog must not reject update intent.
    auto request = readRequest(resolved);
    CCompressDialog path;
    path.m_ArchivePath = resolved;
    path.Format = format_->currentText();
    path.ShowPassword = showPassword_ && showPassword_->isChecked();
    path.EncryptHeaders = encryptHeaders_ && encryptHeaders_->isChecked();
    path.OnOK(m_RegistryInfo);
    m_RegistryInfo.Save(compressionSettingsPath());
    accepted_ = std::move(request);
    accept();
}

void CreateArchiveDialog::showEvent(QShowEvent *event) {
    accepted_.reset();
    QDialog::showEvent(event);
}

CompressionOptions CreateArchiveDialog::compressionOptions() const {
    if (accepted_)
        return accepted_->Options;
    CompressionOptions options;
    options.SFXMode = sfx_ && sfx_->isEnabled() && sfx_->isChecked();
    if (options.SFXMode)
        options.SfxModule = sfxModule_.path();
    options.format = format_->currentText();
    CCompressDialog metadata;
    prepareMetadataSource(metadata);
    NCompression::CInfo metadataRegistry = m_RegistryInfo;
    metadata.SaveMetadataOptions(metadataRegistry);
    options.SymLinks = metadata.Info.SymLinks;
    options.HardLinks = metadata.Info.HardLinks;
    options.AltStreams = metadata.Info.AltStreams;
    options.NtSecurity = metadata.Info.NtSecurity;
    options.OpenShareForWrite =
        sharedFiles_ && sharedFiles_->isEnabled() && sharedFiles_->isChecked();
    options.DeleteAfterCompressing = deleteAfterCompressing_ &&
                                     deleteAfterCompressing_->isEnabled() &&
                                     deleteAfterCompressing_->isChecked();
#ifdef Q_OS_WIN
    options.PreserveATime = m_RegistryInfo.PreserveATime;
#endif
    if (parameters_) {
        options.Options = parameters_->text().toStdWString().c_str();
        options.Options.Trim();
    }
    const int index = FindRegistryFormat(registryFormat_);
    if (index >= 0) {
        const auto &fo = m_RegistryInfo.Formats[index];
        options.TimePrec = fo.TimePrec;
        options.MTime = fo.MTime;
        options.CTime = fo.CTime;
        options.ATime = fo.ATime;
        options.SetArcMTime = fo.SetArcMTime;
    }
    if (volume_ && volume_->isEnabled())
        CCompressDialog::OnOK(volume_->currentText(), options);
    if (updateMode_)
        options.UpdateMode =
            static_cast<NCompressDialog::NUpdateMode::EEnum>(updateMode_->currentData().toInt());
    options.PathMode = static_cast<NWildcard::ECensorPathMode>(pathMode_->currentData().toInt());
    if (level_) {
        if (encryption_)
            options.EncryptionMethod = GetEncryptionMethodSpec();
        options.Level = level_->currentData().toInt();
        options.Method = archiveNameState_.GetMethodSpec();
        options.OrderMode = NCompressDialog::GetOrderMode(archiveNameState_.GetMethodID());
        const UInt32 threads = threads_->currentData().toUInt();
        options.NumThreads = threads ? threads : UInt32(-1);
        options.MemUsage = Get_MemUse_Spec();
        const UInt64 dictionary =
            dictionary_->isEnabled() ? dictionary_->currentData().toULongLong() : 0;
        options.Dict64 = dictionary ? dictionary : UInt64(-1);
        const UInt32 order = wordSize_->isEnabled() ? wordSize_->currentData().toUInt() : 0;
        options.Order = order ? order : UInt32(-1);
        options.SolidIsSpecified = solidBlock_->isEnabled();
        NCompressDialog::OnOK(
            solidBlock_->isEnabled() ? solidBlock_->currentData().toUInt() : UInt32(-1), options);
    }
    return options;
}
