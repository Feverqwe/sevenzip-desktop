// OS-association boundary for the source external-open process workflow.
#pragma once
#include "filemanager/external/qtexternalapplications.h"
#include "platform/process/qtexternalprocess.h"
#include <QUrl>
#include <functional>

class QtAssociatedFileServices final : public IQtExternalFileServices {
    std::function<bool(const QUrl &)> _opener;
    QtKnownAssociationOpener _knownOpener;
    QtNativeProcessServices _processServices;
    std::function<bool(CPanel &, const QtArchiveUpdateBody &)> _updateRunner;
    std::function<bool()> _updateReady;
    std::function<bool(const QString &)> _hasAssociation;
    QtExternalSessionFactory _sessionFactory;

  public:
    void SetExternalSessionFactory(QtExternalSessionFactory factory) {
        _sessionFactory = std::move(factory);
    }
    std::shared_ptr<IQtExternalSessionOwner> CreateExternalSession(CPanel &source,
                                                                   const CTempFileInfo &file,
                                                                   const QString &settings,
                                                                   const QString &prefix) override {
        return _sessionFactory ? _sessionFactory(source, file, settings, prefix) : nullptr;
    }
    explicit QtAssociatedFileServices(
        std::function<bool(const QUrl &)> opener,
        std::function<bool(CPanel &, const QtArchiveUpdateBody &)> updateRunner = {},
        std::function<bool()> updateReady = {},
        std::function<bool(const QString &)> hasAssociation = {},
        QtKnownAssociationOpener knownOpener = {});
    bool ConfirmArchiveFallback(const UString &path, QWidget *window) override;
    bool CanHandleExternalUpdate() const override;
    bool RunArchiveUpdate(CPanel &panel, const QtArchiveUpdateBody &body) override;
    HRESULT StartApplication(const UString &dir, const UString &path, QWidget *window) override;
    HRESULT StartApplicationKnown(const UString &dir, const UString &path, QWidget *window,
                                  IQtProcessHandle *&process) override;
    IQtProcessServices *ProcessServices() override { return &_processServices; }
};
