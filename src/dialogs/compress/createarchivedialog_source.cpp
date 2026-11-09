#include "7zip/UI/FileManager/PasswordDialogRes.h"
#include "7zip/UI/FileManager/resource.h"
#include "7zip/UI/GUI/CompressDialogRes.h"
#include "Windows/ErrorMsg.h"
#include "Windows/FileFind.h"
#include "Windows/FileName.h"
#include "archive/callbacks/qtguimessages.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/operations/Update.h"
#include "dialogs/compress/createarchivedialog.h"
#include "fileops/links/qtlinkservices.h"
#include "fileops/native/correctfspath.h"
#include "fileops/transfer/qttransfertargetname.h"
#include "settings/ViewSettings.h"
#include "ui/controls/qtdisclosure.h"
#include "ui/lifecycle/qttaskpresentation.h"
#include <QCoreApplication>
#include <QDirIterator>
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
                return QCoreApplication::translate("CopyRedesign", "Not a folder: %1")
                    .arg(QDir::toNativeSeparators(path));
#ifdef Q_OS_UNIX
            if (!entry.isExecutable())
                return QCoreApplication::translate("CopyRedesign", "Cannot access folder: %1")
                    .arg(QDir::toNativeSeparators(path));
#endif
            if (!entry.isWritable())
                return QCoreApplication::translate("CopyRedesign", "The folder is read-only: %1")
                    .arg(QDir::toNativeSeparators(path));
            return {};
        }
        const QString parent = entry.absolutePath();
        if (parent == path)
            return {};
        path = parent;
    }
}

QString ExistingVolume(const UString &basePath) {
    const QFileInfo base(ToQString(basePath));
    const QString prefix = base.fileName() + u'.';
    QDirIterator entries(base.absolutePath(),
                         QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    while (entries.hasNext()) {
        const QFileInfo entry(entries.next());
        const QString name = entry.fileName();
        if (!name.startsWith(prefix, Qt::CaseInsensitive))
            continue;
        const QString suffix = name.mid(prefix.size());
        const uint number = suffix.toUInt();
        if (number == 0 || suffix != QString::number(number).rightJustified(3, u'0'))
            continue;
        const UString candidate = basePath + ToUString(u'.' + suffix);
        if (NWindows::NFile::NFind::DoesFileOrDirExist(us2fs(candidate)))
            return ToQString(candidate);
    }
    return {};
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
        if (protect && password_->text().isEmpty()) {
            ShowErrorMessageHwndResQt(this, IDD_PASSWORD);
            password_->setFocus();
            return;
        }
        const auto error = CCompressDialog::CheckPassword(
            protect ? password_->text() : QString(),
            protect ? passwordConfirmation_->text() : QString(), showPassword_->isChecked(),
            format_->currentText(), GetEncryptionMethodSpec());
        if (error != CCompressDialog::kPasswordOK) {
            const UInt32 id = error == CCompressDialog::kPasswordUseAscii  ? IDS_PASSWORD_USE_ASCII
                              : error == CCompressDialog::kPasswordTooLong ? IDS_PASSWORD_TOO_LONG
                                                                           : IDS_PASSWORD_NOT_MATCH;
            ShowErrorMessageHwndResQt(this, id);
            auto *field =
                error == CCompressDialog::kPasswordNotMatch ? passwordConfirmation_ : password_;
            field->setFocus();
            field->selectAll();
            return;
        }
    }
    if (!checkMemoryUsage()) {
        showExpertOptions(memUse_);
        return;
    }
    SaveOptionsInMem();
    NCompressDialog::CInfo volumeInfo;
    const auto volumeResult = volume_->isEnabled()
                                  ? CCompressDialog::OnOK(volume_->currentText(), volumeInfo)
                                  : CCompressDialog::kVolumeOK;
    if (volumeResult == CCompressDialog::kIncorrectVolumeSize) {
        checkVolumeSizes(volumeResult, volumeInfo);
        return;
    }
    UString draftName(name_->text().toStdWString().c_str());
    draftName.Trim();
    const UString leaf(draftName.Ptr(unsigned(draftName.ReverseFind_PathSepar() + 1)));
    const bool hasFileName = !leaf.IsEmpty() && !leaf.IsEqualTo(".") && !leaf.IsEqualTo("..");
    const QString resolved = archivePath();
    const UString nativeResolved = ToUString(QDir::toNativeSeparators(resolved));
    const bool relativeName = NWindows::NFile::NName::GetRootPrefixSize(draftName) == 0;
    const UString nativeName = ToUString(QDir::toNativeSeparators(name_->text()));
    const UString nativeFolder = ToUString(QDir::toNativeSeparators(archiveFolder_->text()));
    const int invalidNameCharacter = FindInvalidWindowsPathCharacter(nativeName);
    const bool invalidName =
        invalidNameCharacter >= 0 || HasEmptyWindowsPathComponent(nativeName, nativeResolved);
    const bool invalidFolder =
        relativeName && (FindInvalidWindowsPathCharacter(nativeFolder) >= 0 ||
                         HasEmptyWindowsPathComponent(nativeFolder, nativeResolved));
    const bool singleVolume = volumeInfo.VolumeSizes.IsEmpty();
    CArchivePath archive;
    archive.VolExtension = archiveNameState_.Get_ArcInfoEx().GetMainExt();
    if (!codecs_ && archive.VolExtension.IsEmpty())
        archive.VolExtension = format_->currentText().toStdWString().c_str();
    archive.BaseExtension = archive.VolExtension;
    archive.ParseFromPath(ToUString(resolved), k_ArcNameMode_Smart);
    const QString output = ToQString(archive.GetFinalPath());
    const QFileInfo target(output);
    const bool danglingTarget = singleVolume && target.isSymLink() && !target.exists();
    bool occupiedName = danglingTarget;
    QString pathError =
        !hasFileName || invalidName || invalidFolder || resolved.isEmpty() || danglingTarget
            ? InvalidArchivePath()
            : ArchiveDirectoryError(target.absolutePath());
    const bool blockedDirectory = !pathError.isEmpty() && hasFileName && !invalidName &&
                                  !invalidFolder && !resolved.isEmpty() && !danglingTarget;
    if (pathError.isEmpty() && target.isDir()) {
        occupiedName = true;
        pathError =
            QCoreApplication::translate("CreateArchiveDialog", "Archive path is a folder: %1")
                .arg(QDir::toNativeSeparators(output));
    }
    if (danglingTarget)
        pathError += '\n' + QDir::toNativeSeparators(output);
    if (pathError.isEmpty() && singleVolume && target.exists()) {
        NWindows::NFile::NFind::CFileInfo existing;
        const UString nativePath = ToUString(QDir::toNativeSeparators(output));
        if (existing.Find_FollowLink(us2fs(nativePath)) && existing.IsReadOnly()) {
            occupiedName = true;
            pathError =
                QCoreApplication::translate("SevenZipDiagnostics", "The file is read-only") + '\n' +
                QDir::toNativeSeparators(output);
        }
    }
    if (pathError.isEmpty() && !singleVolume) {
        const UString first = archive.GetFinalVolPath() + L".001";
        QString occupied;
        for (const UString &candidate : {archive.GetFinalPath(), first}) {
            if (NWindows::NFile::NFind::DoesFileOrDirExist(us2fs(candidate))) {
                occupied = ToQString(candidate);
                break;
            }
        }
        if (occupied.isEmpty())
            occupied = ExistingVolume(archive.GetFinalVolPath());
        if (!occupied.isEmpty()) {
            occupiedName = true;
            pathError =
                QCoreApplication::translate("SevenZipDiagnostics", "The file already exists") +
                '\n' + occupied;
        }
    }
    if (!pathError.isEmpty()) {
        // Keep the draft available for correction before handing it to UpdateArchive.
        const std::wstring message = pathError.toStdWString();
        ShowErrorMessageQt(this, message.c_str());
        auto *field = name_;
        if (archiveFolder_->text().contains(QChar::Null) || (invalidFolder && !invalidName))
            field = archiveFolder_;
        else if (hasFileName && !invalidName && !name_->text().contains(QChar::Null) &&
                 relativeName) {
            CCompressDialog base = archiveNameState_;
            base.m_ArchivePath.clear();
            QString directory;
            if (!base.GetFinalPath_Smart(directory) ||
                !ArchiveDirectoryError(QFileInfo(directory).absoluteFilePath()).isEmpty())
                field = archiveFolder_;
        }
        field->setFocus();
        if (field == archiveFolder_)
            archiveFolder_->selectAll();
        else if (invalidNameCharacter >= 0)
            name_->setSelection(invalidNameCharacter, 1);
        else if (occupiedName || !hasFileName || blockedDirectory) {
            const auto text = QDir::fromNativeSeparators(name_->text());
            const int leafStart = int(text.lastIndexOf(u'/') + 1);
            if (blockedDirectory && leafStart > 0)
                name_->setSelection(0, leafStart);
            else if (leafStart == text.size())
                name_->setCursorPosition(leafStart);
            else
                name_->setSelection(leafStart, int(text.size()) - leafStart);
        }
        return;
    }
    const auto sourcesAvailable = [&] {
        for (const auto &source : requiredSources_) {
            const QFileInfo entry(source);
            if (QtIsFileSystemLink(source))
                continue;
            const QString message =
                entry.exists()
                    ? QtTransferSourceError(entry, false)
                    : QCoreApplication::translate("mainwindow_filelist", "Path not found: ") +
                          QDir::toNativeSeparators(source);
            if (message.isEmpty())
                continue;
            ShowErrorMessageQt(this, message.toStdWString().c_str());
            return false;
        }
        return true;
    };
    if (!sourcesAvailable() || !checkVolumeSizes(volumeResult, volumeInfo) ||
        (volumeResult == CCompressDialog::kConfirmSmallVolume && !sourcesAvailable()))
        return;
    // Existing outputs are opened/validated by source UpdateArchive after OK,
    // just as for the original form. The dialog must not reject update intent.
    const QString acceptedPath = archive.BaseExtension.IsEmpty() ? resolved : output;
    auto request = readRequest(acceptedPath);
    CCompressDialog path;
    path.m_ArchivePath = acceptedPath;
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
    QStringList selected, required;
    FOR_VECTOR(i, paths) {
        const auto &entry = paths[i];
        if (!entry.Include)
            continue;
        const QString path = QString::fromWCharArray(entry.Path.Ptr());
        selected.append(path);
        if (QDir::isAbsolutePath(path) &&
            (!entry.Props.WildcardMatching || !DoesNameContainWildcard(entry.Path)))
            required.append(path);
    }
    const QString directory =
        QFileInfo(QString::fromWCharArray(source.Info.ArcPath.Ptr())).absolutePath();
    CreateArchiveDialog dialog(directory, selected, parent, std::move(codecs), &source);
    dialog.requiredSources_ = std::move(required);
    if (dialog.exec() != QDialog::Accepted)
        return E_ABORT;
    source.Info = dialog.sourceInfo();
    return S_OK;
}
