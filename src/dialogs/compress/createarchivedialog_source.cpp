#include "archive/codecs/archivecodeccatalog.h"
#include "dialogs/compress/createarchivedialog.h"

// Qt controls for the source CCompressDialog::OnInit/OnOK DTO. The source
// ShowDialog remains responsible for all update options and properties.
void CreateArchiveDialog::initializeSourceControls(const NCompressDialog::CInfo &info) {
    QString name;
    archiveNameState_.SetArcPathFields(QString::fromWCharArray(info.ArcPath.Ptr()), name, true);
    archiveNameState_.SetArchiveName(name);
    name_->setText(archiveNameState_.m_ArchivePath);
    archiveFolder_->setText(QDir::toNativeSeparators(archiveNameState_.DirPrefix));
    updateMode_->setCurrentIndex(updateMode_->findData(int(info.UpdateMode)));
    pathMode_->setCurrentIndex(pathMode_->findData(int(info.PathMode)));
    sharedFiles_->setChecked(info.OpenShareForWrite);
    deleteAfterCompressing_->setChecked(info.DeleteAfterCompressing);
    if (password_) {
        password_->setText(info.Password);
        passwordConfirmation_->setText(info.Password);
    }
    if (info.SFXMode && sfx_->isEnabled())
        sfx_->setChecked(true);
}

NCompressDialog::CInfo CreateArchiveDialog::sourceInfo() const {
    const auto request = accepted_ ? *accepted_ : readRequest({});
    NCompressDialog::CInfo info = request.Options;
    info.Level = request.SourceLevel;
    return info;
}

CreateArchiveDialog::AcceptedRequest CreateArchiveDialog::readRequest(const QString &target) const {
    // Source GetLevelSpec returns Auto for an empty or single-entry combo.
    // The standalone CLI options retain their normalized effective level.
    AcceptedRequest request{compressionOptions(), CCompressDialog::GetComboValue(*level_, 1)};
    auto &info = request.Options;
    info.KeepName = archiveNameState_.Info.KeepName;
    info.ArcPath = target.toStdWString().c_str();
    info.FormatIndex = codecs_ ? codecs_->formatIndex(info.format) : -1;
    info.Password = password();
    info.EncryptHeadersIsAllowed =
        NCompressDialog::GetFormatInfo(format_->currentText()).EncryptFileNames_();
    info.EncryptHeaders = encryptHeaders();
    return request;
}

HRESULT CreateArchiveDialog::PresentSource(CCompressDialog &source,
                                           const CObjectVector<NWildcard::CCensorPath> &paths,
                                           std::shared_ptr<const ArchiveCodecCatalog> codecs,
                                           QWidget *parent) {
    QStringList selected;
    FOR_VECTOR(i, paths) {
        if (paths[i].Include)
            selected.append(QString::fromWCharArray(paths[i].Path.Ptr()));
    }
    const QString directory =
        QFileInfo(QString::fromWCharArray(source.Info.ArcPath.Ptr())).absolutePath();
    CreateArchiveDialog dialog(directory, selected, parent, std::move(codecs), &source);
    if (dialog.exec() != QDialog::Accepted)
        return E_ABORT;
    source.Info = dialog.sourceInfo();
    return S_OK;
}
