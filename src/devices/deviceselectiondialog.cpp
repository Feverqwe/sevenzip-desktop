#include "devices/deviceselectiondialog.h"
#include "archive/backend/archivebackend.h"
#include "archive/resources/archivenativeopen.h"
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QThread>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <atomic>

struct DeviceSelectionDialog::Job {
    std::atomic<bool> cancelled{false};
    bool listing = false;
    QList<ExternalDevice> devices;
    ExternalDevice device;
    std::shared_ptr<ArchiveDevice> source;
    DeviceAccessResult access;
    QString error;
    HRESULT result = E_FAIL;
};

struct DeviceSelectionDialog::Work {
    const std::shared_ptr<Job> job;
    const std::unique_ptr<QThread> worker;
    Work(std::shared_ptr<Job> state, const std::function<void()> &operation)
        : job(std::move(state)), worker(QThread::create([job = job, operation] {
              try {
                  operation();
              } catch (...) {
                  job->result = E_FAIL;
              }
          })) {}
    ~Work() { worker->wait(); }
};

namespace {
class CDeviceProgress final : public IOpenCallbackUI {
    const std::atomic<bool> &cancelled_;

  public:
    explicit CDeviceProgress(const std::atomic<bool> &cancelled) : cancelled_(cancelled) {}
    HRESULT Open_CheckBreak() override { return cancelled_ ? E_ABORT : S_OK; }
    HRESULT Open_SetTotal(const UInt64 *, const UInt64 *) override { return Open_CheckBreak(); }
    HRESULT Open_SetCompleted(const UInt64 *, const UInt64 *) override { return Open_CheckBreak(); }
    HRESULT Open_Finished() override { return Open_CheckBreak(); }
    HRESULT Open_CryptoGetTextPassword(BSTR *) override { return E_NOTIMPL; }
};
QString Size(quint64 size) { return QLocale().formattedDataSize(qint64(size)); }
} // namespace

DeviceSelectionServices DeviceSelectionServices::system() {
    return {EnumerateExternalDevices, [](const ExternalDevice &device, const auto &cancelled) {
                return OpenExternalDevice(device, true, cancelled);
            }};
}

DeviceSelectionDialog::DeviceSelectionDialog(std::shared_ptr<const ArchiveCodecCatalog> catalog,
                                             QWidget *parent, DeviceSelectionServices services)
    : QDialog(parent), catalog_(std::move(catalog)), services_(std::move(services)) {
    setObjectName("deviceSelectionDialog");
    setWindowTitle(tr("Open device"));
    setWindowModality(Qt::WindowModal);
    auto *layout = new QVBoxLayout(this);
    auto *description = new QLabel(
        tr("Read supported filesystems without modifying the device. Copy files to another disk."),
        this);
    description->setWordWrap(true);
    layout->addWidget(description);
    items_ = new QTreeWidget(this);
    items_->setObjectName("deviceItems");
    items_->setAccessibleName(tr("Devices and partitions"));
    items_->setRootIsDecorated(false);
    items_->setSelectionMode(QAbstractItemView::SingleSelection);
    items_->setHeaderLabels({tr("Name"), tr("Size"), tr("Status")});
    items_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    items_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    items_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    items_->header()->setStretchLastSection(false);
    items_->setMinimumSize(320, 100);
    layout->addWidget(items_, 1);
    message_ = new QLabel(this);
    message_->setObjectName("deviceMessage");
    message_->setTextFormat(Qt::PlainText);
    message_->setWordWrap(true);
    message_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(message_);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Cancel, this);
    open_ = buttons->button(QDialogButtonBox::Open);
    open_->setObjectName("deviceOpenButton");
    open_->setDefault(true);
    refresh_ = buttons->addButton(tr("Refresh"), QDialogButtonBox::ActionRole);
    refresh_->setObjectName("deviceRefreshButton");
    back_ = buttons->addButton(tr("Back"), QDialogButtonBox::ActionRole);
    back_->setObjectName("deviceBackButton");
    back_->hide();
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] { activateSelection(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &DeviceSelectionDialog::reject);
    connect(items_, &QTreeWidget::itemSelectionChanged, this, [this] { updateOpen(); });
    connect(items_, &QTreeWidget::itemActivated, this, [this] { activateSelection(); });
    connect(refresh_, &QPushButton::clicked, this, [this] { listDevices(); });
    connect(back_, &QPushButton::clicked, this, [this] {
        source_.reset();
        showDevices();
    });
    resize(620, 390);
    items_->setFocus();
    listDevices();
}

DeviceSelectionDialog::~DeviceSelectionDialog() {
    if (work_)
        work_->job->cancelled = true;
}

void DeviceSelectionDialog::reject() {
    if (work_) {
        closing_ = true;
        work_->job->cancelled = true;
        message_->setText(tr("Canceling…"));
        return;
    }
    selected_.Release();
    source_.reset();
    QDialog::reject();
}

void DeviceSelectionDialog::setWorking(bool working) {
    items_->setEnabled(!working);
    refresh_->setEnabled(!working);
    back_->setEnabled(!working);
    if (!working)
        items_->setFocus();
    updateOpen();
}

void DeviceSelectionDialog::updateOpen() {
    const auto *item = items_->currentItem();
    open_->setEnabled(!work_ && item && !item->isDisabled());
}

void DeviceSelectionDialog::start(const std::shared_ptr<Job> &job,
                                  const std::function<void()> &operation) {
    auto work = std::make_unique<Work>(job, operation);
    auto *worker = work->worker.get();
    connect(worker, &QThread::finished, this, [this, job] {
        if (!work_ || work_->job != job)
            return;
        // Result callbacks may restart or destroy the dialog after this join.
        auto completed = std::move(work_);
        completed->worker->wait();
        finish(job);
    });
    work_ = std::move(work);
    setWorking(true);
    worker->start();
}

void DeviceSelectionDialog::listDevices() {
    if (work_)
        return;
    source_.reset();
    selected_.Release();
    items_->clear();
    back_->hide();
    message_->setText(tr("Looking for external devices…"));
    auto job = std::make_shared<Job>();
    job->listing = true;
    const auto enumerate = services_.enumerate;
    start(job, [job, enumerate] {
        job->devices = enumerate(job->error);
        job->result = job->error.isEmpty() ? S_OK : E_FAIL;
    });
}

void DeviceSelectionDialog::showDevices() {
    items_->clear();
    items_->setHeaderLabels({tr("Name"), tr("Size"), tr("Status")});
    items_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    back_->hide();
    refresh_->show();
    for (const auto &device : devices_) {
        auto *item = new QTreeWidgetItem(items_, {device.name, Size(device.size),
                                                  device.mounted ? tr("In use") : tr("Read only")});
        item->setDisabled(device.mounted);
        item->setToolTip(0, device.id);
    }
    message_->setText(devices_.isEmpty()
                          ? tr("No external devices found. Connect a device and refresh.")
                          : tr("Select a device. Volumes in use must be unmounted first."));
    updateOpen();
}

void DeviceSelectionDialog::showPartitions() {
    items_->clear();
    items_->setHeaderLabels({tr("Name"), tr("Size"), tr("Partition type")});
    items_->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    back_->show();
    refresh_->hide();
    for (const auto &partition : source_->partitions())
        new QTreeWidgetItem(items_, {partition.name, Size(partition.size), partition.type});
    message_->setText(tr("Select a partition to open read only."));
    updateOpen();
}

void DeviceSelectionDialog::finish(const std::shared_ptr<Job> &job) {
    // Release the acquisition lease before controls permit an immediate retry.
    // A successful source retains the handle needed by its partition streams.
    job->access.handle.reset();
    setWorking(false);
    if (closing_) {
        job->source.reset();
        reject();
        return;
    }
    if (job->listing) {
        devices_ = job->devices;
        showDevices();
        if (job->result != S_OK)
            message_->setText(job->error.isEmpty() ? tr("Could not list external devices.")
                                                   : job->error);
        return;
    }
    if (job->result == S_OK) {
        source_ = std::move(job->source);
        selectedName_ = job->device.name;
        if (source_->partitions().size() == 1)
            selectPartition(0);
        else
            showPartitions();
        return;
    }
    if (job->access.status == DeviceAccessStatus::Cancelled || job->result == E_ABORT)
        message_->setText(tr("Device access canceled. You can try again."));
    else if (job->access.status == DeviceAccessStatus::PermissionRequired)
        message_->setText(tr("Read access was not authorized. You can try again."));
    else if (job->access.status == DeviceAccessStatus::Busy)
        message_->setText(tr("This device is in use. Unmount its volumes and try again."));
    else
        message_->setText(!job->error.isEmpty() ? job->error
                          : !job->access.error.isEmpty()
                              ? job->access.error
                              : tr("Could not read this device. It may have been disconnected."));
}

void DeviceSelectionDialog::activateSelection() {
    if (!open_->isEnabled())
        return;
    const int index = items_->indexOfTopLevelItem(items_->currentItem());
    if (source_) {
        selectPartition(index);
        return;
    }
    if (index < 0 || index >= devices_.size())
        return;
    message_->setText(tr("Opening device… System authorization may be required."));
    auto job = std::make_shared<Job>();
    job->device = devices_[index];
    const auto open = services_.open;
    const auto catalog = catalog_;
    start(job, [job, open, catalog] {
        job->access = open(job->device, [job] { return job->cancelled.load(); });
        if (job->cancelled || job->access.status != DeviceAccessStatus::Ready)
            return;
        CMyComPtr<IInStream> input;
        job->result = CreateDeviceInput(job->device, job->access.handle, &input);
        if (job->result != S_OK)
            return;
        CDeviceProgress progress(job->cancelled);
        job->result = ArchiveBackend::inspectDevice(catalog, input, job->device.size,
                                                    job->device.name, &progress, job->source);
    });
}

void DeviceSelectionDialog::selectPartition(int index) {
    if (source_->partitionInput(index, &selected_) != S_OK) {
        message_->setText(tr("Could not open this partition."));
        return;
    }
    selectedName_ += '/' + source_->partitions()[index].name;
    accept();
}
