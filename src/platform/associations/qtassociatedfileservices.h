// OS-association boundary for the source external-open process workflow.
#pragma once
#include "filemanager/external/qtexternalapplications.h"
#include "platform/process/qtexternalprocess.h"
#include <QUrl>
#include <functional>

class QtAssociatedFileServices final : public IQtExternalFileServices {
    std::function<bool(const QUrl &)> _opener;
    QtNativeProcessServices _processServices;
    std::function<bool(CPanel &, const QtArchiveUpdateBody &)> _updateRunner;
    std::function<bool()> _updateReady;
    std::function<bool(const QString &)> _hasAssociation;

  public:
    explicit QtAssociatedFileServices(
        std::function<bool(const QUrl &)> opener,
        std::function<bool(CPanel &, const QtArchiveUpdateBody &)> updateRunner = {},
        std::function<bool()> updateReady = {},
        std::function<bool(const QString &)> hasAssociation = {});
    bool ConfirmArchiveFallback(const UString &path, QWidget *window,
                                const QString &settingsPath) override;
    bool CanHandleExternalUpdate() const override;
    bool RunArchiveUpdate(CPanel &panel, const QtArchiveUpdateBody &body) override;
    HRESULT StartApplication(const UString &dir, const UString &path, QWidget *window) override;
    HRESULT StartApplicationKnown(const UString &dir, const UString &path, QWidget *window,
                                  IQtProcessHandle *&process) override;
    HRESULT CreateProcess(const UString &program, const UString &parameters,
                          const UStringVector &paths, QWidget *window) override;
    IQtProcessServices *ProcessServices() override { return &_processServices; }
    HRESULT CreateKnownProcess(const UString &program, const UString &parameters,
                               const UStringVector &paths, QWidget *window,
                               IQtProcessHandle *&process) override;
};
