#include "mount/qtmountsettings.h"
#include "archive/backend/archivebackend.h"
#include "archive/resources/archivereadonlyview.h"
#include "ui/controls/qtsourcesummary.h"
#include "ui/dialogs/qtdialoglayout.h"
#include "ui/theme/qtcommandicons.h"
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QSpinBox>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

namespace {
class MountPreparation;
class MountSettingsDialog final : public QDialog {
    enum class Phase { Preparing, Cancelling, Failed, Ready };
    Phase phase_ = Phase::Ready;
    std::unique_ptr<MountPreparation> preparation_;
    QDialogButtonBox *buttons_ = nullptr;
    QLabel *checking_ = nullptr;
    QLabel *detail_ = nullptr, *elapsed_ = nullptr, *preparationHint_ = nullptr;
    QProgressBar *progress_ = nullptr;
    QElapsedTimer elapsedTime_;
    QTimer progressTimer_;
    QFormLayout *form_ = nullptr;
    QSpinBox *quota_ = nullptr;
    QLineEdit *password_ = nullptr;
    QPushButton *start_ = nullptr;
    QLabel *cacheHint_ = nullptr;
    void BuildControls(const QString &label, const QString &password, bool checkReader);
    void Prepare(IUnknown *resource, const QString &password, const QString &folder);
    void PreparationFinished();
    void UpdatePreparation();
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
        : View(ArchiveBackend::retainReadOnlyView(resource, 0, password, {}, {}, folder, true)) {}
    void cancel() {
        View->cancel();
        requestInterruption();
    }
};

void MountSettingsDialog::BuildControls(const QString &label, const QString &archivePassword,
                                        bool checkReader) {
    phase_ = checkReader ? Phase::Preparing : Phase::Ready;
    setObjectName("archiveMountSettings");
    setWindowTitle(QCoreApplication::translate("QtArchiveMountManager", "Mount read-only"));
    QtUseContextDialogModality(*this);
    buttons_ = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    auto *layout = QtCreateDialogLayout(*this, *buttons_);
    const QString name = QFileInfo(QDir::cleanPath(label)).fileName();
    auto *source = new QtSourceSummary(QtCommandSymbol(QtCommandIcon::Folder),
                                       name.isEmpty() ? label : name, label, this);
    source->setObjectName("mountSourceSummary");
    layout->addWidget(source);
    checking_ = new QLabel(
        QCoreApplication::translate("QtArchiveMountManager", "Reading folder tree…"), this);
    checking_->setWordWrap(true);
    checking_->setVisible(checkReader);
    layout->addWidget(checking_);
    progress_ = new QProgressBar(this);
    progress_->setObjectName("mountPreparationProgress");
    progress_->setRange(0, 0);
    progress_->setVisible(checkReader);
    layout->addWidget(progress_);
    detail_ = new QLabel(this);
    detail_->setTextFormat(Qt::PlainText);
    detail_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    detail_->setVisible(checkReader);
    layout->addWidget(detail_);
    elapsed_ = new QLabel(this);
    elapsed_->setObjectName("mountPreparationElapsed");
    elapsed_->setVisible(checkReader);
    layout->addWidget(elapsed_);
    preparationHint_ = new QtWrappingLabel(
        QCoreApplication::translate(
            "QtArchiveMountManager",
            "Mount will be available when preparation finishes. Large folders on a network "
            "drive can take longer. File contents are not extracted during this check."),
        this);
    preparationHint_->setWordWrap(true);
    preparationHint_->setTextFormat(Qt::PlainText);
    preparationHint_->setVisible(checkReader);
    layout->addWidget(preparationHint_);
    form_ = QtCreateDialogForm();
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
    for (auto *field : {static_cast<QWidget *>(quota_), static_cast<QWidget *>(password_)})
        static_cast<QLabel *>(form_->labelForField(field))->setWordWrap(true);
    layout->addLayout(form_);
    cacheHint_ =
        new QtWrappingLabel(QCoreApplication::translate(
                                "QtArchiveMountManager",
                                "The cache stores extracted files. Files larger than this limit "
                                "cannot be read through the connection."),
                            this);
    cacheHint_->setObjectName("mountCacheHint");
    cacheHint_->setWordWrap(true);
    cacheHint_->setTextFormat(Qt::PlainText);
    layout->addWidget(cacheHint_);
    start_ = buttons_->addButton(QCoreApplication::translate("QtArchiveMountManager", "Mount"),
                                 QDialogButtonBox::AcceptRole);
    start_->setDefault(true);
    start_->setEnabled(phase_ == Phase::Ready);
    ShowCache(!checkReader);
    connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    resize(520, sizeHint().height());
}

void MountSettingsDialog::ShowCache(bool required) {
    form_->setRowVisible(quota_, required);
    cacheHint_->setVisible(required);
}

void MountSettingsDialog::Prepare(IUnknown *resource, const QString &password,
                                  const QString &folder) {
    if (!resource)
        return;
    preparation_ = std::make_unique<MountPreparation>(resource, password, folder);
    connect(preparation_.get(), &QThread::finished, this,
            &MountSettingsDialog::PreparationFinished);
    elapsedTime_.start();
    connect(&progressTimer_, &QTimer::timeout, this, &MountSettingsDialog::UpdatePreparation);
    progressTimer_.start(200);
    UpdatePreparation();
    preparation_->start();
}

void MountSettingsDialog::UpdatePreparation() {
    elapsed_->setText(QCoreApplication::translate("QtArchiveMountManager", "Elapsed: %1 s")
                          .arg(elapsedTime_.elapsed() / 1000));
    if (phase_ != Phase::Preparing)
        return;
    const auto snapshot = preparation_->View->preparationProgress();
    using Stage = ArchiveReadOnlyView::PreparationProgress::Stage;
    switch (snapshot.Phase) {
    case Stage::ReadingDirectories:
        checking_->setText(
            QCoreApplication::translate("QtArchiveMountManager", "Reading folder tree…"));
        break;
    case Stage::ListingEntries:
        checking_->setText(
            QCoreApplication::translate("QtArchiveMountManager", "Preparing file list: %1 of %2")
                .arg(snapshot.Processed)
                .arg(snapshot.Total));
        break;
    case Stage::CheckingAccess:
        checking_->setText(
            QCoreApplication::translate("QtArchiveMountManager", "Checking direct access: %1 of %2")
                .arg(snapshot.Processed)
                .arg(snapshot.Total));
        break;
    }
    if (snapshot.Total) {
        progress_->setRange(0, 1000);
        progress_->setValue(
            int(std::min(snapshot.Processed, snapshot.Total) * 1000 / snapshot.Total));
    } else
        progress_->setRange(0, 0);
    detail_->setToolTip(snapshot.CurrentPath);
    detail_->setText(detail_->fontMetrics().elidedText(snapshot.CurrentPath, Qt::ElideMiddle,
                                                       detail_->contentsRect().width()));
}

void MountSettingsDialog::PreparationFinished() {
    UpdatePreparation();
    progressTimer_.stop();
    progress_->hide();
    detail_->hide();
    preparationHint_->hide();
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
    elapsed_->hide();
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
