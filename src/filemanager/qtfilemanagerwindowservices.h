#pragma once
class CApp;

enum class QtFileManagerCloseReason { Request, Destruction };

// Native WM_CLOSE functional body calls these narrow Qt/platform services.
class IQtFileManagerWindowServices {
  public:
    virtual ~IQtFileManagerWindowServices() = default;
    virtual bool CanCloseNow() const = 0;
    virtual void Deactivate() = 0;
    virtual void ExitExternalWorkers(bool hardExit) = 0;
    virtual void RevokeDropTarget() = 0;
    virtual void QuiesceOperations() = 0;
    virtual bool WindowWasCreated() const = 0;
    virtual void SaveWindowInfo() = 0;
};

bool CloseFileManager(CApp &app, IQtFileManagerWindowServices &services,
                      QtFileManagerCloseReason reason = QtFileManagerCloseReason::Request);
