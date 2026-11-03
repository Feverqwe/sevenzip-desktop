#include "7zip/UI/GUI/CompressDialogRes.h"
#include "dialogs/compress/CompressDialog.h"
#include "dialogs/compress/createarchivedialog.h"
#include "localization/qtlangcontrols.h"

void CreateArchiveDialog::createCompressionOptions(QFormLayout *form) {
    // The desktop CompressDialog.rc has plain controls in the left column.
    QWidget *compression = form->parentWidget();
    QFormLayout *compressionForm = form;
    level_ = new QComboBox(compression);
    level_->setObjectName("compressionLevel");
    compressionForm->addRow("Compression &level:", level_);
    method_ = new QComboBox(compression);
    method_->setObjectName("compressionMethod");
    compressionForm->addRow("Compression &method:", method_);
    dictionary_ = new QComboBox(compression);
    dictionary_->setObjectName("compressionDictionary");
    dictionary_->addItem("Auto", 0);
    compressionForm->addRow("&Dictionary size:", dictionary_);
    wordSize_ = new QComboBox(compression);
    wordSize_->setObjectName("compressionWordSize");
    wordSize_->addItem("Auto", 0);
    compressionForm->addRow("&Word size:", wordSize_);
    solidBlock_ = new QComboBox(compression);
    solidBlock_->setObjectName("compressionSolidBlock");
    solidBlock_->addItem("Auto", 0);
    compressionForm->addRow("Solid Block size:", solidBlock_);
    auto *cpuRow = new QWidget(compression);
    auto *cpuLayout = new QHBoxLayout(cpuRow);
    cpuLayout->setContentsMargins(0, 0, 0, 0);
    threads_ = new QComboBox(cpuRow);
    threads_->setObjectName("compressionThreads");
    hardwareThreads_ = new QLabel(cpuRow);
    hardwareThreads_->setObjectName("compressionHardwareThreads");
    cpuLayout->addWidget(threads_, 1);
    cpuLayout->addWidget(hardwareThreads_);
    cpuRow->setFocusProxy(threads_);
    compressionForm->addRow("Number of CPU threads:", cpuRow);
    memoryUsage_ = new QLabel(compression);
    memoryUsage_->setObjectName("compressionMemoryUsage");
    memoryUsage_->setWordWrap(true);
    auto *memoryGroup = new QWidget(compression);
    memoryGroup->setObjectName("compressionMemoryGroup");
    auto *memoryForm = new QFormLayout(memoryGroup);
    memoryForm->setContentsMargins(0, 0, 0, 0);
    memoryForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    memoryForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    auto *memoryRow = new QWidget(memoryGroup);
    auto *memoryLayout = new QHBoxLayout(memoryRow);
    memoryLayout->setContentsMargins(0, 0, 0, 0);
    memUse_ = new QComboBox(memoryRow);
    memUse_->setObjectName("compressionMemoryLimit");
    memoryLayout->addWidget(memUse_, 1);
    memoryRow->setFocusProxy(memUse_);
    memoryForm->addRow("Compression memory limit:", memoryRow);
    auto *memoryHint = new QHBoxLayout;
    auto *usageLabel = new QLabel(memoryGroup);
    QtBindLocalText(usageLabel, "ArchiveRedesign",
                    QT_TRANSLATE_NOOP("ArchiveRedesign", "Compression:"));
    memoryHint->addWidget(usageLabel);
    memoryHint->addWidget(memoryUsage_, 1);
    decompressionMemory_ = new QLabel(compression);
    decompressionMemory_->setObjectName("compressionDecompressionMemory");
    decompressionMemory_->setAlignment(Qt::AlignLeading | Qt::AlignVCenter);
    // IDT_COMPRESS_MEMORY_DE spans the column independently of g0xs/g1xs.
    // Its caption must not widen the labels of the preceding combo rows.
    auto *decompressionGroup = new QWidget(compression);
    decompressionGroup->setObjectName("compressionDecompressionGroup");
    auto *decompressionRow = new QHBoxLayout(decompressionGroup);
    decompressionRow->setContentsMargins(0, 0, 0, 0);
    auto *decompressionLabel = new QLabel(compression);
    decompressionLabel->setObjectName("compressionDecompressionLabel");
    QtBindLocalText(decompressionLabel, "ArchiveRedesign",
                    QT_TRANSLATE_NOOP("ArchiveRedesign", "Extraction:"));
    decompressionRow->addWidget(decompressionLabel);
    decompressionRow->addWidget(decompressionMemory_, 1);
    memoryHint->addWidget(decompressionGroup);
    QFont hintFont = font();
    if (hintFont.pointSizeF() > 0)
        hintFont.setPointSizeF(hintFont.pointSizeF() * 0.9);
    for (auto *label : {usageLabel, memoryUsage_, decompressionLabel, decompressionMemory_}) {
        label->setFont(hintFont);
        label->setForegroundRole(QPalette::PlaceholderText);
    }
    memoryForm->addRow(memoryHint);
    compressionForm->addRow(reserveControlSpace(memoryGroup));
    registryFormat_ = format_->currentText();
    connect(format_, &QComboBox::currentTextChanged, this, [this] {
        SaveOptionsInMem();
        registryFormat_ = format_->currentText();
        FormatChanged();
        archiveNameState_.SetArchiveName2();
        name_->setText(archiveNameState_.m_ArchivePath);
    });
    connect(level_, &QComboBox::currentIndexChanged, this, [this] {
        // IDC_COMPRESS_LEVEL: clear the previous method-dependent specs before
        // selecting the level's default method and recalculating its controls.
        const UInt32 level = level_->currentData().toUInt();
        Get_FormatOptions().ResetForLevelChange();
        // OnCommand changes method-dependent controls, not the whole format:
        // SetParams would discard text the user is still editing.
        const QSignalBlocker methodBlock(method_);
        archiveNameState_.SetMethod2(*method_, level, m_RegistryInfo, [this] { MethodChanged(); });
    });
    connect(method_, &QComboBox::currentTextChanged, this, [this] {
        MethodChanged();
        // Original OnCommand(IDC_COMPRESS_METHOD) refreshes the Hash filename
        // after method/SFX/memory controls; source SetArchiveName2 owns naming.
        if (archiveNameState_.Get_ArcInfoEx().Flags_HashHandler()) {
            archiveNameState_.SetArchiveName2(false);
            name_->setText(archiveNameState_.m_ArchivePath);
        }
    });
    connect(dictionary_, &QComboBox::currentIndexChanged, this, [this] {
        // IDC_COMPRESS_DICTIONARY preserves explicit threads, but resets a
        // numeric solid size; Non-solid and Full-solid survive this change.
        SaveOptionsInMem();
        const UInt32 blockSizeLog = solidBlock_->currentData().toUInt();
        if (blockSizeLog != 0 && blockSizeLog != 64)
            Get_FormatOptions().Reset_BlockLogSize();
        MethodChanged();
    });
    connect(threads_, &QComboBox::currentIndexChanged, this, [this] { SetMemoryUsage(); });
    connect(solidBlock_, &QComboBox::currentIndexChanged, this,
            &CreateArchiveDialog::SetMemoryUsage);
    connect(memUse_, &QComboBox::currentIndexChanged, this, [this] {
        SaveOptionsInMem();
        SetNumThreads2();
    });
    FormatChanged();
}

// Adapted from CCompressDialog::SetLevel2 / SetNearestSelectComboBox (0766b73).
// Registry defaults and nearest-selection control flow follow upstream.
void CreateArchiveDialog::SetLevel2() {
    UInt32 level = 5;
    const int index = FindRegistryFormat(registryFormat_);
    if (index >= 0) {
        const NCompression::CFormatOptions &fo = m_RegistryInfo.Formats[index];
        if (fo.Level <= 9)
            level = fo.Level;
        else if (fo.Level == (UInt32)(Int32)-1)
            level = 5;
        else
            level = 9;
    }
    level_->clear();
    const auto &fi = NCompressDialog::GetFormatInfo(format_->currentText());
    for (unsigned i = 0; i < sizeof(UInt32) * 8; i++) {
        const UInt32 mask = fi.LevelsMask >> i;
        if (mask & 1) {
            level_->addItem(LevelText(i), i);
        }
    }
    for (int i = level_->count() - 1; i >= 0; i--)
        if (level_->itemData(i).toUInt() <= level) {
            level_->setCurrentIndex(i);
            return;
        }
    if (level_->count() > 0)
        level_->setCurrentIndex(0);
}

void CreateArchiveDialog::SetOrder2(UInt32 defaultOrder) {
    const int method = archiveNameState_.GetMethodID();
    const auto options =
        NCompressDialog::SetOrder2(method, level_->currentData().toUInt(), defaultOrder);
    wordSize_->clear();
    if (!options.Values.isEmpty())
        wordSize_->addItem(NCompressDialog::Modify_Auto(QString::number(options.AutoOrder)), 0);
    for (int order : options.Values)
        wordSize_->addItem(QString::number(order), order);
    wordSize_->setCurrentIndex(options.Selected);
}

void CreateArchiveDialog::SetDictionary2(UInt32 defaultDict) {
    const int method = archiveNameState_.GetMethodID();
    const auto options =
        NCompressDialog::SetDictionary2(method, level_->currentData().toUInt(), defaultDict);
    dictionary_->clear();
    if (options.HasAuto)
        dictionary_->addItem(
            NCompressDialog::Modify_Auto(NCompressDialog::Combo_AddDict2(options.AutoDict)),
            QVariant::fromValue(quint64(0)));
    for (quint64 size : options.Values)
        dictionary_->addItem(NCompressDialog::Combo_AddDict2(size), QVariant::fromValue(size));
    dictionary_->setCurrentIndex(options.Selected);
}

void CreateArchiveDialog::SetSolidBlockSize2(UInt32 defaultBlockSize) {
    const int method = archiveNameState_.GetMethodID();
    const auto options =
        NCompressDialog::SetSolidBlockSize2(format_->currentText(), level_->currentData().toUInt(),
                                            method, GetDict2(), defaultBlockSize);
    solidBlock_->clear();
    if (!options.Values.isEmpty())
        solidBlock_->addItem(
            NCompressDialog::Modify_Auto(NCompressDialog::Add_Size(options.AutoSolid)), UInt32(-1));
    for (UInt32 log : options.Values) {
        const QString label = log == 0    ? QtLangText(IDS_COMPRESS_NON_SOLID)
                              : log == 64 ? QtLangText(IDS_COMPRESS_SOLID)
                                          : NCompressDialog::Add_Size(UInt64(1) << log);
        solidBlock_->addItem(label, log);
    }
    solidBlock_->setCurrentIndex(options.Selected);
}
