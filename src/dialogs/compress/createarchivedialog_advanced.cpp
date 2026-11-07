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
    compressionSection_ = new QtDisclosure(compression->parentWidget(), expertContent_);
    compressionSection_->setObjectName("archiveCompressionSection");
    QtBindLocalText(compressionSection_->button(), "ArchiveRedesign",
                    QT_TRANSLATE_NOOP("ArchiveRedesign", "Compression settings"));
    compressionSection_->setExpanded(true);
    expert->addWidget(compressionSection_);

    auto *files = form();
    files->setContentsMargins(0, 0, 0, 0);
    files->addRow("Update mode:", updateMode_);
    files->addRow("Path mode:", pathMode_);
    createLifecycleOptions(files);
    filesSection_ = new QtDisclosure(files->parentWidget(), expertContent_);
    filesSection_->setObjectName("archiveFilesSection");
    QtBindLocalText(filesSection_->button(), "ArchiveRedesign",
                    QT_TRANSLATE_NOOP("ArchiveRedesign", "File settings"));
    filesSection_->setExpanded(true);
    expert->addWidget(filesSection_);
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
    auto *metadata = new QPushButton(additional->parentWidget());
    metadata->setObjectName("archiveMetadataOptions");
    metadata->setEnabled(format_->currentIndex() >= 0);
    connect(format_, &QComboBox::currentIndexChanged, metadata,
            [metadata](int index) { metadata->setEnabled(index >= 0); });
    QtBindLocalText(metadata, "ArchiveRedesign",
                    QT_TRANSLATE_NOOP("ArchiveRedesign", "Metadata and time"));
    additional->addRow(metadata);
    connect(metadata, &QPushButton::clicked, this, [this] {
        CCompressDialog source;
        configureMetadataSource(source);
        const CCompressDialog initial = source;
        NCompression::CFormatOptions profile = Get_FormatOptions();
        source.FormatOptions = &profile;
        COptionsDialog dialog(&source);
        if (dialog.Create(this) == QDialog::Accepted) {
            applyMetadataSource(source, initial, profile);
            updateAdvancedSummary();
        }
    });
    additional->addRow(createSourcesSection(additional->parentWidget()));
    expert->insertWidget(0, createSourcesSection(expertContent_));
    auto *commandLine = form();
    commandLine->setContentsMargins(0, 0, 0, 0);
    createCommandLineOptions(commandLine);
    parametersSection_ = new QtDisclosure(commandLine->parentWidget(), expertContent_);
    parametersSection_->setObjectName("archiveParametersSection");
    QtBindLocalText(parametersSection_->button(), "ArchiveRedesign",
                    QT_TRANSLATE_NOOP("ArchiveRedesign", "Method properties"));
    parametersSection_->setExpanded(true);
    expert->addWidget(parametersSection_);
    expertContent_->hide();
}

QtDisclosure *CreateArchiveDialog::createSourcesSection(QWidget *parent) {
    auto *sources = new QPlainTextEdit(selected_.join('\n'), parent);
    sources->setObjectName("archiveSources");
    sources->setReadOnly(true);
    sources->setMaximumHeight(fontMetrics().lineSpacing() * 6);
    QtBindLocalText(sources, "ArchiveRedesign", QT_TRANSLATE_NOOP("ArchiveRedesign", "Sources"),
                    true);
    auto *sourceSection = new QtDisclosure(sources, parent);
    sourceSection->setObjectName("archiveSourcesSection");
    QtBindLocalText(sourceSection->button(), "ArchiveRedesign",
                    QT_TRANSLATE_NOOP("ArchiveRedesign", "Sources"));
    return sourceSection;
}

void CreateArchiveDialog::createLifecycleOptions(QFormLayout *form) {
    auto *group = new QWidget(this);
    group->setObjectName("archiveLifecycleOptions");
    auto *layout = new QVBoxLayout(group);
    layout->setContentsMargins(0, 0, 0, 0);
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
    form->addRow(group);
#ifndef Q_OS_WIN
    form->setRowVisible(group, false);
#endif
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

void CreateArchiveDialog::configureMetadataSource(CCompressDialog &source) const {
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
}

void CreateArchiveDialog::applyMetadataSource(CCompressDialog &source,
                                              const CCompressDialog &initial,
                                              const NCompression::CFormatOptions &profile) {
    // Publish only metadata. Method/resource edits still belong to their controls.
    auto &target = Get_FormatOptions();
    target.TimePrec = profile.TimePrec;
    target.MTime = profile.MTime;
    target.CTime = profile.CTime;
    target.ATime = profile.ATime;
    target.SetArcMTime = profile.SetArcMTime;
    auto converted = m_RegistryInfo;
    source.SaveMetadataOptions(converted);
    const auto publish = [&](const CBool1 &value, const CBool1 &before, CBoolPair &registry,
                             const CBoolPair &convertedRegistry, CBoolPair &request,
                             const CBoolPair &convertedRequest, const CBoolPair &initialRequest) {
        if (value.Val == before.Val)
            return;
        registry = convertedRegistry;
        if (fixedSources_) {
            request = convertedRequest;
            // Source acceptance normalizes false into an implicit request.
            // This form is read again: retain an explicit command override
            // so a saved true value cannot override the user's false choice.
            if (initialRequest.Def)
                request.Def = true;
        }
    };
    publish(source.SymLinks, initial.SymLinks, m_RegistryInfo.SymLinks, converted.SymLinks,
            archiveNameState_.Info.SymLinks, source.Info.SymLinks, initial.Info.SymLinks);
    publish(source.HardLinks, initial.HardLinks, m_RegistryInfo.HardLinks, converted.HardLinks,
            archiveNameState_.Info.HardLinks, source.Info.HardLinks, initial.Info.HardLinks);
    publish(source.AltStreams, initial.AltStreams, m_RegistryInfo.AltStreams, converted.AltStreams,
            archiveNameState_.Info.AltStreams, source.Info.AltStreams, initial.Info.AltStreams);
    publish(source.NtSecurity, initial.NtSecurity, m_RegistryInfo.NtSecurity, converted.NtSecurity,
            archiveNameState_.Info.NtSecurity, source.Info.NtSecurity, initial.Info.NtSecurity);
    if (source.PreserveATime.Supported && source.PreserveATime.Val != initial.PreserveATime.Val) {
        m_RegistryInfo.PreserveATime.Val = source.PreserveATime.Val;
        m_RegistryInfo.PreserveATime.Def = true;
    }
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
    bool filesModified = updateMode_->currentData().toInt() != NCompressDialog::NUpdateMode::kAdd ||
                         pathMode_->currentData().toInt() != NWildcard::k_RelatPath;
    filesModified |= sharedFiles_->isEnabled() && sharedFiles_->isChecked();
    compressionSection_->setSummary(compressionModified ? modified : QString());
    filesSection_->setSummary(filesModified ? modified : QString());
    parametersSection_->setSummary(parameters_->text().isEmpty() ? QString() : modified);
    QStringList summary;
    if (level_->count() > 1)
        summary.append(level_->currentText());
    if (volume_->isEnabled() && !volume_->currentText().trimmed().isEmpty())
        summary.append(volume_->currentText());
    if (compressionModified || filesModified || hasMetadataOptions_ ||
        (parameters_->isEnabled() && !parameters_->text().isEmpty()))
        summary.append(modified);
    additionalOptions_->setSummary(summary.join(QStringLiteral(" · ")));
}
