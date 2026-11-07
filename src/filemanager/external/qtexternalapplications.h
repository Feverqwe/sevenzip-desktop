// Local services and launch adapters for configured external applications.
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

// Default OS associations can successfully launch without a process handle.
// Known-process/editor services are a separate mandatory transport extension.
class IQtExternalFileServices {
  public:
    virtual ~IQtExternalFileServices() = default;
    virtual bool ConfirmArchiveFallback(const UString &, QWidget *) { return true; }
    virtual HRESULT StartApplication(const UString &dir, const UString &path, QWidget *window) = 0;
    // Qt keeps base parameters and file paths separate at the process boundary.
    virtual HRESULT CreateProcess(const UString &, const UString &, const UStringVector &,
                                  QWidget *) {
        return E_NOTIMPL;
    }
    virtual IQtProcessServices *ProcessServices() { return nullptr; }
    virtual std::shared_ptr<IQtExternalSessionOwner>
    CreateExternalSession(CPanel &, const CTempFileInfo &, const QString &, const QString &) {
        return {};
    }
    virtual bool CanHandleExternalUpdate() const { return true; }
    virtual bool RunArchiveUpdate(CPanel &, const QtArchiveUpdateBody &body);
    virtual HRESULT CreateKnownProcess(const UString &, const UString &, const UStringVector &,
                                       QWidget *, IQtProcessHandle *&process) {
        process = nullptr;
        return E_NOTIMPL;
    }
    virtual HRESULT StartApplicationKnown(const UString &dir, const UString &path, QWidget *window,
                                          IQtProcessHandle *&process) {
        process = nullptr;
        return StartApplication(dir, path, window);
    }
};

// Adapted from 7-Zip PanelItemOpen.cpp (0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
void SplitCmdLineSmart(const UString &cmd, UString &prg, UString &params);
HRESULT StartAppWithParams(const UString &cmd, const UStringVector &paramVector,
                           IQtExternalFileServices &process, QWidget *window,
                           IQtProcessHandle **knownProcess = nullptr);
HRESULT StartEditApplication(const UString &path, bool useEditor, QWidget *window,
                             IQtExternalFileServices &process, const QString &settingsPath,
                             IQtProcessHandle **knownProcess = nullptr);
