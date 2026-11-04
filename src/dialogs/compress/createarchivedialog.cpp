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
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QToolButton>

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
    auto *scroll = createOptionsScrollArea(this);
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
