#pragma once
#include "archive/resources/archivedevice.h"
#include "devices/deviceaccess.h"
#include <QDialog>
#include <functional>
#include <memory>

class QLabel;
class QPushButton;
class QTreeWidget;

// The dialog consumes the platform boundary; filesystem parsing stays in the
// original engine. Callbacks run on its owned worker and never touch widgets.
struct DeviceSelectionServices {
    std::function<QList<ExternalDevice>(QString &)> enumerate;
    std::function<DeviceAccessResult(const ExternalDevice &, const std::function<bool()> &)> open;
    static DeviceSelectionServices system();
};

class DeviceSelectionDialog final : public QDialog {
    Q_OBJECT
    struct Job;
    struct Work;
    std::shared_ptr<const ArchiveCodecCatalog> catalog_;
    DeviceSelectionServices services_;
    QList<ExternalDevice> devices_;
    std::shared_ptr<ArchiveDevice> source_;
    std::unique_ptr<Work> work_;
    QTreeWidget *items_;
    QLabel *message_;
    QPushButton *open_, *refresh_, *back_;
    CMyComPtr<IInStream> selected_;
    QString selectedName_;
    bool closing_ = false;
    void listDevices();
    void activateSelection();
    void setWorking(bool working);
    void updateOpen();
    void showDevices();
    void showPartitions();
    void start(const std::shared_ptr<Job> &job, const std::function<void()> &operation);
    void finish(const std::shared_ptr<Job> &job);
    void selectPartition(int index);

  public:
    explicit DeviceSelectionDialog(
        std::shared_ptr<const ArchiveCodecCatalog> catalog, QWidget *parent = nullptr,
        DeviceSelectionServices services = DeviceSelectionServices::system());
    ~DeviceSelectionDialog() override;
    IInStream *selectedStream() const { return selected_; }
    QString selectedName() const { return selectedName_; }
    void reject() override;
};
