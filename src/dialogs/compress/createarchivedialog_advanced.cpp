#include "7zip/PropID.h"
#include "7zip/UI/GUI/CompressDialogRes.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "dialogs/compress/createarchivedialog.h"
#include "dialogs/compress/qtcompressoptionsdialog.h"
#include "localization/qtlangcontrols.h"
#include "ui/controls/qtdisclosure.h"
#include <QPlainTextEdit>
#include <QToolButton>

void CreateArchiveDialog::createOptionSections(QVBoxLayout *layout, QFormLayout *basic) {
    const auto form = [this] {
        auto *result = new QFormLayout(new QWidget(this));
        result->setContentsMargins(24, 4, 0, 4);
        result->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        result->setRowWrapPolicy(QFormLayout::WrapLongRows);
        return result;
    };
    auto *additional = form();
    additionalOptions_ = new QtDisclosure(additional->parentWidget(), layout->parentWidget());
    additionalOptions_->setObjectName("archiveAdditionalSection");
    QtBindLocalText(additionalOptions_->button(), "ArchiveRedesign",
                    QT_TRANSLATE_NOOP("ArchiveRedesign", "Additional options"));
    layout->addWidget(additionalOptions_);
    expertContent_ = new QWidget(this);
    expertContent_->setObjectName("archiveExpertContent");
    auto *expert = new QVBoxLayout(expertContent_);
    expert->setContentsMargins(0, 0, 0, 0);
    expert->setSpacing(16);
    auto *compression = form();
    compression->setContentsMargins(0, 0, 0, 0);
    compression->parentWidget()->setObjectName("archiveCompressionColumn");
    basic->addRow("Archive format:", format_);
    createCompressionOptions(compression);
    const auto level = compression->takeRow(level_);
    additional->addRow(level.labelItem->widget(), level.fieldItem->widget());
    level_->show();
    delete level.labelItem;
    delete level.fieldItem;
    expert->addWidget(compression->parentWidget());

    auto *files = form();
    files->setContentsMargins(0, 0, 0, 0);
    files->addRow("Update mode:", updateMode_);
    files->addRow("Path mode:", pathMode_);
    createLifecycleOptions(files);
    createAdvancedOptions(files);
    expert->addWidget(files->parentWidget());
    createVolumeOptions(additional);
    auto *edit = new QPushButton(additional->parentWidget());
    edit->setObjectName("archiveExpertOptions");
    edit->setEnabled(format_->currentIndex() >= 0);
    connect(format_, &QComboBox::currentIndexChanged, edit,
            [edit](int index) { edit->setEnabled(index >= 0); });
    QtBindLocalText(edit, "ArchiveRedesign",
                    QT_TRANSLATE_NOOP("ArchiveRedesign", "Expert options…"));
    additional->addRow(edit);
    connect(edit, &QPushButton::clicked, this, [this] { showExpertOptions(); });
    auto *sources = new QPlainTextEdit(selected_.join('\n'), additional->parentWidget());
    sources->setObjectName("archiveSources");
    sources->setReadOnly(true);
    sources->setMaximumHeight(fontMetrics().lineSpacing() * 6);
    QtBindLocalText(sources, "ArchiveRedesign", QT_TRANSLATE_NOOP("ArchiveRedesign", "Sources"),
                    true);
    auto *sourceSection = new QtDisclosure(sources, additional->parentWidget());
    sourceSection->setObjectName("archiveSourcesSection");
    QtBindLocalText(sourceSection->button(), "ArchiveRedesign",
                    QT_TRANSLATE_NOOP("ArchiveRedesign", "Sources"));
    additional->addRow(sourceSection);
    auto *commandLine = form();
    commandLine->setContentsMargins(0, 0, 0, 0);
    createCommandLineOptions(commandLine);
    expert->addWidget(commandLine->parentWidget());
    expertContent_->hide();
}

void CreateArchiveDialog::createLifecycleOptions(QFormLayout *form) {
    // CompressDialog.rc: right-hand Options group, SFX, Shared, Delete,
    // preceding Encryption.
    auto *group = new QWidget(this);
    group->setObjectName("archiveLifecycleOptions");
    auto *layout = new QVBoxLayout(group);
    layout->setContentsMargins(0, 0, 0, 0);
    sfx_ = new QCheckBox(group);
    sfx_->setObjectName("archiveSfx");
    if (codecs_)
        sfxModule_ = codecs_->sfxModule();
    layout->addWidget(sfx_);
    connect(sfx_, &QCheckBox::toggled, this, &CreateArchiveDialog::toggleSfx);
    updateSfxControls();
    sharedFiles_ = new QCheckBox("Compress shared files", group);
    sharedFiles_->setObjectName("archiveSharedFiles");
#ifndef Q_OS_WIN
    sharedFiles_->setEnabled(false);
    sharedFiles_->hide();
    QtBindLocalToolTip(sharedFiles_, "createarchivedialog_advanced",
                       QT_TRANSLATE_NOOP("createarchivedialog_advanced",
                                         "File sharing restrictions apply only on Windows."));
#endif
    layout->addWidget(sharedFiles_);
    deleteAfterCompressing_ = new QCheckBox("Delete files after compression", group);
    deleteAfterCompressing_->setObjectName("archiveDeleteAfterCompression");
    layout->addWidget(deleteAfterCompressing_);
    form->addRow(group);
    // OnInit leaves this checkbox available for all update modes. The original
    // UpdateArchive owner validates incompatible action sets after confirmation.
}

void CreateArchiveDialog::createCommandLineOptions(QFormLayout *form) {
    parameters_ = new QLineEdit(this);
    parameters_->setObjectName("archiveParameters");
    const auto &options = Get_FormatOptions().Options;
    parameters_->setText(QString::fromWCharArray(options.Ptr(), options.Len()));
    QtBindLocalToolTip(parameters_, "createarchivedialog_advanced",
                       QT_TRANSLATE_NOOP("createarchivedialog_advanced",
                                         "Method parameters, for example: tm=off or mt=2."));
    auto *parametersForm = new QFormLayout;
    parametersForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    parametersForm->setRowWrapPolicy(QFormLayout::WrapAllRows);
    parametersForm->addRow("&Parameters:", parameters_);
    form->addRow(parametersForm);
}

void CreateArchiveDialog::createAdvancedOptions(QFormLayout *form) {
    advanced_ = new QPushButton("&Parameters:", this);
    advanced_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
#ifdef Q_OS_MACOS
    advanced_->setAttribute(Qt::WA_LayoutUsesWidgetRect);
#endif
    advanced_->setObjectName("archiveAdvancedOptions");
    form->addRow(advanced_);
    updateAdvancedSummary();
    connect(advanced_, &QPushButton::clicked, this, &CreateArchiveDialog::showAdvancedOptions);
}

void CreateArchiveDialog::showAdvancedOptions() {
    // The exact registered handler flags supply the original dialog capability
    // owner while codecs/compression remain in the official external engine.
    CCompressDialog source;
    source.Format = registryFormat_;
    source.MethodID = archiveNameState_.GetMethodID();
#ifdef Q_OS_WIN
    source.PreserveATime.SetVal_as_Supported(m_RegistryInfo.PreserveATime.Val);
#endif
    source.ArcInfo.Name = registryFormat_.toStdWString().c_str();
    source.ArcInfo.Flags = NArcInfoFlags::kCTime | NArcInfoFlags::kATime | NArcInfoFlags::kMTime |
                           NArcInfoFlags::kMTime_Default;
    source.ArcInfo.TimeFlags = TIME_PREC_TO_ARC_FLAGS_MASK(NFileTimeType::kWindows) |
                               TIME_PREC_TO_ARC_FLAGS_TIME_DEFAULT(NFileTimeType::kWindows);
    if (registryFormat_ == "zip")
        source.ArcInfo.TimeFlags |= TIME_PREC_TO_ARC_FLAGS_MASK(NFileTimeType::kUnix) |
                                    TIME_PREC_TO_ARC_FLAGS_MASK(NFileTimeType::kDOS);
    prepareMetadataSource(source);
    // Local profile copy is source COptionsDialog's in-memory transaction:
    // Options Cancel does not mutate the Add form; Add Cancel never saves disk.
    NCompression::CFormatOptions profile = Get_FormatOptions();
    source.FormatOptions = &profile;
    COptionsDialog dialog(&source);
    if (dialog.Create(advanced_->window()) == QDialog::Accepted) {
        Get_FormatOptions() = profile;
        source.SaveMetadataOptions(m_RegistryInfo);
        if (fixedSources_) {
            archiveNameState_.Info.SymLinks = source.Info.SymLinks;
            archiveNameState_.Info.HardLinks = source.Info.HardLinks;
            archiveNameState_.Info.AltStreams = source.Info.AltStreams;
            archiveNameState_.Info.NtSecurity = source.Info.NtSecurity;
        }
        if (source.PreserveATime.Supported) {
            m_RegistryInfo.PreserveATime.Val = source.PreserveATime.Val;
            m_RegistryInfo.PreserveATime.Def = true;
        }
    }
    updateAdvancedSummary();
}

void CreateArchiveDialog::prepareMetadataSource(CCompressDialog &source) const {
    if (fixedSources_)
        source.Info = archiveNameState_.Info;
    if (codecs_ && codecs_->result() == S_OK) {
        const int index = codecs_->formatIndex(registryFormat_);
        if (index >= 0)
            source.ArcInfo = codecs_->nativeCodecs().Formats[unsigned(index)];
    }
    source.LoadMetadataOptions(m_RegistryInfo);
}

void CreateArchiveDialog::updateAdvancedSummary() {
    CCompressDialog source;
    prepareMetadataSource(source);
    source.FormatOptions = &Get_FormatOptions();
    source.ShowOptionsString();
    hasMetadataOptions_ = !source.OptionsString.IsEmpty();
    updateRedesignSummary();
}

void CreateArchiveDialog::updateRedesignSummary() {
    if (!additionalOptions_ || !volume_ || !parameters_)
        return;
    if (auto *form = qobject_cast<QFormLayout *>(level_->parentWidget()->layout()))
        form->setRowVisible(level_, level_->count() > 1);
    const QString modified = QCoreApplication::translate("ArchiveRedesign", "Modified");
    bool compressionModified = memUse_->isEnabled() && Get_MemUse_Spec().IsDefined;
    for (auto *combo : {method_, dictionary_, wordSize_, solidBlock_, threads_})
        if (combo->isEnabled() && combo->currentIndex() > 0)
            compressionModified = true;
    bool filesModified = hasMetadataOptions_ ||
                         updateMode_->currentData().toInt() != NCompressDialog::NUpdateMode::kAdd ||
                         pathMode_->currentData().toInt() != NWildcard::k_RelatPath;
    for (auto *check : {sfx_, sharedFiles_, deleteAfterCompressing_})
        if (check->isEnabled() && check->isChecked())
            filesModified = true;
    QStringList summary;
    if (level_->count() > 1)
        summary.append(level_->currentText());
    if (volume_->isEnabled() && !volume_->currentText().trimmed().isEmpty())
        summary.append(volume_->currentText());
    if (compressionModified || filesModified ||
        (parameters_->isEnabled() && !parameters_->text().isEmpty()))
        summary.append(modified);
    additionalOptions_->setSummary(summary.join(QStringLiteral(" · ")));
}
