#include "7zip/UI/FileManager/resourceGui.h"
#include "7zip/UI/GUI/CompressDialogRes.h"
#include "dialogs/compress/createarchivedialog.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include <QSignalBlocker>

UInt64 CreateArchiveDialog::GetDict2() const {
    const UInt64 dict = dictionary_->currentData().toULongLong();
    if (dict != 0)
        return dict;
    const int method = archiveNameState_.GetMethodID();
    const auto options = NCompressDialog::SetDictionary2(method, level_->currentData().toUInt());
    return options.AutoDict == UInt32(-1) ? UInt64(-1) : UInt64(options.AutoDict);
}

void CreateArchiveDialog::SetNumThreads2() {
    const bool hadNoSelection = threads_->count() > 0 && threads_->currentIndex() < 0;
    const UInt32 previous = threads_->currentData().toUInt();
    const int method = archiveNameState_.GetMethodID();
    const auto options = NCompressDialog::SetNumThreads2(
        format_->currentText(), method, level_->currentData().toUInt(), GetDict2(),
        solidBlock_->currentData().toUInt(), system_, Get_MemUse_Spec());
    archiveNameState_._auto_NumThreads = options.AutoNumThreads;
    {
        const QSignalBlocker blocked(threads_);
        threads_->clear();
        if (options.HasAuto)
            threads_->addItem(NCompressDialog::Modify_Auto(QString::number(options.AutoNumThreads)),
                              0u);
        for (UInt32 count : options.Values)
            threads_->addItem(QString::number(count), count);
        const int index = threads_->findData(previous);
        threads_->setCurrentIndex(hadNoSelection ? -1 : qMax(index, 0));
        threads_->setEnabled(!options.Values.isEmpty());
    }
    hardwareThreads_->setText(
        system_.NumCPUs == system_.NumHardwareThreads
            ? QStringLiteral("/ %1").arg(system_.NumCPUs)
            : QStringLiteral("/ %1 / %2").arg(system_.NumCPUs).arg(system_.NumHardwareThreads));
    SetMemoryUsage();
}

UInt64 CreateArchiveDialog::GetMemoryUsage_DecompMem(UInt64 &decompressMemory) const {
    const int method = archiveNameState_.GetMethodID();
    const UInt32 spec = threads_->currentData().toUInt();
    const UInt32 numThreads = archiveNameState_.GetNumThreads2(spec ? spec : UInt32(-1));
    return NCompressDialog::GetMemoryUsage_Threads_Dict_DecompMem(
        format_->currentText(), method, level_->currentData().toUInt(), numThreads, GetDict2(),
        solidBlock_->currentData().toUInt(), decompressMemory);
}

void CreateArchiveDialog::SetMemoryUsage() {
    UInt64 decompressMemory;
    const UInt64 usage = GetMemoryUsage_DecompMem(decompressMemory);
    const auto mu = Get_MemUse_Spec();
    memoryUsage_->setText(
        CCompressDialog::PrintMemUsage(IDT_COMPRESS_MEMORY_VALUE, usage, mu, system_));
    decompressionMemory_->setText(CCompressDialog::PrintMemUsage(IDT_COMPRESS_MEMORY_DE_VALUE,
                                                                 decompressMemory, mu, system_));
    memoryUsage_->setToolTip(
        mu.IsDefined || system_.RamSizeDefined
            ? QCoreApplication::translate("createarchivedialog_resources", "Limit: %1 / RAM: %2")
                  .arg(NCompressDialog::AddMemUsage(NCompressDialog::Get_MemUse_Bytes(mu, system_)),
                       system_.RamSizeDefined ? NCompressDialog::AddMemUsage(system_.RamSize)
                                              : QCoreApplication::translate(
                                                    "createarchivedialog_resources", "unknown"))
            : QCoreApplication::translate("createarchivedialog_resources", "RAM size is unknown"));
}

NCompression::CMemUse CreateArchiveDialog::Get_MemUse_Spec() const {
    NCompression::CMemUse mu;
    mu.Parse(memUse_->currentData().toString());
    return mu;
}

bool CreateArchiveDialog::checkMemoryUsage() {
    UInt64 decompressMem;
    const UInt64 memUsage = GetMemoryUsage_DecompMem(decompressMem);
    UString message;
    if (!CCompressDialog::OnOK(memUsage, system_, Get_MemUse_Spec(), message)) {
        QMessageBox::critical(this, QStringLiteral("7-Zip"),
                              QString::fromWCharArray(message.Ptr()));
        return false;
    }
    return true;
}
