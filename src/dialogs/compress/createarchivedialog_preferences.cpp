// Adapted from CCompressDialog format options in CompressDialog.cpp (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "dialogs/compress/createarchivedialog.h"
#include <QSignalBlocker>
#include <QStandardPaths>

QString CreateArchiveDialog::compressionSettingsPath() {
    const QString overridden = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
    if (!overridden.isEmpty())
        return overridden;
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(directory);
    return QDir(directory).filePath("toolbar.ini");
}

int CreateArchiveDialog::FindRegistryFormat(const QString &name) const {
    return CCompressDialog::FindRegistryFormat(m_RegistryInfo, name);
}

unsigned CreateArchiveDialog::FindRegistryFormat_Always(const QString &name) {
    return CCompressDialog::FindRegistryFormat_Always(m_RegistryInfo, name);
}

NCompression::CFormatOptions &CreateArchiveDialog::Get_FormatOptions() {
    return m_RegistryInfo.Formats[FindRegistryFormat_Always(registryFormat_)];
}

UString CreateArchiveDialog::parameterOptions() const {
    UString options;
    if (parameters_) {
        const auto text = parameters_->text().toStdWString();
        options.SetFrom(text.data(), unsigned(text.size()));
    }
    return options;
}

void CreateArchiveDialog::SaveOptionsInMem() {
    // Upstream saves the format that was selected before FormatChanged.
    NCompressDialog::CInfo info;
    info.Options = parameterOptions();
    info.Level = CCompressDialog::GetComboValue(*level_, 1);
    // Qt Auto uses 0; source GetComboValue(control, 1) returns all ones.
    const UInt64 dictionary = dictionary_->currentData().toULongLong();
    info.Dict64 = dictionary ? dictionary : UInt64(-1);
    const UInt32 order = wordSize_->currentData().toUInt();
    info.Order = order ? order : UInt32(-1);
    info.Method = archiveNameState_.GetMethodSpec();
    const UInt32 threads = threads_->currentData().toUInt();
    info.NumThreads = threads ? threads : UInt32(-1);
    info.EncryptionMethod =
        encryption_ ? GetEncryptionMethodSpec() : Get_FormatOptions().EncryptionMethod;
    CCompressDialog::SaveOptionsInMem(m_RegistryInfo, registryFormat_, info,
                                      solidBlock_->count() > 1 ? solidBlock_->currentData().toUInt()
                                                               : UInt32(-1),
                                      memUse_->currentData().toString(), parameters_ != nullptr);
}

void CreateArchiveDialog::SetMemUseCombo() {
    const QSignalBlocker blocked(memUse_);
    memUse_->clear();
    const bool enable = NCompressDialog::GetFormatInfo(registryFormat_).MemUse_();
    findChild<QWidget *>("compressionMemoryGroup")->setVisible(enable);
    findChild<QWidget *>("compressionDecompressionGroup")->setVisible(enable);
    memUse_->setEnabled(enable);
    if (!enable)
        return;
    const QString spec = Get_FormatOptions().MemUse;
    int selected;
    for (const auto &item : NCompressDialog::SetMemUseCombo(spec, &selected))
        memUse_->addItem(item.User, item.Spec);
    memUse_->setCurrentIndex(selected);
}

// Qt adapter for FormatChanged / SetMethod2 and the registry branches of
// SetDictionary2, SetOrder2, SetSolidBlockSize2 and SetNumThreads2.
void CreateArchiveDialog::FormatChanged() {
    const QSignalBlocker levelBlock(level_), methodBlock(method_), dictBlock(dictionary_),
        orderBlock(wordSize_), solidBlock(solidBlock_), threadsBlock(threads_);
    const NCompression::CFormatOptions fo = Get_FormatOptions();
    if (parameters_)
        parameters_->setText(QString::fromWCharArray(fo.Options.Ptr(), fo.Options.Len()));
    updateAdvancedSummary();
    SetMemUseCombo();
    SetLevel2();
    configureArchiveName(registryFormat_);
    archiveNameState_.SetMethod2(*method_, level_->currentData().toUInt(), m_RegistryInfo,
                                 [this] { MethodChanged(); });
    SetEncryptionMethod();
}

void CreateArchiveDialog::MethodChanged() {
    const QSignalBlocker dictBlock(dictionary_), orderBlock(wordSize_), solidBlock(solidBlock_),
        threadsBlock(threads_);
    const NCompression::CFormatOptions fo = Get_FormatOptions();
    // Upstream restores method-dependent values only for the saved method.
    const bool sameMethod =
        archiveNameState_.IsMethodEqualTo(UString(fo.Method.toStdWString().c_str()));
    wordSize_->setCurrentIndex(0);
    dictionary_->setCurrentIndex(0);
    solidBlock_->setCurrentIndex(-1);
    threads_->setCurrentIndex(0);
    SetDictionary2(sameMethod ? fo.Dictionary : UInt32(-1));
    SetOrder2(sameMethod ? fo.Order : UInt32(-1));
    SetSolidBlockSize2(sameMethod ? fo.BlockLogSize : UInt32(-1));
    SetNumThreads2();
    if (sameMethod && fo.NumThreads != UInt32(-1)) {
        const int threads = threads_->findData(fo.NumThreads);
        threads_->setCurrentIndex(threads);
    }
    // Source SetMethod2 retains TAR's GNU/POSIX choice at its only level (0).
    method_->setEnabled(method_->count() > 1);
    dictionary_->setEnabled(dictionary_->count() > 1);
    wordSize_->setEnabled(wordSize_->count() > 1);
    solidBlock_->setEnabled(solidBlock_->count() > 1);
    for (auto *field : {dictionary_, wordSize_, solidBlock_})
        if (auto *form = qobject_cast<QFormLayout *>(field->parentWidget()->layout()))
            form->setRowVisible(field, field->count() > 0);
    if (auto *form =
            qobject_cast<QFormLayout *>(threads_->parentWidget()->parentWidget()->layout()))
        form->setRowVisible(threads_->parentWidget(), threads_->count() > 0);
    SetMemoryUsage();
}
