#pragma once

struct CExitEventLauncher;
class IQtProcessServices;
enum class QtExternalObservation { NotStarted, Completed, Unknown, Stopped, Failed };

// Native observation retains this application owner, never a file window.
class IQtExternalSessionOwner {
  public:
    virtual ~IQtExternalSessionOwner() = default;
    virtual bool ReadyToLaunch() const = 0;
    virtual CExitEventLauncher &Launcher() = 0;
    virtual IQtProcessServices &ProcessServices() = 0;
    virtual bool CanPresent() const = 0;
    virtual bool Update() = 0;
    virtual void Launched(bool knownProcess) = 0;
    // Observer-thread notifications are delivered by the owner on the GUI thread.
    virtual void PresentObservation(QtExternalObservation outcome) = 0;
    virtual void Finished(QtExternalObservation outcome) = 0;
    virtual void Stop() = 0;
};
