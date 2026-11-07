#include "7zip/UI/FileManager/resource.h"
#include "7zip/UI/GUI/CompressDialogRes.h"
#include "Windows/ErrorMsg.h"
#include "Windows/FileName.h"
#include "archive/callbacks/qtguimessages.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "dialogs/compress/createarchivedialog.h"
#include "ui/controls/qtdisclosure.h"
#include "ui/lifecycle/qttaskpresentation.h"
#include <QCoreApplication>
#include <QEvent>
#include <QFileInfo>

namespace {
QString InvalidArchivePath() {
    return QCoreApplication::translate("CreateArchiveDialog", "Incorrect archive path");
}

QString ArchiveDirectoryError(QString path) {
    for (;;) {
        const QFileInfo entry(path);
        if (entry.exists() || entry.isSymLink()) {
            if (!entry.isDir())
                return InvalidArchivePath();
            if (!entry.isWritable())
                return QCoreApplication::translate("CopyRedesign", "The destination is read-only.");
            return {};
        }
        const QString parent = entry.absolutePath();
        if (parent == path)
            return {};
        path = parent;
    }
}
} // namespace

// Qt controls for the source CCompressDialog::OnInit/OnOK DTO. The source
// ShowDialog remains responsible for all update options and properties.
void CreateArchiveDialog::initializeSourceControls(const NCompressDialog::CInfo &info) {
    QString name;
    archiveNameState_.SetArcPathFields(QString::fromWCharArray(info.ArcPath.Ptr()), name, true);
    archiveNameState_.SetArchiveName(name);
    name_->setText(archiveNameState_.m_ArchivePath);
    archiveFolder_->setText(QDir::toNativeSeparators(archiveNameState_.DirPrefix));
    const int updateIndex = updateMode_->findData(int(info.UpdateMode));
    updateMode_->setCurrentIndex(
        updateIndex >= 0 ? updateIndex
                         : updateMode_->findData(int(NCompressDialog::NUpdateMode::kAdd)));
    pathMode_->setCurrentIndex(pathMode_->findData(int(info.PathMode)));
    sharedFiles_->setChecked(info.OpenShareForWrite);
    if (password_) {
        password_->setText(info.Password);
        passwordConfirmation_->setText(info.Password);
    }
}

QString CreateArchiveDialog::archivePath() const {
    if (archiveFolder_->text().contains(QChar::Null))
        return {};
    QString result;
    archiveNameState_.GetFinalPath_Smart(result);
    return result;
}

void CreateArchiveDialog::validateAndAccept() {
    const QtTaskPresentation::QuestionScope validation;
    accepted_.reset();
    if (!updateProtectionControls()) {
        passwordEnabled_->setFocus();
        return;
    }
    if (parameters_ && parameters_->text().contains(QChar::Null)) {
        showExpertOptions(parameters_);
        return;
    }
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
    if (!checkMemoryUsage()) {
        showExpertOptions(memUse_);
        return;
    }
    SaveOptionsInMem();
    UString draftName(name_->text().toStdWString().c_str());
    draftName.Trim();
    const QString resolved = archivePath();
    const QFileInfo target(resolved);
    const QString pathError = draftName.IsEmpty() || resolved.isEmpty() || target.isDir()
                                  ? InvalidArchivePath()
                                  : ArchiveDirectoryError(target.absolutePath());
    if (!pathError.isEmpty()) {
        // Keep the draft available for correction before handing it to UpdateArchive.
        const std::wstring message = pathError.toStdWString();
        ShowErrorMessageQt(this, message.c_str());
        auto *field = name_;
        if (archiveFolder_->text().contains(QChar::Null))
            field = archiveFolder_;
        else if (!draftName.IsEmpty() && !name_->text().contains(QChar::Null) &&
                 NWindows::NFile::NName::GetRootPrefixSize(draftName) == 0) {
            CCompressDialog base = archiveNameState_;
            base.m_ArchivePath.clear();
            QString directory;
            if (!base.GetFinalPath_Smart(directory) ||
                !ArchiveDirectoryError(QFileInfo(directory).absoluteFilePath()).isEmpty())
                field = archiveFolder_;
        }
        field->setFocus();
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
#ifdef Q_OS_WIN
    options.PreserveATime = m_RegistryInfo.PreserveATime;
#endif
    options.Options = parameterOptions();
    options.Options.Trim();
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

QString CreateArchiveDialog::target() const {
    return accepted_ ? QString::fromWCharArray(accepted_->Options.ArcPath.Ptr()) : QString();
}

QString CreateArchiveDialog::password() const {
    if (accepted_)
        return accepted_->Options.Password;
    return password_ && password_->isEnabled() ? password_->text() : QString();
}

bool CreateArchiveDialog::encryptHeaders() const {
    if (accepted_)
        return accepted_->Options.EncryptHeaders;
    return encryptHeaders_ && encryptHeaders_->isEnabled() && encryptHeaders_->isChecked();
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
