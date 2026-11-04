#include "mount/qtmountsettings.h"
#include "archive/backend/archivebackend.h"
#include "archive/resources/archivereadonlyview.h"
#include "help/qthelpservices.h"
#include "ui/dialogs/qtdialoglayout.h"
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QShortcut>
#include <QSpinBox>
#include <QThread>
#include <QVBoxLayout>

namespace {
class MountPreparation;
class MountSettingsDialog final : public QDialog {
    enum class Phase { Preparing, Cancelling, Failed, Ready };
    Phase phase_ = Phase::Ready;
    std::unique_ptr<MountPreparation> preparation_;
    QDialogButtonBox *buttons_ = nullptr;
    QLabel *checking_ = nullptr;
    QFormLayout *form_ = nullptr;
    QSpinBox *quota_ = nullptr;
    QLineEdit *password_ = nullptr;
    QPushButton *help_ = nullptr, *start_ = nullptr;
    QShortcut *shortcut_ = nullptr;
    void BuildControls(const QString &label, const QString &password, bool checkReader);
    void Prepare(IUnknown *resource, const QString &password, const QString &folder);
    void PreparationFinished();
    void ShowCache(bool required);

  public:
    using QDialog::QDialog;
    ~MountSettingsDialog() override;
    QtMountOptions Run(const QString &label, const QString &password, IUnknown *resource,
                       const QString &folder);
    void accept() override;
    void reject() override;
};

class MountPreparation final : public QThread {
    void run() override {
        Result = ArchiveBackend::prepareReadOnlyView(View, Error);
        if (Result == S_OK)
            Result = View->needsCache(CacheRequired);
    }

  public:
    std::shared_ptr<ArchiveReadOnlyView> View;
    HRESULT Result = E_FAIL;
    QString Error;
    bool CacheRequired = true;
    MountPreparation(IUnknown *resource, const QString &password, const QString &folder)
        : View(ArchiveBackend::retainReadOnlyView(resource, 0, password, {}, {}, folder)) {}
    void cancel() {
        View->cancel();
        requestInterruption();
    }
};

void ShowCacheHelp(QWidget *parent) {
    QtOpenHelpText(
        QCoreApplication::translate("QtArchiveMountManager", "Temporary cache"),
        QCoreApplication::translate(
            "QtArchiveMountManager",
            "Sevenzip connects the folder currently open in the file manager. It reuses the open "
            "reader and any temporary container files already created.\n\n"
            "Files that support direct reading are passed to other applications without "
            "extraction. For example, a ZIP inside a filesystem in a QCOW image is read as an "
            "ordinary ZIP file.\n\n"
            "When connecting a folder inside a compressed archive, files may need decoding. "
            "You approve extraction once for this mount. Files are then decoded when another "
            "application reads them, "
            "and kept in a temporary disk cache. The source remains read-only.\n\n"
            "The limit is the total size of these extracted files. Older cached files are "
            "removed to make room and may need to be extracted again. If one file exceeds "
            "the limit, that file cannot be read through this connection; other files and "
            "the connection remain available. Directly read files are not subject to this limit."),
        parent);
}

class MountLabel final : public QLabel {
  public:
    using QLabel::QLabel;

  protected:
    void resizeEvent(QResizeEvent *event) override {
        QLabel::resizeEvent(event);
        // As in the benchmark's wrapped-caption boundary, native layouts can
        // otherwise use the wider layout hint when calculating this height.
        setMinimumHeight(heightForWidth(event->size().width()));
    }
};
void MountSettingsDialog::BuildControls(const QString &label, const QString &archivePassword,
                                        bool checkReader) {
    phase_ = checkReader ? Phase::Preparing : Phase::Ready;
    setObjectName("archiveMountSettings");
    setWindowTitle(QCoreApplication::translate("QtArchiveMountManager", "Mount read-only"));
    QtUseContextDialogModality(*this);
    buttons_ = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Help, this);
    help_ = buttons_->button(QDialogButtonBox::Help);
    help_->setObjectName("mountCacheHelpButton");
    auto *layout = QtCreateDialogLayout(*this, *buttons_);
    auto *source = new MountLabel(label, this);
    source->setTextFormat(Qt::PlainText);
    source->setWordWrap(true);
    layout->addWidget(source);
    checking_ = new QLabel(
        QCoreApplication::translate("QtArchiveMountManager", "Checking file access…"), this);
    checking_->setWordWrap(true);
    checking_->setVisible(checkReader);
    layout->addWidget(checking_);
    form_ = new QFormLayout;
    form_->setRowWrapPolicy(QFormLayout::WrapAllRows);
    form_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    quota_ = new QSpinBox(this);
    quota_->setObjectName("mountCacheLimit");
    quota_->setMinimumWidth(160);
    quota_->setRange(16, 1048576);
    quota_->setValue(1024);
    quota_->setSuffix(QCoreApplication::translate("QtArchiveMountManager", " MiB"));
    form_->addRow(QCoreApplication::translate("QtArchiveMountManager", "Temporary cache limit:"),
                  quota_);
    password_ = new QLineEdit(archivePassword, this);
    password_->setObjectName("mountArchivePassword");
    password_->setMinimumWidth(160);
    password_->setEchoMode(QLineEdit::Password);
    form_->addRow(
        QCoreApplication::translate("QtArchiveMountManager", "Archive password (if needed):"),
        password_);
    layout->addLayout(form_);
    start_ = buttons_->addButton(QCoreApplication::translate("QtArchiveMountManager", "Mount"),
                                 QDialogButtonBox::AcceptRole);
    start_->setDefault(true);
    start_->setEnabled(phase_ == Phase::Ready);
    shortcut_ = new QShortcut(QKeySequence::HelpContents, this);
    const auto showHelp = [this] {
        if (quota_->isVisible())
            ShowCacheHelp(this);
    };
    connect(buttons_, &QDialogButtonBox::helpRequested, this, showHelp);
    connect(shortcut_, &QShortcut::activated, this, showHelp);
    ShowCache(!checkReader);
    connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    resize(520, sizeHint().height());
}

void MountSettingsDialog::ShowCache(bool required) {
    form_->setRowVisible(quota_, required);
    help_->setVisible(required);
    shortcut_->setEnabled(required);
}

void MountSettingsDialog::Prepare(IUnknown *resource, const QString &password,
                                  const QString &folder) {
    if (!resource)
        return;
    preparation_ = std::make_unique<MountPreparation>(resource, password, folder);
    connect(preparation_.get(), &QThread::finished, this,
            &MountSettingsDialog::PreparationFinished);
    preparation_->start();
}

void MountSettingsDialog::PreparationFinished() {
    if (phase_ == Phase::Cancelling) {
        phase_ = Phase::Failed;
        QDialog::reject();
        return;
    }
    if (preparation_->Result != S_OK) {
        phase_ = Phase::Failed;
        checking_->setText(preparation_->Error.isEmpty()
                               ? QCoreApplication::translate("QtArchiveMountManager",
                                                             "Could not check file access.")
                               : preparation_->Error);
        return;
    }
    ShowCache(preparation_->CacheRequired);
    checking_->hide();
    phase_ = Phase::Ready;
    start_->setEnabled(true);
    resize(width(), sizeHint().height());
}

void MountSettingsDialog::accept() {
    if (phase_ == Phase::Ready)
        QDialog::accept();
}

void MountSettingsDialog::reject() {
    if (phase_ == Phase::Preparing) {
        phase_ = Phase::Cancelling;
        preparation_->cancel();
        checking_->setText(QCoreApplication::translate("QtArchiveMountManager", "Cancelling…"));
        buttons_->setEnabled(false);
    } else if (phase_ != Phase::Cancelling)
        QDialog::reject();
}

MountSettingsDialog::~MountSettingsDialog() {
    // Retire the worker while its view and dialog consumers still exist,
    // including unwinding before Run reaches its normal join.
    if (preparation_) {
        if (preparation_->isRunning())
            preparation_->cancel();
        preparation_->wait();
    }
}

QtMountOptions MountSettingsDialog::Run(const QString &label, const QString &password,
                                        IUnknown *resource, const QString &folder) {
    BuildControls(label, password, resource != nullptr);
    Prepare(resource, password, folder);
    const bool accepted = exec() == QDialog::Accepted;
    if (preparation_) {
        if (!accepted)
            preparation_->cancel();
        preparation_->wait();
    }
    if (!accepted)
        return {};
    const bool cacheRequired = !preparation_ || preparation_->CacheRequired;
    return {true, cacheRequired ? quint64(quota_->value()) * 1024 * 1024 : 0, password_->text(),
            preparation_ ? preparation_->View : nullptr};
}
} // namespace

QtMountOptions QtAskMountOptions(QWidget *owner, const QString &label,
                                 const QString &archivePassword, IUnknown *resource,
                                 const QString &folder) {
    MountSettingsDialog settings(owner);
    return settings.Run(label, archivePassword, resource, folder);
}
