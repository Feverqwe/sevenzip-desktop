#pragma once
#include "filemanager/external/qtexternalsessionowner.h"
#include <QObject>
#include <memory>
#include <optional>

class CPanel;
class ArchiveBackend;
struct CTempFileInfo;
class QWidget;
class QtOwnedTempLease;

// Application lifetime and presentation around the retained native observer.
class QtExternalFileTask final : public QObject,
                                 public IQtExternalSessionOwner,
                                 public std::enable_shared_from_this<QtExternalFileTask> {
    Q_OBJECT
  public:
    static std::shared_ptr<IQtExternalSessionOwner>
    Create(CPanel &source, const CTempFileInfo &file, const ArchiveBackend &backend,
           const QString &settings, const QString &prefix);
    static QList<QtExternalFileTask *> Active();
    ~QtExternalFileTask() override;
    CExitEventLauncher &Launcher() override;
    IQtProcessServices &ProcessServices() override;
    bool ReadyToLaunch() const override;
    bool CanPresent() const override;
    bool Update() override;
    void Launched(bool knownProcess) override;
    void PresentObservation(QtExternalObservation outcome) override;
    void Finished(QtExternalObservation outcome) override;
    void Show();
    void Stop() override;
  signals:
    void published(const QString &archive);

  private:
    QtExternalFileTask(CPanel &source, const CTempFileInfo &file, const ArchiveBackend &backend,
                       const QString &settings, const QString &prefix);
    bool Persist();
    void Initialize();
    void SaveCopy(QWidget *parent, bool fromUpdate = false);
    void Refresh();
    void WatchAssociationCopy();
    std::shared_ptr<const QtOwnedTempLease> InputLease() const;
    bool CheckChanges();
    int AskForUpdate();
    struct State;
    std::unique_ptr<State> state_;
};
