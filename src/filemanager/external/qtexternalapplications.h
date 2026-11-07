// Local services for external file associations and archive updates.
#pragma once

#include "Common/MyString.h"
#include "Common/MyWindows.h"
#include <QString>
#include <functional>
#include <memory>
#include <optional>

class CPanel;
struct CSelectedState;
class QWidget;
class IQtProcessHandle;
class IQtProcessServices;
class IQtExternalSessionOwner;
struct CTempFileInfo;
using QtExternalSessionFactory = std::function<std::shared_ptr<IQtExternalSessionOwner>(
    CPanel &, const CTempFileInfo &, const QString &, const QString &)>;
using QtArchiveUpdateBody = std::function<bool(std::optional<CSelectedState> &)>;
using QtKnownAssociationOpener = std::function<HRESULT(const UString &, IQtProcessHandle *&)>;

// Default OS associations can successfully launch without a process handle.
// A process handle is available only when the association can provide one.
class IQtExternalFileServices {
  public:
    virtual ~IQtExternalFileServices() = default;
    virtual bool ConfirmArchiveFallback(const UString &, QWidget *) { return true; }
    virtual HRESULT StartApplication(const UString &dir, const UString &path, QWidget *window) = 0;
    virtual IQtProcessServices *ProcessServices() { return nullptr; }
    virtual std::shared_ptr<IQtExternalSessionOwner>
    CreateExternalSession(CPanel &, const CTempFileInfo &, const QString &, const QString &) {
        return {};
    }
    virtual bool CanHandleExternalUpdate() const { return true; }
    virtual bool RunArchiveUpdate(CPanel &, const QtArchiveUpdateBody &body);
    virtual HRESULT StartApplicationKnown(const UString &dir, const UString &path, QWidget *window,
                                          IQtProcessHandle *&process) {
        process = nullptr;
        return StartApplication(dir, path, window);
    }
};
