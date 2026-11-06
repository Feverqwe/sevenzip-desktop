#include "7zip/UI/GUI/CompressDialogRes.h"
#include "archive/callbacks/qtguimessages.h"
#include "dialogs/compress/createarchivedialog.h"
#include "filemanager/FormatUtils.h"
#include "filemanager/SplitUtils.h"
#include "localization/qtlangcontrols.h"
#include "ui/controls/qtdisclosure.h"

void CreateArchiveDialog::createVolumeOptions(QFormLayout *form) {
    volume_ = new QComboBox(this);
    volume_->setObjectName("archiveVolumeSize");
    volume_->setEditable(true);
    volume_->setInsertPolicy(QComboBox::NoInsert);
    AddVolumeItems(*volume_);
    volume_->setCurrentIndex(-1);
    QtBindLocalToolTip(
        volume_, "createarchivedialog_volumes",
        QT_TRANSLATE_NOOP("createarchivedialog_volumes",
                          "For example: 10M or 10M 20M. Leave empty for a single archive."));
    auto *volumeForm = new QFormLayout;
    volumeForm->setRowWrapPolicy(QFormLayout::WrapAllRows);
    volumeForm->addRow("Split to volumes, bytes:", volume_);
    form->addRow(volumeForm);
}

bool CreateArchiveDialog::checkVolumeSizes() {
    if (!volume_->isEnabled())
        return true;
    NCompressDialog::CInfo info;
    const auto result = CCompressDialog::OnOK(volume_->currentText(), info);
    if (result == CCompressDialog::kIncorrectVolumeSize) {
        additionalOptions_->setExpanded(true);
        ShowErrorMessageHwndResQt(this, IDS_INCORRECT_VOLUME_SIZE);
        volume_->setFocus();
        return false;
    }
    if (result == CCompressDialog::kConfirmSmallVolume)
        return QMessageBox::question(
                   this, "7-Zip",
                   QString::fromWCharArray(MyFormatNew(IDS_SPLIT_CONFIRM, info.VolumeSizes.Back())),
                   QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel) == QMessageBox::Yes;
    return true;
}
